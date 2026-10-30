// Our reverb environment probe (enhanced sound only; not title code). See snd_env.h.
//
// Every few sound updates: rays from the listener through the world's collision (CM_BoxTrace,
// world only): 8 level, 5 upward, 1 down. They give the place's shape (walls, floor, ceiling;
// upward rays that hit nothing are open sky, the maps have no sky brushes) and what it is made
// of (each hit's surface type). The reverb follows from room acoustics:
// - decay time: Sabine, RT60 = 0.161 V / A, with V and the surface from a box fitted to the
//   rays and A = surface x absorption by material, open sky and missing walls absorbing all;
// - first reflections: after the way to the nearest wall and back; the late reverb after the
//   mean free path (4 V / S);
// - level: how enclosed the place is (walls around, a roof overhead); brightness: the same
//   Sabine at high frequencies (surfaces' high-frequency absorption and the air's) over the
//   mid one, so carpet and grass dull the tail and stone keeps it bright. An open field stays dry, a roofless ruin gives reflections and little
//   tail, a stone cellar rings;
// - direction: the reflections come from the near, hard walls (a stone wall on the right and
//   open ground on the left: from the right), the late reverb leans the same way; turned
//   with the listener on every update, between probes.
// The result glides towards each new estimate.
//
// The game has five unused I3DL2 presets of its own (FUN_002ad9d0, nothing calls it: Generic,
// Room, Hangar and two others); fixed rooms blended by one size did not tell places apart
// (playtest 10-29), so they are not used.

#include "snd_env.h"

#include <algorithm>
#include <cmath>
#include <mutex>

#include "cm.h"
#include "sound_device.h"
#include "title_data.h"

namespace game {

namespace {

using audio::Environment;

constexpr uint32_t kProbeEvery = 6;   // sound updates (60 per second)
constexpr float kRange = 3000.0f;     // world units (inches)
constexpr float kMetres = 0.0254f;    // per world unit
constexpr float kSpeedOfSound = 343;  // metres per second
// Solid surfaces and glass (glass is not solid, contents 0x10; foliage 0x2 and water 0x20 are
// left out: leaves barely reflect, water is underfoot).
constexpr uint32_t kSolidMask = 1 | 0x10;
constexpr Environment kSilent{};

// Each surface type's absorption coefficient (share of the sound a surface takes in) at mid
// (500-1000 Hz) and high (2-4 kHz) frequencies. The type is bits 20-24 of a hit's surface flags,
// as the game's surface parameter table (0x008d4e68, 16-byte rows: name, clears solid, surface
// flags, contents) assigns them to the material names. Values: octave-band tables of building
// materials (sengpielaudio.com "Absorption Coefficients of Building Materials and Finishes";
// window glass, brick, plaster, concrete, wood, carpet, curtains, water/ice, people), Egan
// "Architectural Acoustics" for loose gravel, CNOSSOS-EU ground factors (grass, soil and snow
// soft, gravel roads and hard ground reflecting) for the ground; "est." = no measured figure
// found, estimated from similar surfaces.
struct Absorption {
    float mid, high;
};
constexpr Absorption kAbsorption[32] = {
    {0.05f, 0.05f},  // 0 default (est.: an ordinary finished wall)
    {0.12f, 0.15f},  // 1 bark (est.: rough wood)
    {0.035f, 0.06f}, // 2 brick, unglazed
    {0.25f, 0.62f},  // 3 carpet on concrete
    {0.62f, 0.65f},  // 4 cloth (heavy curtain)
    {0.03f, 0.04f},  // 5 concrete (between smooth and painted block)
    {0.35f, 0.50f},  // 6 dirt (loose soil, est. between dry and moist)
    {0.44f, 0.50f},  // 7 flesh (people)
    {0.30f, 0.50f},  // 8 foliage (not traced: not solid)
    {0.15f, 0.055f}, // 9 glass, window
    {0.50f, 0.60f},  // 10 grass
    {0.67f, 0.70f},  // 11 gravel, loose (Egan)
    {0.014f, 0.022f},// 12 ice
    {0.035f, 0.02f}, // 13 metal (est.: like a large glass plate)
    {0.15f, 0.20f},  // 14 mud (est.: wet soil absorbs far less)
    {0.15f, 0.20f},  // 15 paper (est.)
    {0.025f, 0.045f},// 16 plaster, smooth
    {0.01f, 0.02f},  // 17 rock (marble/stone)
    {0.30f, 0.45f},  // 18 sand (est.: dry sand soft, wet sand hard)
    {0.60f, 0.85f},  // 19 snow (est.: very absorbent)
    {0.014f, 0.022f},// 20 water
    {0.10f, 0.08f},  // 21 wood (wood on joists / plywood)
    {0.02f, 0.03f},  // 22 asphalt (est.: like smooth concrete)
    {0.05f, 0.05f}, {0.05f, 0.05f}, {0.05f, 0.05f}, {0.05f, 0.05f}, {0.05f, 0.05f},
    {0.05f, 0.05f}, {0.05f, 0.05f}, {0.05f, 0.05f}, {0.05f, 0.05f},  // unused
};
// What the rays do not see takes in sound as well: furniture, debris, openings, people
// (ReverbTuning::clutter, 0.07 by default). Without it, Sabine with stone's 0.01 gives a
// 4 x 4 x 3 m stone room nearly 10 s; with it ~1.2 s.

std::mutex g_tuning_mutex;
audio::ReverbTuning g_tuning;
// Air takes in high frequencies too (energy attenuation per metre near 5 kHz, 20 C, 50 % humidity).
constexpr float kAirHigh = 0.009f;
constexpr uint32_t kMetal = 13;

float lerp(float a, float b, float t) { return a + (b - a) * t; }

Environment mix(const Environment& a, const Environment& b, float t) {
    Environment e{lerp(a.room, b.room, t),
                  lerp(a.room_hf, b.room_hf, t),
                  lerp(a.room_rolloff, b.room_rolloff, t),
                  lerp(a.decay_time, b.decay_time, t),
                  lerp(a.decay_hf_ratio, b.decay_hf_ratio, t),
                  lerp(a.reflections, b.reflections, t),
                  lerp(a.reflections_delay, b.reflections_delay, t),
                  lerp(a.reverb, b.reverb, t),
                  lerp(a.reverb_delay, b.reverb_delay, t),
                  lerp(a.diffusion, b.diffusion, t),
                  lerp(a.density, b.density, t),
                  lerp(a.hf_reference, b.hf_reference, t)};
    for (int i = 0; i < 3; ++i) {
        e.reflections_pan[i] = lerp(a.reflections_pan[i], b.reflections_pan[i], t);
        e.reverb_pan[i] = lerp(a.reverb_pan[i], b.reverb_pan[i], t);
    }
    return e;
}

bool world_loaded() {
    const uint8_t* world = title_global<const uint8_t*>(0x0095f4fc);
    return world && *reinterpret_cast<const int32_t*>(world + 0xc) > 0;
}

struct Hit {
    float distance;  // world units; kRange: nothing hit
    bool hit;
    uint32_t type;   // surface type (when hit)
};

Hit trace(const float start[3], const float direction[3]) {
    alignas(16) uint8_t results[0x40] = {};  // endpos +0, plane +0x10, fraction +0x20, surface flags +0x24
    alignas(16) const float zero[4] = {0, 0, 0, 0};
    alignas(16) float from[4] = {start[0], start[1], start[2], 0};
    alignas(16) float end[4] = {start[0] + direction[0] * kRange, start[1] + direction[1] * kRange,
                                start[2] + direction[2] * kRange, 0};
    CM_BoxTrace(nullptr, 0, zero, results, from, end, zero, nullptr, kSolidMask, 0);
    const float fraction = *reinterpret_cast<const float*>(results + 0x20);
    return {fraction * kRange, fraction < 1.0f, (*reinterpret_cast<const uint32_t*>(results + 0x24) >> 20) & 31};
}

// What the rays found.
struct Probe {
    float level[8];      // distances, world units (kRange: open)
    float up;            // straight up (kRange: open)
    float down;          // to the floor
    int walls = 0;       // level rays that hit
    float sky = 0;       // share of upward rays that hit nothing
    Absorption alpha{0, 0};  // average over the hits
    float metal = 0;         // share of the hits on metal
    float reflections_from[3] = {0, 0, 0};  // world direction (length: focus)
    float reverb_from[3] = {0, 0, 0};
};

Environment environment_of(const Probe& p, const audio::ReverbTuning& t) {
    const float enclosure = 0.6f * (1.0f - p.sky) + 0.4f * float(p.walls) / 8;
    if (enclosure <= 0.02f) return kSilent;  // an open field: dry

    // A box around the listener: opposite rays give width and depth, up and down the height.
    const float m = kMetres;
    const float width = std::clamp((p.level[0] + p.level[4]) * m, 1.5f, 76.0f);
    const float depth = std::clamp((p.level[2] + p.level[6]) * m, 1.5f, 76.0f);
    const float height = std::clamp((std::min(p.up, 1200.0f) + p.down) * m, 2.0f, 30.0f);
    const float volume = width * depth * height;
    const float floor_area = width * depth;
    const float wall_area = 2.0f * (width + depth) * height;
    const float surface = 2.0f * floor_area + wall_area;

    // Sabine at mid and high frequencies: the surfaces' absorption, the open part of the roof and
    // the missing walls taking in everything; at high frequencies the air too (4 m V).
    const float open_walls = 1.0f - float(p.walls) / 8;
    auto absorption = [&](float alpha) {
        return floor_area * alpha + floor_area * (p.sky + (1.0f - p.sky) * alpha) +
               wall_area * (open_walls + (1.0f - open_walls) * alpha);
    };
    const float decay =
        std::clamp(0.161f * volume / std::max(absorption(p.alpha.mid + t.clutter), 0.1f), 0.1f, 8.0f);
    const float decay_high =
        0.161f * volume / std::max(absorption(p.alpha.high + t.clutter) + 4.0f * kAirHigh * volume, 0.1f);
    const float reflectance = 1.0f - p.alpha.mid;

    float nearest = kRange;
    for (float d : p.level) nearest = std::min(nearest, d);
    nearest = std::min(nearest, p.up);

    Environment e;
    e.room = lerp(-3000.0f, -300.0f, enclosure);
    e.room_hf = lerp(-1200.0f, -100.0f, 1.0f - p.alpha.high);
    e.room_rolloff = 0.0f;
    e.decay_time = decay;
    e.decay_hf_ratio = std::clamp(decay_high / decay, 0.1f, 2.0f);
    // Close reflecting walls give strong first reflections.
    e.reflections = lerp(-2400.0f, -400.0f, reflectance * reflectance * std::clamp(1.0f - nearest / 1200.0f, 0.0f, 1.0f));
    e.reflections_delay = std::min(2.0f * nearest * m / kSpeedOfSound, 0.3f);
    // The late reverb: as loud as the roof is closed.
    e.reverb = lerp(-2600.0f, 200.0f, 1.0f - p.sky);
    e.reverb_delay = std::clamp(4.0f * volume / surface / kSpeedOfSound, 0.005f, 0.1f);
    e.diffusion = lerp(30.0f, 100.0f, 1.0f - p.sky) * lerp(1.0f, 0.6f, p.metal);  // metal rings
    e.density = 100.0f;
    e.hf_reference = 5000.0f;
    // The player's adjustments (millibels = dB x 100).
    e.room += t.amount * 100.0f;
    e.decay_time = std::clamp(e.decay_time * t.decay, 0.1f, 20.0f);
    e.decay_hf_ratio = std::clamp(e.decay_hf_ratio * t.brightness, 0.1f, 2.0f);
    e.reflections += t.reflections * 100.0f;
    // World directions for now (SndEnv_Update turns them to the listener).
    for (int i = 0; i < 3; ++i) {
        e.reflections_pan[i] = p.reflections_from[i];
        e.reverb_pan[i] = p.reverb_from[i];
    }
    return e;
}

// The environment with its world directions turned to the listener's (x right, y up, z ahead;
// right = top x front, as the sound placement uses them). The game's "top" is world down
// (0, 0, -1 seen in mission 1: its fix for the left-handed sound space), so top x front is the
// true right and up/down are mirrored as for every other sound.
Environment facing(Environment e, const audio::Listener& l) {
    const float* f = l.front;
    const float* t = l.top;
    const float r[3] = {t[1] * f[2] - t[2] * f[1], t[2] * f[0] - t[0] * f[2], t[0] * f[1] - t[1] * f[0]};
    auto turn = [&](float v[3]) {
        const float x = v[0] * r[0] + v[1] * r[1] + v[2] * r[2];
        const float y = v[0] * t[0] + v[1] * t[1] + v[2] * t[2];
        const float z = v[0] * f[0] + v[1] * f[1] + v[2] * f[2];
        v[0] = x;
        v[1] = y;
        v[2] = z;
    };
    turn(e.reflections_pan);
    turn(e.reverb_pan);
    return e;
}

struct State {
    uint32_t updates = 0;
    bool active = false;
    Environment current = kSilent;
};
State g_state;

}  // namespace

void SndEnv_Update(audio::SoundDevice& device, const audio::Listener& l) {
    State& s = g_state;
    if (s.updates++ % kProbeEvery) {
        if (s.active) device.set_environment(facing(s.current, l));
        return;
    }
    const float* listener = l.position;
    if (!world_loaded()) {
        if (s.active) device.set_environment(kSilent);
        s.active = false;
        return;
    }
    static const float kLevel[8][3] = {{1, 0, 0},           {0.7071f, 0.7071f, 0}, {0, 1, 0},  {-0.7071f, 0.7071f, 0},
                                       {-1, 0, 0},          {-0.7071f, -0.7071f, 0}, {0, -1, 0}, {0.7071f, -0.7071f, 0}};
    static const float kUp[5][3] = {{0, 0, 1},
                                    {0.7071f, 0, 0.7071f},
                                    {-0.7071f, 0, 0.7071f},
                                    {0, 0.7071f, 0.7071f},
                                    {0, -0.7071f, 0.7071f}};
    static const float kDown[3] = {0, 0, -1};
    Probe p;
    int hits = 0;
    auto material = [&](const Hit& hit) {
        if (!hit.hit) return;
        p.alpha.mid += kAbsorption[hit.type].mid;
        p.alpha.high += kAbsorption[hit.type].high;
        p.metal += hit.type == kMetal ? 1.0f : 0.0f;
        ++hits;
    };
    float near_weight = 0, hard_weight = 0;
    for (int i = 0; i < 8; ++i) {
        const Hit hit = trace(listener, kLevel[i]);
        p.level[i] = hit.distance;
        p.walls += hit.hit;
        material(hit);
        if (!hit.hit) continue;
        // Reflections from near, hard walls; the late reverb from hard ones at any distance.
        const float hard = 1.0f - kAbsorption[hit.type].mid;  // reflectance
        const float near = hard * std::clamp(1.0f - hit.distance / 1200.0f, 0.0f, 1.0f);
        for (int k = 0; k < 3; ++k) {
            p.reflections_from[k] += near * kLevel[i][k];
            p.reverb_from[k] += hard * kLevel[i][k];
        }
        near_weight += near;
        hard_weight += hard;
    }
    for (int k = 0; k < 3; ++k) {
        p.reflections_from[k] = near_weight > 0 ? p.reflections_from[k] / near_weight : 0.0f;
        p.reverb_from[k] = hard_weight > 0 ? 0.5f * p.reverb_from[k] / hard_weight : 0.0f;
    }
    for (int i = 0; i < 5; ++i) {
        const Hit hit = trace(listener, kUp[i]);
        if (i == 0) p.up = hit.distance;
        p.sky += hit.hit ? 0.0f : 0.2f;
        material(hit);
    }
    const Hit floor = trace(listener, kDown);
    p.down = floor.hit ? floor.distance : 64.0f;
    material(floor);
    p.alpha = hits ? Absorption{p.alpha.mid / float(hits), p.alpha.high / float(hits)} : Absorption{0.05f, 0.05f};
    p.metal = hits ? p.metal / float(hits) : 0.0f;
    const Environment target = environment_of(p, SndEnv_Tuning());
    // A new place fades in over about half a second (probes 10 times a second).
    s.current = s.active ? mix(s.current, target, 0.25f) : target;
    s.active = true;
    device.set_environment(facing(s.current, l));
}

void SndEnv_SetTuning(const audio::ReverbTuning& tuning) {
    std::lock_guard lock(g_tuning_mutex);
    g_tuning = tuning;
}

audio::ReverbTuning SndEnv_Tuning() {
    std::lock_guard lock(g_tuning_mutex);
    return g_tuning;
}

}  // namespace game
