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
#include <array>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <thread>
#include <vector>
#include <cmath>
#include <cstdlib>
#include <memory>
#include <cstdio>
#include <mutex>

#include "cm.h"
#include "float_env.h"
#include "snd_scene.h"
#include "snd_paths.h"
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

// The game's collision (CM_BoxTrace; before 3.1, now only for COD_BRO_SOUND_SCENE_CHECK).
Hit trace_game(const float start[3], const float direction[3]) {
    alignas(16) uint8_t results[0x40] = {};  // endpos +0, plane +0x10, fraction +0x20, surface flags +0x24
    alignas(16) const float zero[4] = {0, 0, 0, 0};
    alignas(16) float from[4] = {start[0], start[1], start[2], 0};
    alignas(16) float end[4] = {start[0] + direction[0] * kRange, start[1] + direction[1] * kRange,
                                start[2] + direction[2] * kRange, 0};
    CM_BoxTrace(nullptr, 0, zero, results, from, end, zero, nullptr, kSolidMask, 0);
    const float fraction = *reinterpret_cast<const float*>(results + 0x20);
    return {fraction * kRange, fraction < 1.0f, (*reinterpret_cast<const uint32_t*>(results + 0x24) >> 20) & 31};
}

// The share of the line from the listener `a` to a point `b` near a source that is blocked: 0
// clear, 1 a wall, glass less. Hits within kListenerGap of the listener (a wall the player leans
// against or stands in) and kPointGap of the point do not count, nor does a line starting inside
// a wall (fraction 0; playtest 10-30: everything muffled at a ruin's window).
struct Line {
    float share = 0;     // blocked: 0 clear, 1 a wall, 0.35 glass
    float fraction = 1;  // of the traced part; 0: started inside a wall
    float hit[3] = {0, 0, 0};
    uint32_t flags = 0, contents = 0;
    bool traced = false;  // false: too short to count
};

Line trace_line_game(const float a[3], const float b[3]) {
    constexpr float kListenerGap = 32.0f, kPointGap = 12.0f;  // world units
    Line line;
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length <= kListenerGap + kPointGap) return line;
    const float s = kListenerGap / length, t = kPointGap / length;
    alignas(16) uint8_t results[0x40] = {};
    alignas(16) const float zero[4] = {0, 0, 0, 0};
    alignas(16) float from[4] = {a[0] + d[0] * s, a[1] + d[1] * s, a[2] + d[2] * s, 0};
    alignas(16) float end[4] = {b[0] - d[0] * t, b[1] - d[1] * t, b[2] - d[2] * t, 0};
    CM_BoxTrace(nullptr, 0, zero, results, from, end, zero, nullptr, kSolidMask, 0);
    line.traced = true;
    line.fraction = *reinterpret_cast<const float*>(results + 0x20);
    for (int k = 0; k < 3; ++k) line.hit[k] = reinterpret_cast<const float*>(results)[k];
    line.flags = *reinterpret_cast<const uint32_t*>(results + 0x24);
    line.contents = *reinterpret_cast<const uint32_t*>(results + 0x28);
    if (line.fraction >= 1.0f || line.fraction <= 0.0f) return line;
    constexpr uint32_t kGlass = 9;
    line.share = ((line.flags >> 20) & 31) == kGlass ? 0.35f : 1.0f;
    return line;
}


// COD_BRO_SOUND_SCENE_CHECK=1: every sound ray also through the game's collision, the differences counted and logged
// (3.1's check: our scene against the game's on the same recordings).
struct SceneCheck {
    bool on = [] {
        const char* v = std::getenv("COD_BRO_SOUND_SCENE_CHECK");
        return v && *v && *v != '0';
    }();
    uint64_t rays = 0, both_hit = 0, ours_only = 0, game_only = 0, far_apart = 0;
    double distance_sum = 0;
    uint64_t lines = 0, line_disagree = 0, line_glass_disagree = 0;
    double ours_us = 0, game_us = 0;  // time in each (the check runs both)
    void report() {
        if ((rays + lines) % 5000) return;
        SndScene_Log("scene check: %llu rays (%llu both hit, %llu ours only, %llu game only, %llu more than 8 "
                 "apart, mean distance difference %.2f), %llu lines (%llu blocked differently, %llu glass differently)",
                 (unsigned long long)rays, (unsigned long long)both_hit, (unsigned long long)ours_only,
                 (unsigned long long)game_only, (unsigned long long)far_apart,
                 both_hit ? distance_sum / double(both_hit) : 0.0, (unsigned long long)lines,
                 (unsigned long long)line_disagree, (unsigned long long)line_glass_disagree);
        SndScene_Log("scene check: per ray/line %.2f us ours, %.2f us the game's", ours_us / double(rays + lines),
                     game_us / double(rays + lines));
    }
};
SceneCheck g_check;

// The scene the calling thread's rays use, taken by SndEnv_BeginRays (once per sound update; a mark's dump).
thread_local std::shared_ptr<const AcousticScene> g_scene;

const AcousticScene* scene() {
    if (!g_scene) g_scene = SndScene_Current();
    return g_scene.get();
}

double now_us() {
    return std::chrono::duration<double, std::micro>(std::chrono::steady_clock::now().time_since_epoch()).count();
}

Hit trace(const float start[3], const float direction[3]) {
    const double t0 = g_check.on ? now_us() : 0;
    Hit ours{kRange, false, 0};
    if (const AcousticScene* s = scene()) {
        const AcousticScene::Hit h = s->nearest(start, direction, kRange, kSolidMask);
        if (h.hit) ours = {h.distance, true, h.surface};
    }
    if (g_check.on) {
        const double t1 = now_us();
        const Hit game = trace_game(start, direction);
        g_check.ours_us += t1 - t0;
        g_check.game_us += now_us() - t1;
        ++g_check.rays;
        if (ours.hit && game.hit) {
            ++g_check.both_hit;
            const float d = std::fabs(ours.distance - game.distance);
            g_check.distance_sum += d;
            if (d > 8.0f) ++g_check.far_apart;
        } else if (ours.hit) {
            ++g_check.ours_only;
        } else if (game.hit) {
            ++g_check.game_only;
        }
        g_check.report();
    }
    return ours;
}

Line trace_line(const float a[3], const float b[3]) {
    constexpr float kListenerGap = 32.0f, kPointGap = 12.0f;  // world units
    Line line;
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (length <= kListenerGap + kPointGap) return line;
    const float s = kListenerGap / length, t = kPointGap / length;
    const double t0 = g_check.on ? now_us() : 0;
    const float from[3] = {a[0] + d[0] * s, a[1] + d[1] * s, a[2] + d[2] * s};
    const float end[3] = {b[0] - d[0] * t, b[1] - d[1] * t, b[2] - d[2] * t};
    line.traced = true;
    if (const AcousticScene* sc = scene()) {
        const float dir[3] = {end[0] - from[0], end[1] - from[1], end[2] - from[2]};
        const AcousticScene::Hit h = sc->nearest(from, dir, 1.0f, kSolidMask);
        if (h.hit) {
            line.fraction = h.distance;
            for (int k = 0; k < 3; ++k) line.hit[k] = from[k] + dir[k] * line.fraction;
            line.flags = uint32_t(h.surface) << 20;
            line.contents = h.contents;
        } else {
            for (int k = 0; k < 3; ++k) line.hit[k] = end[k];
        }
    }
    if (line.fraction < 1.0f && line.fraction > 0.0f) {
        constexpr uint32_t kGlass = 9;
        line.share = ((line.flags >> 20) & 31) == kGlass ? 0.35f : 1.0f;
    }
    if (g_check.on) {
        const double t1 = now_us();
        const Line game = trace_line_game(a, b);
        g_check.ours_us += t1 - t0;
        g_check.game_us += now_us() - t1;
        ++g_check.lines;
        if ((game.share > 0) != (line.share > 0)) { ++g_check.line_disagree; }
        else if (game.share != line.share) ++g_check.line_glass_disagree;
        g_check.report();
    }
    return line;
}

float blocked(const float a[3], const float b[3]) { return trace_line(a, b).share; }

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
    // Enclosed: -1000 mB as the I3DL2 room presets (was -300: small stone rooms rang, playtest 10-30),
    // + up to 2 dB for enclosed hard places (stone, concrete, brick: user 10-30, -1000 a little dry
    // there only): by enclosure and by how little the surfaces take in (mid absorption 0.03: all,
    // 0.15 and up: none).
    const float hard = std::clamp((0.15f - p.alpha.mid) / 0.12f, 0.0f, 1.0f);
    e.room = lerp(-3000.0f, -1000.0f, enclosure) + 200.0f * enclosure * hard;
    e.room_hf = lerp(-1200.0f, -100.0f, 1.0f - p.alpha.high);
    e.room_rolloff = 0.0f;
    e.decay_time = decay;
    e.decay_hf_ratio = std::clamp(decay_high / decay, 0.1f, 2.0f);
    // Close reflecting walls give strong first reflections.
    e.reflections = lerp(-2400.0f, -400.0f, reflectance * reflectance * std::clamp(1.0f - nearest / 1200.0f, 0.0f, 1.0f));
    e.reflections_delay = std::min(2.0f * nearest * m / kSpeedOfSound, 0.3f);
    // The late reverb: as loud as the roof is closed.
    e.reverb = lerp(-2600.0f, 0.0f, 1.0f - p.sky);
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

constexpr float kLevel[8][3] = {{1, 0, 0},           {0.7071f, 0.7071f, 0}, {0, 1, 0},  {-0.7071f, 0.7071f, 0},
                               {-1, 0, 0},          {-0.7071f, -0.7071f, 0}, {0, -1, 0}, {0.7071f, -0.7071f, 0}};
constexpr float kUp[5][3] = {{0, 0, 1},
                            {0.7071f, 0, 0.7071f},
                            {-0.7071f, 0, 0.7071f},
                            {0, 0.7071f, 0.7071f},
                            {0, -0.7071f, 0.7071f}};
constexpr float kDown[3] = {0, 0, -1};

// Surface type names (bits 20-24 of a hit's surface flags), for the mark dump.
constexpr const char* kSurfaceNames[32] = {
    "default", "bark", "brick", "carpet", "cloth", "concrete", "dirt", "flesh", "foliage", "glass", "grass", "gravel",
    "ice", "metal", "mud", "paper", "plaster", "rock", "sand", "snow", "water", "wood", "asphalt", "23", "24", "25",
    "26", "27", "28", "29", "30", "31"};

struct State {
    uint32_t updates = 0;
    bool active = false;
    Environment current = kSilent;
};
State g_state;

}  // namespace

// Low frequencies (125-250 Hz) of the same surfaces (3.5; same sources as kAbsorption): glass and thin panels take in
// lows (they move), heavy hard walls barely.
constexpr float kAbsorptionLow[32] = {
    0.03f, 0.10f, 0.03f, 0.05f, 0.30f, 0.01f, 0.15f, 0.25f, 0.10f, 0.30f, 0.11f, 0.25f, 0.01f, 0.10f, 0.10f, 0.10f,
    0.013f, 0.01f, 0.15f, 0.45f, 0.01f, 0.15f, 0.02f, 0.03f, 0.03f, 0.03f, 0.03f, 0.03f, 0.03f, 0.03f, 0.03f, 0.03f};

// The detailed probe (3.5): kDenseRays directions over the sphere from the listener, a quarter of them from 4
// points 48 units round it (that it sees) - Sabine per band from the room's measured volume and surfaces, early
// reflections from the nearby hard surfaces, and for echoes the distant hard ones.
constexpr int kDenseRays = 256;
const std::array<std::array<float, 3>, kDenseRays>& dense_directions() {
    static const auto directions = [] {
        std::array<std::array<float, 3>, kDenseRays> d{};
        const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < kDenseRays; ++i) {
            const float z = 1.0f - 2.0f * (float(i) + 0.5f) / kDenseRays;
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            d[i] = {r * std::cos(golden * float(i)), r * std::sin(golden * float(i)), z};
        }
        return d;
    }();
    return directions;
}

struct EchoPick {
    bool found = false;
    audio::Echo echo;          // spread set by the game thread (listener's facing)
    float direction[3] = {0, 0, 0};  // world, of the first echo
};

// Fills `target` (detail) and `echo` from the rays; false without a scene.
bool probe_dense(const float listener[3], bool detail, Environment& target, EchoPick& echo,
                 const audio::ReverbTuning& t) {
    const AcousticScene* sc = scene();
    if (!sc) return false;
    const auto& dirs = dense_directions();
    constexpr float kOpen = 1200.0f;  // a ray that hits nothing: an opening this far, taking in everything
    const float m = kMetres;
    double volume = 0, surface = 0, absorb[3] = {0, 0, 0};
    double hits = 0, alpha[3] = {0, 0, 0}, metal = 0;
    double up_rays = 0, up_open = 0, side_rays = 0, side_hits = 0;
    double near_sum = 0, near_sq = 0, delay_w = 0, refl_from[3] = {0, 0, 0}, rev_from[3] = {0, 0, 0}, hard_sum = 0;
    struct Candidate {
        float strength, distance, dir[3], hard;
    };
    std::vector<Candidate> candidates;
    const auto cast = [&](const float origin[3], int stride, double weight) {
        for (int i = 0; i < kDenseRays; i += stride) {
            const float* d = dirs[i].data();
            const AcousticScene::Hit h = sc->nearest(origin, d, kRange, kSolidMask);
            const float r = h.hit ? std::max(h.distance, 8.0f) : kOpen;
            const float rm = r * m;
            const float cosine = h.hit ? std::max(0.25f, std::fabs(h.normal[0] * d[0] + h.normal[1] * d[1] +
                                                                    h.normal[2] * d[2]))
                                       : 1.0f;
            const double w = weight * stride / kDenseRays;  // share of the sphere this ray stands for
            volume += w * 4.18879 * double(rm) * rm * rm;     // (4 pi / 3) r^3
            const double area = w * 12.56637 * double(rm) * rm / cosine;
            surface += area;
            const float a_low = h.hit ? kAbsorptionLow[h.surface] : 1.0f;
            const float a_mid = h.hit ? kAbsorption[h.surface].mid : 1.0f;
            const float a_high = h.hit ? kAbsorption[h.surface].high : 1.0f;
            absorb[0] += area * a_low;
            absorb[1] += area * a_mid;
            absorb[2] += area * a_high;
            if (d[2] > 0.3f) {
                up_rays += w;
                up_open += h.hit ? 0 : w;
            }
            if (std::fabs(d[2]) < 0.3f) {
                side_rays += w;
                side_hits += h.hit ? w : 0;
            }
            if (!h.hit) continue;
            hits += w;
            alpha[0] += w * a_low;
            alpha[1] += w * a_mid;
            alpha[2] += w * a_high;
            metal += h.surface == kMetal ? w : 0;
            const float hard = 1.0f - a_mid;
            const float near = hard * std::clamp(1.0f - r / 1200.0f, 0.0f, 1.0f);
            near_sum += w * near;
            near_sq += w * near * near;
            delay_w += w * near * near * 2.0 * rm / kSpeedOfSound;
            for (int k = 0; k < 3; ++k) {
                refl_from[k] += w * near * d[k];
                rev_from[k] += w * hard * d[k];
            }
            hard_sum += w * hard;
            if (origin == listener && r > 600.0f && std::fabs(d[2]) < 0.5f)
                candidates.push_back({hard * cosine * 600.0f / r, r, {d[0], d[1], d[2]}, hard});
        }
    };
    cast(listener, 1, 0.5);
    constexpr std::array<std::array<float, 2>, 4> kAround = {{{48, 0}, {-48, 0}, {0, 48}, {0, -48}}};
    std::vector<std::array<float, 3>> origins;  // the points round the listener that it sees
    for (const auto& o : kAround) {
        const std::array<float, 3> origin = {listener[0] + o[0], listener[1] + o[1], listener[2]};
        if (!sc->blocked_any(listener, origin.data(), kSolidMask)) origins.push_back(origin);
    }
    const int around = int(origins.size());
    for (const auto& origin : origins) cast(origin.data(), 4, 0.5 / around);
    // Normalise to one sphere (the listener's half + the points' half; without points the listener's is all).
    const double total = around ? 1.0 : 0.5;
    const auto norm = [&](double v) { return v / total; };
    const float sky = up_rays > 0 ? float(up_open / up_rays) : 0.0f;
    const float walls = side_rays > 0 ? float(side_hits / side_rays) : 0.0f;
    const float enclosure = 0.6f * (1.0f - sky) + 0.4f * walls;

    // Echoes: the strongest distant hard surface, and a second one at least 40 ms later.
    std::sort(candidates.begin(), candidates.end(),
              [](const Candidate& a, const Candidate& b) { return a.strength > b.strength; });
    if (!candidates.empty() && sky >= 0.5f) {
        const Candidate& first = candidates[0];
        const float delay1 = 2.0f * first.distance * m / kSpeedOfSound;
        float delay2 = delay1;
        for (const Candidate& c : candidates) {
            const float d = 2.0f * c.distance * m / kSpeedOfSound;
            if (d - delay1 >= 0.04f) {
                delay2 = d;
                break;
            }
        }
        echo.found = true;
        echo.echo.level = std::clamp(first.strength * sky * 0.35f, 0.0f, 0.3f);
        echo.echo.delay = std::min(delay1, 0.207f);
        echo.echo.lr_delay = std::clamp(delay2 - delay1, 0.0f, 0.404f);
        echo.echo.damping = std::clamp(0.3f + 0.5f * first.distance / kRange, 0.0f, 0.95f);
        for (int k = 0; k < 3; ++k) echo.direction[k] = first.dir[k];
    }
    if (!detail) return true;
    if (enclosure <= 0.02f) {
        target = kSilent;
        return true;
    }
    const double v = norm(volume), area = norm(surface);
    const double clutter = t.clutter * area;
    double decay[3];
    for (int b = 0; b < 3; ++b) {
        const double air = b == 2 ? 4.0 * kAirHigh * v : 0.0;
        decay[b] = std::clamp(0.161 * v / std::max(norm(absorb[b]) + clutter + air, 0.1), 0.1, 8.0);
    }
    const float a_low = hits > 0 ? float(alpha[0] / hits) : 0.05f, a_mid = hits > 0 ? float(alpha[1] / hits) : 0.05f,
                a_high = hits > 0 ? float(alpha[2] / hits) : 0.05f;
    Environment e;
    const float hard = std::clamp((0.15f - a_mid) / 0.12f, 0.0f, 1.0f);
    e.room = lerp(-3000.0f, -1000.0f, enclosure) + 200.0f * enclosure * hard;
    e.room_hf = lerp(-1200.0f, -100.0f, 1.0f - a_high);
    e.room_lf = -1000.0f * a_low;
    e.decay_time = float(decay[1]);
    e.decay_hf_ratio = std::clamp(float(decay[2] / decay[1]), 0.1f, 2.0f);
    e.decay_lf_ratio = std::clamp(float(decay[0] / decay[1]), 0.1f, 2.0f);
    const float strength = near_sum > 0 ? float(near_sq / near_sum) : 0.0f;  // the near hard surfaces' weight
    e.reflections = lerp(-2400.0f, -400.0f, strength);
    e.reflections_delay = near_sq > 0 ? std::min(float(delay_w / near_sq), 0.3f) : 0.02f;
    e.reverb = lerp(-2600.0f, 0.0f, 1.0f - sky);
    e.reverb_delay = std::clamp(float(4.0 * v / std::max(area, 1.0) / kSpeedOfSound), 0.005f, 0.1f);
    e.diffusion = lerp(30.0f, 100.0f, 1.0f - sky) * lerp(1.0f, 0.6f, hits > 0 ? float(metal / hits) : 0.0f);
    e.density = 100.0f;
    e.hf_reference = 5000.0f;
    e.room += t.amount * 100.0f;
    e.decay_time = std::clamp(e.decay_time * t.decay, 0.1f, 20.0f);
    e.decay_hf_ratio = std::clamp(e.decay_hf_ratio * t.brightness, 0.1f, 2.0f);
    e.reflections += t.reflections * 100.0f;
    const float rf = float(std::sqrt(refl_from[0] * refl_from[0] + refl_from[1] * refl_from[1] +
                                     refl_from[2] * refl_from[2]));
    for (int k = 0; k < 3; ++k) {
        e.reflections_pan[k] = near_sum > 0 ? float(refl_from[k] / near_sum) : 0.0f;
        e.reverb_pan[k] = hard_sum > 0 ? float(0.5 * rev_from[k] / hard_sum) : 0.0f;
    }
    (void)rf;
    target = e;
    return true;
}

// The probe rays from the listener -> the environment there (false: no scene to trace).
bool probe_environment(const float listener[3], Environment& target) {
    if (!scene()) return false;
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
    target = environment_of(p, SndEnv_Tuning());
    return true;
}

namespace {

constexpr int kOcclusionPoints = 7;

// SndEnv_Occlusion's points; false when the source is at the listener.
bool occlusion_points(const float listener[3], const float source[3], std::array<float, 3> (&points)[kOcclusionPoints]) {
    // Volumetric, as Steam Audio's occlusion: lines to points spread over a sphere around the
    // source (radius kRadius, its size), the share blocked. The centre is at shoulder height (the
    // origin is mostly a soldier's feet); one point leans towards the listener (where a rifle's
    // muzzle is: a soldier back from a window or round a corner is heard through the opening),
    // the others to the sides, above and below. Earlier tries (playtests 10-30) - one line, the
    // clearest of three, five above and beside - muffled gunfire in plain sight.
    constexpr float kRadius = 40.0f;  // world units
    const float centre[3] = {source[0], source[1], source[2] + 48};
    float toward[3] = {listener[0] - centre[0], listener[1] - centre[1], listener[2] - centre[2]};
    const float length = std::sqrt(toward[0] * toward[0] + toward[1] * toward[1] + toward[2] * toward[2]);
    if (length < 1.0f) return false;
    for (float& v : toward) v /= length;
    float side[3] = {-toward[1], toward[0], 0};
    const float flat = std::sqrt(side[0] * side[0] + side[1] * side[1]);
    if (flat > 1e-3f) {
        side[0] /= flat;
        side[1] /= flat;
    } else {
        side[0] = 1;
        side[1] = 0;
    }
    auto at = [&](float f, float s, float u) {
        return std::array<float, 3>{centre[0] + (toward[0] * f + side[0] * s) * kRadius,
                                    centre[1] + (toward[1] * f + side[1] * s) * kRadius,
                                    centre[2] + (toward[2] * f + side[2] * s + u) * kRadius};
    };
    const std::array<float, 3> list[kOcclusionPoints] = {
        at(0, 0, 0), at(1, 0, 0), at(0.7f, 0, 0.7f), at(0, 1, 0), at(0, -1, 0), at(0, 0, 1), at(0, 0, -0.7f),
    };
    std::copy(std::begin(list), std::end(list), std::begin(points));
    return true;
}

}  // namespace

// Sound bends over and round what blocks it (playtest 10-30: a tank behind a parapet, all lines
// clipping its top, came out muffled though plainly in view): when the line to the centre is
// blocked, a way over or round the hit - points above it and to its sides - with both legs clear
// is looked for, and the sound is only as muffled as that detour is long (the path idea of
// Rainbow Six Siege's and Hitman's propagation, over one edge). Returns 0..1 (1: no way found).
float detour(const float listener[3], const float point[3], const Line& line, float* edge_out = nullptr) {
    const float d[3] = {point[0] - listener[0], point[1] - listener[1], point[2] - listener[2]};
    const float straight = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    float side[3] = {-d[1], d[0], 0};
    const float flat = std::sqrt(side[0] * side[0] + side[1] * side[1]);
    if (flat > 1e-3f) {
        side[0] /= flat;
        side[1] /= flat;
    }
    // Over the top first (low walls, parapets, rubble), then round either side.
    static const float kSteps[] = {24, 48, 96, 160};
    float best = 1.0f;
    for (const float step : kSteps) {
        const float candidates[3][3] = {
            {line.hit[0], line.hit[1], line.hit[2] + step},
            {line.hit[0] + side[0] * step, line.hit[1] + side[1] * step, line.hit[2]},
            {line.hit[0] - side[0] * step, line.hit[1] - side[1] * step, line.hit[2]},
        };
        for (const auto& edge : candidates) {
            if (blocked(listener, edge) > 0.0f || blocked(edge, point) > 0.0f) continue;
            const float a[3] = {edge[0] - listener[0], edge[1] - listener[1], edge[2] - listener[2]};
            const float b[3] = {point[0] - edge[0], point[1] - edge[1], point[2] - edge[2]};
            const float extra = std::sqrt(a[0] * a[0] + a[1] * a[1] + a[2] * a[2]) +
                                std::sqrt(b[0] * b[0] + b[1] * b[1] + b[2] * b[2]) - straight;
            // Grazing an edge already costs a little; 200 units (5 m) further round: all.
            const float cost = 0.2f + 0.8f * std::clamp(extra / 200.0f, 0.0f, 1.0f);
            if (cost < best && edge_out)
                for (int k = 0; k < 3; ++k) edge_out[k] = edge[k];
            best = std::min(best, cost);
        }
        if (best < 1.0f) break;  // the nearest way found
    }
    return best;
}

float SndEnv_Occlusion(const float listener[3], const float source[3]) {
    std::array<float, 3> points[kOcclusionPoints];
    if (!scene() || !occlusion_points(listener, source, points)) return 0.0f;
    // In view - the line to the sound's centre or to the point towards the listener (a muzzle)
    // clear: the direct sound is heard in full (user 10-30: what bounces off walls only adds).
    const Line centre = trace_line(listener, points[0].data());
    if (centre.share <= 0.0f || blocked(listener, points[1].data()) <= 0.0f) return 0.0f;
    // Hidden: the share of the sphere's lines blocked (some may pass round an edge), at most as
    // much as the shortest way round the obstacle costs.
    float sum = centre.share;
    for (int i = 1; i < kOcclusionPoints; ++i) sum += blocked(listener, points[i].data());
    return std::min(sum / float(kOcclusionPoints), detour(listener, points[0].data(), centre));
}

namespace {

// Transmission (3.2): sound through a solid, per band (below ~250 Hz, middle, above ~5 kHz), in dB at the material's
// reference thickness (world units); thicker or thinner by the mass law, 6 dB per doubling, within [half, +24 dB].
// Building figures (sound reduction of single walls, e.g. Sharp / ISO 12354 tables: 2 cm timber ~20 dB, half a
// brick ~40 dB, 20 cm concrete ~50 dB, 4 mm glass ~25 dB at mid) scaled to how game walls are built; "est." =
// no measured figure, set by similar materials. The game's walls are often thin brushes standing for thick ones.
struct TransmissionLoss {
    float db[3];
    float reference;  // world units
};
constexpr TransmissionLoss kTransmission[32] = {
    {{25, 35, 45}, 8},   // 0 default (est.: a plastered wall)
    {{10, 16, 24}, 4},   // 1 bark (est.: as wood)
    {{32, 42, 52}, 8},   // 2 brick
    {{14, 20, 26}, 2},   // 3 carpet (est.: with what it lies on)
    {{2, 4, 7}, 1},      // 4 cloth (a curtain)
    {{36, 46, 56}, 8},   // 5 concrete
    {{30, 40, 50}, 8},   // 6 dirt (earth banks)
    {{8, 12, 18}, 8},    // 7 flesh (est.)
    {{2, 4, 7}, 8},      // 8 foliage (not traced)
    {{14, 22, 28}, 1},   // 9 glass, a window pane
    {{30, 40, 50}, 8},   // 10 grass (the ground under it)
    {{30, 40, 50}, 8},   // 11 gravel
    {{25, 35, 45}, 8},   // 12 ice (est.)
    {{14, 22, 30}, 1},   // 13 metal (sheet / plate)
    {{30, 40, 50}, 8},   // 14 mud
    {{2, 4, 6}, 1},      // 15 paper
    {{24, 32, 40}, 4},   // 16 plaster
    {{40, 50, 60}, 8},   // 17 rock
    {{22, 32, 42}, 8},   // 18 sand (sandbags)
    {{25, 35, 45}, 8},   // 19 snow (est.)
    {{30, 40, 50}, 8},   // 20 water
    {{10, 16, 24}, 4},   // 21 wood (planks)
    {{36, 46, 56}, 8},   // 22 asphalt (est.: as concrete)
    {{25, 35, 45}, 8}, {{25, 35, 45}, 8}, {{25, 35, 45}, 8}, {{25, 35, 45}, 8}, {{25, 35, 45}, 8},
    {{25, 35, 45}, 8}, {{25, 35, 45}, 8}, {{25, 35, 45}, 8}, {{25, 35, 45}, 8},  // unused
};
// A detour over or round an edge (diffraction) loses by its cost (0.2 grazing .. 1 none found), highs most.
constexpr float kDetourBase[3] = {1, 2, 4}, kDetourSlope[3] = {6, 12, 20};
constexpr float kTransmissionFloor = 18.0f;  // dB, the middle band: never quieter (user 11-15, realistic but trackable)

void add_slab(uint8_t surface, float thickness, float db[3]) {
    const TransmissionLoss& m = kTransmission[surface & 31];
    const float scale = 6.0f * std::log2(std::max(thickness, 0.25f) / m.reference);
    for (int b = 0; b < 3; ++b) db[b] += std::clamp(m.db[b] + scale, m.db[b] * 0.5f, m.db[b] + 24.0f);
}

// What the solids between a and b take from a sound, per band (dB), the line trimmed as trace_line's. Each solid
// is an enter .. leave pair of the scene's crossings; a leave without its enter (the line started inside) is not
// counted, an enter without a leave runs to the end. Returns the number of solids.
int line_loss(const float a[3], const float b[3], float db[3], std::string* describe = nullptr) {
    constexpr float kListenerGap = 32.0f, kPointGap = 12.0f;
    const AcousticScene* sc = scene();
    const float d[3] = {b[0] - a[0], b[1] - a[1], b[2] - a[2]};
    const float length = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
    if (!sc || length <= kListenerGap + kPointGap) return 0;
    const float s = kListenerGap / length, t = kPointGap / length;
    const float from[3] = {a[0] + d[0] * s, a[1] + d[1] * s, a[2] + d[2] * s};
    const float to[3] = {b[0] - d[0] * t, b[1] - d[1] * t, b[2] - d[2] * t};
    const float traced = length - kListenerGap - kPointGap;
    AcousticScene::Crossing crossings[32];
    const size_t n = std::min<size_t>(sc->crossings(from, to, kSolidMask, crossings, std::size(crossings)), 32);
    int solids = 0;
    bool inside = false;
    float enter = 0;
    uint8_t surface = 0;
    const auto slab = [&](float leave) {
        const float thickness = (leave - enter) * traced;
        add_slab(surface, thickness, db);
        ++solids;
        if (describe) {
            char text[96];
            std::snprintf(text, sizeof(text), " %s %.0f", kSurfaceNames[surface & 31], double(thickness));
            *describe += text;
        }
    };
    for (size_t i = 0; i < n; ++i) {
        const AcousticScene::Crossing& c = crossings[i];
        if (c.entering) {
            if (inside) slab(c.t);  // a solid inside a solid (overlapping brushes): the outer one ends here
            inside = true;
            enter = c.t;
            surface = c.surface;
        } else if (inside) {
            slab(c.t);
            inside = false;
        }
    }
    if (inside) slab(1.0f);
    return solids;
}

}  // namespace

namespace {

// Partial cover (3.3): 30 directions spread evenly over a sphere (a Fibonacci lattice), for points over the
// sound's size besides the centre and the muzzle point.
constexpr int kCoverPoints = 30;
const std::array<std::array<float, 3>, kCoverPoints>& cover_directions() {
    static const auto directions = [] {
        std::array<std::array<float, 3>, kCoverPoints> d{};
        const float golden = 3.14159265f * (3.0f - std::sqrt(5.0f));
        for (int i = 0; i < kCoverPoints; ++i) {
            const float z = 1.0f - 2.0f * (float(i) + 0.5f) / kCoverPoints;
            const float r = std::sqrt(std::max(0.0f, 1.0f - z * z));
            d[i] = {r * std::cos(golden * float(i)), r * std::sin(golden * float(i)), z};
        }
        return d;
    }();
    return directions;
}

}  // namespace

SndPropagation SndEnv_Propagation(const float listener[3], const float source[3], bool partial_cover,
                                  std::string* describe) {
    SndPropagation p;
    std::array<float, 3> fixed[kOcclusionPoints];
    if (!scene() || !occlusion_points(listener, source, fixed)) return p;
    const Line centre = trace_line(listener, fixed[0].data());
    if (centre.share <= 0.0f || blocked(listener, fixed[1].data()) <= 0.0f) return p;  // in view: all of it
    // The points: the 7 of occlusion, or (partial cover) the centre, the muzzle point and 30 over a sphere of the
    // same size (40 units round the centre) - finer steps as cover changes.
    std::vector<std::array<float, 3>> points(fixed, fixed + (partial_cover ? 2 : kOcclusionPoints));
    if (partial_cover) {
        constexpr float kRadius = 40.0f;
        for (const auto& d : cover_directions())
            points.push_back({fixed[0][0] + d[0] * kRadius, fixed[0][1] + d[1] * kRadius, fixed[0][2] + d[2] * kRadius});
    }
    const int count = int(points.size());
    float share = 0;
    double energy[3] = {0, 0, 0};
    for (int i = 0; i < count; ++i) {
        const float line_share = i == 0 ? centre.share : blocked(listener, points[i].data());
        share += line_share;
        if (line_share <= 0.0f) {
            for (double& e : energy) e += 1.0;
            continue;
        }
        float db[3] = {0, 0, 0};
        if (describe) *describe += i ? " |" : "lines:";
        if (!line_loss(listener, points[i].data(), db, describe)) {
            // Blocked by the trace but no solid between the crossings (a one-sided mesh seen from its front with
            // nothing behind it, e.g. a terrain edge): as a thin default wall.
            add_slab(0, 2.0f, db);
            if (describe) *describe += " (surface only)";
        }
        for (int b = 0; b < 3; ++b) energy[b] += std::pow(10.0, -db[b] / 10.0);
    }
    for (double& e : energy) e /= count;
    const float cost = detour(listener, points[0].data(), centre);
    p.occlusion = std::min(share / float(count), cost);
    if (cost < 1.0f)
        for (int b = 0; b < 3; ++b)
            energy[b] = std::max(energy[b], std::pow(10.0, -(kDetourBase[b] + kDetourSlope[b] * cost) / 10.0));
    float db[3];
    for (int b = 0; b < 3; ++b) db[b] = float(-10.0 * std::log10(std::max(energy[b], 1e-30)));
    // The floor holds the middle band; the others keep their difference to it (behind concrete: quieter AND dull; lows
    // at most 6 dB better - the device holds them at the middle anyway),
    // the highs at most 12 dB below the middle there.
    const float mid = std::min(db[1], kTransmissionFloor);
    p.loss_db[1] = mid;
    p.loss_db[0] = std::max(0.0f, mid + std::clamp(db[0] - db[1], -6.0f, 0.0f));
    p.loss_db[2] = mid + std::clamp(db[2] - db[1], 0.0f, 12.0f);
    p.detour = cost;
    return p;
}

SndPropagation SndEnv_QuickPropagation(const float listener[3], const float source[3]) {
    SndPropagation p;
    std::array<float, 3> points[kOcclusionPoints];
    if (!scene() || !occlusion_points(listener, source, points)) return p;
    const Line centre = trace_line(listener, points[0].data());
    if (centre.share <= 0.0f || blocked(listener, points[1].data()) <= 0.0f) return p;
    float db[3] = {0, 0, 0};
    if (!line_loss(listener, points[0].data(), db)) add_slab(0, 2.0f, db);
    p.occlusion = centre.share;
    const float mid = std::min(db[1], kTransmissionFloor);
    p.loss_db[1] = mid;
    p.loss_db[0] = std::max(0.0f, mid + std::clamp(db[0] - db[1], -6.0f, 0.0f));
    p.loss_db[2] = mid + std::clamp(db[2] - db[1], 0.0f, 12.0f);
    return p;
}

audio::Transmission SndEnv_ToTransmission(const float loss_db[3]) {
    audio::Transmission t;
    t.gain = std::pow(10.0f, -loss_db[1] / 20.0f);
    t.gain_lf = std::min(1.0f, std::pow(10.0f, -(loss_db[0] - loss_db[1]) / 20.0f));
    t.gain_hf = std::min(1.0f, std::pow(10.0f, -(loss_db[2] - loss_db[1]) / 20.0f));
    return t;
}

namespace {

// The sound rays' worker thread (3.1): one job at a time - the probe (when due) and every listed voice's occlusion.
struct RayJob {
    bool probe = false;
    bool partial_cover = false;
    bool openings = false;
    bool reverb_detail = false, echoes = false;
    uint32_t update = 0;
    float listener[3] = {0, 0, 0};
    std::vector<SndVoiceRay> voices;
};
struct RayResult {
    bool probed = false;  // a probe ran and found a scene
    Environment target;
    EchoPick echo;        // probe with echoes on
    std::vector<SndVoiceRay> voices;
};

// Openings (3.4): the air grid's ways to the hidden voices, added to what comes through the walls. The grid follows
// the listener (rebuilt 4 times a second, or when it moved a cell); each way loses per band by its bends and its
// extra length over the straight line.
// The air grid is built on a thread of its own (a first build takes up to ~80 ms; occlusion keeps running): the
// ray worker asks for a new field now and then and uses the latest finished one.
class PathWorker {
public:
    ~PathWorker() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            wake_.notify_one();
        }
        if (thread_.joinable()) thread_.join();
    }
    void request(std::shared_ptr<const AcousticScene> scene, const float listener[3]) {
        std::lock_guard lock(mutex_);
        if (busy_) return;
        if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
        scene_ = std::move(scene);
        for (int k = 0; k < 3; ++k) listener_[k] = listener[k];
        busy_ = true;
        wake_.notify_one();
    }
    std::shared_ptr<const SoundField> field() {
        std::lock_guard lock(mutex_);
        return field_;
    }
    void reset() {
        std::unique_lock lock(mutex_);
        idle_.wait(lock, [this] { return !busy_; });
        field_.reset();
    }

private:
    void loop() {
        platform::HostFloatEnv float_env;
        std::unique_lock lock(mutex_);
        while (true) {
            wake_.wait(lock, [this] { return stopping_ || busy_; });
            if (stopping_) return;
            std::shared_ptr<const AcousticScene> scene = std::move(scene_);
            const float listener[3] = {listener_[0], listener_[1], listener_[2]};
            lock.unlock();
            std::shared_ptr<const SoundField> field = builder_.build(*scene, listener);
            scene.reset();
            lock.lock();
            field_ = std::move(field);
            busy_ = false;
            idle_.notify_all();
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::thread thread_;
    SoundPaths builder_;  // its thread only
    std::shared_ptr<const AcousticScene> scene_;
    std::shared_ptr<const SoundField> field_;
    float listener_[3] = {0, 0, 0};
    bool busy_ = false, stopping_ = false;
};
PathWorker g_path_worker;
uint32_t g_paths_update = 0;
float g_paths_at[3] = {0, 0, 0};
constexpr float kBendLoss[3] = {2, 5, 9};  // dB per bend (low, mid, high: corners take the highs)
constexpr float kExtraLoss[3] = {2, 4, 6};  // dB per doubling of (1 + extra / 200 units)

void add_paths(const RayJob& job, std::vector<SndVoiceRay>& voices) {
    const AcousticScene* sc = scene();
    if (!sc) return;
    bool any = false;
    for (const SndVoiceRay& v : voices) any |= v.propagation.occlusion > 0.0f;
    if (!any) return;
    const float moved = std::sqrt((job.listener[0] - g_paths_at[0]) * (job.listener[0] - g_paths_at[0]) +
                                  (job.listener[1] - g_paths_at[1]) * (job.listener[1] - g_paths_at[1]) +
                                  (job.listener[2] - g_paths_at[2]) * (job.listener[2] - g_paths_at[2]));
    std::shared_ptr<const SoundField> field = g_path_worker.field();
    if (!field || job.update - g_paths_update >= 15 || moved > 64.0f) {
        g_path_worker.request(g_scene, job.listener);
        g_paths_update = job.update;
        for (int k = 0; k < 3; ++k) g_paths_at[k] = job.listener[k];
    }
    if (!field) return;
    for (SndVoiceRay& v : voices) {
        SndPropagation& p = v.propagation;
        if (p.occlusion <= 0.0f) continue;
        const float centre[3] = {v.position[0], v.position[1], v.position[2] + 48};
        const SoundField::Path path = field->path_to(*sc, centre);
        if (!path.found || path.bends < 1) continue;
        const float direct = std::sqrt((centre[0] - job.listener[0]) * (centre[0] - job.listener[0]) +
                                       (centre[1] - job.listener[1]) * (centre[1] - job.listener[1]) +
                                       (centre[2] - job.listener[2]) * (centre[2] - job.listener[2]));
        if (direct < 1.0f) continue;
        const float extra = std::max(0.0f, path.length - direct);
        double energy[3], path_energy[3];
        for (int b = 0; b < 3; ++b) {
            const float loss = kBendLoss[b] * float(std::min(path.bends, 4)) +
                               kExtraLoss[b] * std::log2(1.0f + extra / 200.0f);
            path_energy[b] = std::pow(10.0, -loss / 10.0);
            energy[b] = std::pow(10.0, -p.loss_db[b] / 10.0);
        }
        const double through = energy[1];
        for (int b = 0; b < 3; ++b) {
            const double total = std::min(1.0, energy[b] + path_energy[b]);
            p.loss_db[b] = float(-10.0 * std::log10(std::max(total, 1e-30)));
        }
        p.loss_db[0] = std::min(p.loss_db[0], p.loss_db[1]);  // lows as before: not below the middle
        p.path_share = float(path_energy[1] / (through + path_energy[1]));
        p.path_length = path.length;
        p.path_bends = path.bends;
        float to_opening[3], to_source[3];
        for (int k = 0; k < 3; ++k) {
            to_opening[k] = path.opening[k] - job.listener[k];
            to_source[k] = (centre[k] - job.listener[k]) / direct;
        }
        const float lo = std::sqrt(to_opening[0] * to_opening[0] + to_opening[1] * to_opening[1] +
                                   to_opening[2] * to_opening[2]);
        if (lo < 1.0f) continue;
        float d[3];
        for (int k = 0; k < 3; ++k) d[k] = (1 - p.path_share) * to_source[k] + p.path_share * to_opening[k] / lo;
        const float ld = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        for (int k = 0; k < 3; ++k) p.direction[k] = ld > 1e-4f ? d[k] / ld : to_source[k];
    }
}

// COD_BRO_SOUND_TIME=1: time spent on sound rays, logged every 600 updates (game thread per update, worker per job).
struct SoundTime {
    bool on = [] {
        const char* v = std::getenv("COD_BRO_SOUND_TIME");
        return v && *v && *v != '0';
    }();
    std::atomic<uint64_t> worker_ns{0}, jobs{0}, worker_max_ns{0}, voices{0};
    uint64_t game_ns = 0, game_max_ns = 0, updates = 0;
};
SoundTime g_time;

uint64_t now_ns() {
    return uint64_t(std::chrono::duration_cast<std::chrono::nanoseconds>(
                        std::chrono::steady_clock::now().time_since_epoch())
                        .count());
}

RayResult run_job(const RayJob& job) {
    const uint64_t start = g_time.on ? now_ns() : 0;
    struct Timed {
        uint64_t start;
        size_t voices;
        ~Timed() {
            if (!start) return;
            const uint64_t ns = now_ns() - start;
            g_time.worker_ns += ns;
            g_time.voices += voices;
            ++g_time.jobs;
            uint64_t max = g_time.worker_max_ns.load();
            while (ns > max && !g_time.worker_max_ns.compare_exchange_weak(max, ns)) {}
        }
    } timed{start, job.voices.size()};
    SndEnv_BeginRays();
    RayResult r;
    if (job.probe) {
        if (job.reverb_detail || job.echoes) {
            Environment dense;
            r.probed = probe_dense(job.listener, job.reverb_detail, dense, r.echo, SndEnv_Tuning());
            if (job.reverb_detail) r.target = dense;
            else r.probed = probe_environment(job.listener, r.target) && r.probed;
        } else {
            r.probed = probe_environment(job.listener, r.target);
        }
    }
    r.voices = job.voices;
    for (SndVoiceRay& v : r.voices) v.propagation = SndEnv_Propagation(job.listener, v.position, job.partial_cover);
    if (job.openings) add_paths(job, r.voices);
    g_scene.reset();  // a finished level's scene is not kept alive by this thread
    return r;
}

class RayWorker {
public:
    ~RayWorker() { stop(); }
    // Starts the job unless one is running (false: the caller skips this update's job).
    bool post(RayJob job) {
        std::unique_lock lock(mutex_);
        if (busy_) return false;
        if (!thread_.joinable()) thread_ = std::thread([this] { loop(); });
        job_ = std::move(job);
        busy_ = true;
        wake_.notify_one();
        return true;
    }
    bool take(RayResult& out) {
        std::lock_guard lock(mutex_);
        if (!has_result_) return false;
        out = std::move(result_);
        has_result_ = false;
        return true;
    }
    // Waits for a running job and drops its result.
    void drain() {
        std::unique_lock lock(mutex_);
        idle_.wait(lock, [this] { return !busy_; });
        has_result_ = false;
    }
    void stop() {
        {
            std::lock_guard lock(mutex_);
            stopping_ = true;
            wake_.notify_one();
        }
        if (thread_.joinable()) thread_.join();
    }

private:
    void loop() {
        platform::HostFloatEnv float_env;  // not the title thread's modes
        std::unique_lock lock(mutex_);
        while (true) {
            wake_.wait(lock, [this] { return stopping_ || (busy_ && !running_); });
            if (stopping_) return;
            running_ = true;
            const RayJob job = std::move(job_);
            lock.unlock();
            RayResult result = run_job(job);
            lock.lock();
            result_ = std::move(result);
            has_result_ = true;
            running_ = busy_ = false;
            idle_.notify_all();
        }
    }
    std::mutex mutex_;
    std::condition_variable wake_, idle_;
    std::thread thread_;
    RayJob job_;
    RayResult result_;
    bool busy_ = false, running_ = false, has_result_ = false, stopping_ = false;
};
RayWorker g_worker;
uint32_t g_last_probe = 0;  // the update of the last posted probe
bool g_partial_cover_last = false;  // as last passed to SndEnv_Update (the mark dump)

}  // namespace

void SndEnv_Update(audio::SoundDevice& device, const audio::Listener& l, const SndRayOptions& options,
                   std::vector<SndVoiceRay> voices, std::vector<SndVoiceRay>& done) {
    const bool reverb = options.reverb, partial_cover = options.partial_cover;
    State& s = g_state;
    const uint32_t update = s.updates++;
    done.clear();
    RayResult result;
    bool have = false;
    RayJob job;
    job.probe = (reverb || options.echoes) && update - g_last_probe >= kProbeEvery;
    job.reverb_detail = reverb && options.reverb_detail;
    job.echoes = options.echoes;
    job.partial_cover = partial_cover;
    job.openings = options.openings;
    job.update = update;
    g_partial_cover_last = partial_cover;
    for (int k = 0; k < 3; ++k) job.listener[k] = l.position[k];
    job.voices = std::move(voices);
    if (g_check.on) {  // the game's traces in the check: the game thread only
        result = run_job(job);
        have = true;
        if (job.probe) g_last_probe = update;
    } else {
        have = g_worker.take(result);
        const bool probe = job.probe;
        if ((probe || !job.voices.empty()) && g_worker.post(std::move(job)) && probe) g_last_probe = update;
    }
    if (have) done = std::move(result.voices);
    // Echoes (3.5): the first echo's side from the listener's facing; nothing found (or switched off): silent.
    if (have && result.probed) {
        audio::Echo echo = result.echo.found && options.echoes ? result.echo.echo : audio::Echo{};
        if (result.echo.found) {
            const float* f = l.front;
            const float* t = l.top;
            const float right[3] = {t[1] * f[2] - t[2] * f[1], t[2] * f[0] - t[0] * f[2], t[0] * f[1] - t[1] * f[0]};
            echo.spread = std::clamp(result.echo.direction[0] * right[0] + result.echo.direction[1] * right[1] +
                                         result.echo.direction[2] * right[2],
                                     -1.0f, 1.0f);
        }
        device.set_echo(echo);
    }
    if (!reverb) return;
    if (!world_loaded()) {
        if (s.active) device.set_environment(kSilent);
        s.active = false;
        return;
    }
    if (have && result.probed) {
        // A new place fades in over about half a second (probes 10 times a second).
        s.current = s.active ? mix(s.current, result.target, 0.25f) : result.target;
        s.active = true;
    }
    if (s.active) device.set_environment(facing(s.current, l));
}

void SndEnv_DescribeOcclusion(const float listener[3], const float source[3], char* out, size_t size) {
    std::array<float, 3> points[kOcclusionPoints];
    if (!size) return;
    out[0] = 0;
    if (!world_loaded() || !occlusion_points(listener, source, points)) return;
    size_t used = size_t(std::snprintf(out, size, "listener %.0f %.0f %.0f source %.0f %.0f %.0f:", listener[0],
                                       listener[1], listener[2], source[0], source[1], source[2]));
    if (used < size) used += size_t(std::snprintf(out + used, size - used, " occlusion %.2f", SndEnv_Occlusion(listener, source)));
    for (const auto& point : points) {
        if (used >= size) break;
        const Line line = trace_line(listener, point.data());
        const float d[3] = {line.hit[0] - listener[0], line.hit[1] - listener[1], line.hit[2] - listener[2]};
        const float from_listener = std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2]);
        const float e[3] = {point[0] - line.hit[0], point[1] - line.hit[1], point[2] - line.hit[2]};
        const float to_point = std::sqrt(e[0] * e[0] + e[1] * e[1] + e[2] * e[2]);
        if (!line.traced)
            used += size_t(std::snprintf(out + used, size - used, " [short]"));
        else if (line.fraction >= 1.0f)
            used += size_t(std::snprintf(out + used, size - used, " [clear]"));
        else
            used += size_t(std::snprintf(out + used, size - used,
                                         " [%s at %.0f %.0f %.0f, %.0f from listener, %.0f before point, contents %x, "
                                         "flags %x]",
                                         line.fraction <= 0.0f ? "inside" : "hit", line.hit[0], line.hit[1],
                                         line.hit[2], from_listener, to_point, line.contents, line.flags));
    }
}

void SndEnv_SetTuning(const audio::ReverbTuning& tuning) {
    std::lock_guard lock(g_tuning_mutex);
    g_tuning = tuning;
}

namespace {
SoundPaths g_mark_paths;  // the mark dump's own grid (game thread; the worker's is its own)
std::shared_ptr<const SoundField> g_mark_field;
}

void SndEnv_DescribeForMark(const float listener[3], std::string& out) {
    SndEnv_BeginRays();
    g_mark_field.reset();
    if (const AcousticScene* sc = scene()) g_mark_field = g_mark_paths.build(*sc, listener);
    char line[256];
    const auto add = [&](const char* format, auto... args) {
        std::snprintf(line, sizeof(line), format, args...);
        out += line;
    };
    if (!world_loaded()) {
        out += "no world loaded\n";
        return;
    }
    add("listener %.0f %.0f %.0f\n", double(listener[0]), double(listener[1]), double(listener[2]));
    const Environment& e = g_state.current;
    add("environment%s: room %.0f mB (hf %.0f), decay %.2f s (hf ratio %.2f), reflections %.0f mB at %.0f ms, late "
        "%.0f mB at %.0f ms\n",
        g_state.active ? "" : " (none active)", double(e.room), double(e.room_hf), double(e.decay_time),
        double(e.decay_hf_ratio), double(e.reflections), double(e.reflections_delay * 1000), double(e.reverb),
        double(e.reverb_delay * 1000));
    out += "probe rays (world direction: distance, surface):\n";
    const auto ray = [&](const char* group, const float* d) {
        const Hit hit = trace(listener, d);
        if (hit.hit)
            add("  %-5s %5.2f %5.2f %5.2f: %6.0f %s\n", group, double(d[0]), double(d[1]), double(d[2]),
                double(hit.distance), kSurfaceNames[hit.type]);
        else
            add("  %-5s %5.2f %5.2f %5.2f: open\n", group, double(d[0]), double(d[1]), double(d[2]));
    };
    for (const auto& d : kLevel) ray("level", d);
    for (const auto& d : kUp) ray("up", d);
    ray("down", kDown);
}

void SndEnv_DescribeVoiceForMark(const float listener[3], const float source[3], std::string& out) {
    char line[256];
    const auto add = [&](const char* format, auto... args) {
        std::snprintf(line, sizeof(line), format, args...);
        out += line;
    };
    std::array<float, 3> points[kOcclusionPoints];
    if (!world_loaded() || !occlusion_points(listener, source, points)) {
        out += "    (no lines: no world or at the listener)\n";
        return;
    }
    static const char* const kPointNames[kOcclusionPoints] = {"centre", "toward", "toward-up", "left", "right", "up",
                                                              "down"};
    for (int i = 0; i < kOcclusionPoints; ++i) {
        const Line l = trace_line(listener, points[i].data());
        if (!l.traced) {
            add("    %-9s short\n", kPointNames[i]);
            continue;
        }
        if (l.fraction >= 1.0f) {
            add("    %-9s clear\n", kPointNames[i]);
            continue;
        }
        const float d[3] = {l.hit[0] - listener[0], l.hit[1] - listener[1], l.hit[2] - listener[2]};
        add("    %-9s %s %s at %.0f %.0f %.0f (%.0f from listener), contents %x, blocks %.2f\n", kPointNames[i],
            l.fraction <= 0.0f ? "inside" : "hit", kSurfaceNames[(l.flags >> 20) & 31], double(l.hit[0]),
            double(l.hit[1]), double(l.hit[2]), double(std::sqrt(d[0] * d[0] + d[1] * d[1] + d[2] * d[2])),
            unsigned(l.contents), double(l.share));
    }
    const Line centre = trace_line(listener, points[0].data());
    if (centre.share > 0.0f) add("    detour cost %.2f\n", double(detour(listener, points[0].data(), centre)));
    add("    occlusion now %.2f\n", double(SndEnv_Occlusion(listener, source)));
    std::string solids;
    const SndPropagation p = SndEnv_Propagation(listener, source, g_partial_cover_last, &solids);
    add("    transmission now: loss %.1f / %.1f / %.1f dB (low / mid / high), detour cost %.2f\n", double(p.loss_db[0]),
        double(p.loss_db[1]), double(p.loss_db[2]), double(p.detour));
    if (!solids.empty()) out += "    " + solids + "\n";
    if (const AcousticScene* sc = scene(); sc && g_mark_field && p.occlusion > 0.0f) {
        const float centre[3] = {source[0], source[1], source[2] + 48};
        std::vector<std::array<float, 3>> waypoints;
        const SoundField::Path path = g_mark_field->path_to(*sc, centre, &waypoints);
        if (!path.found) {
            out += "    way round: none in the grid\n";
        } else {
            add("    way round: %.0f long, %d bends, via", double(path.length), path.bends);
            for (const auto& w : waypoints) add(" (%.0f %.0f %.0f)", double(w[0]), double(w[1]), double(w[2]));
            out += "\n";
        }
    }
}

size_t SndEnv_SoundPaths(const float listener[3], const float source[3], SoundPathLine* out, size_t max) {
    std::array<float, 3> points[kOcclusionPoints];
    if (!scene() || !occlusion_points(listener, source, points)) return 0;
    size_t n = 0;
    const auto add = [&](const float* a, const float* b, SoundPathLine::Kind kind) {
        if (n >= max) return;
        for (int k = 0; k < 3; ++k) {
            out[n].from[k] = a[k];
            out[n].to[k] = b[k];
        }
        out[n++].kind = kind;
    };
    Line centre;
    for (int i = 0; i < kOcclusionPoints; ++i) {
        const Line l = trace_line(listener, points[i].data());
        if (i == 0) centre = l;
        if (!l.traced || l.fraction >= 1.0f) {
            add(listener, points[i].data(), SoundPathLine::Clear);
        } else {
            add(listener, l.hit, l.share < 1.0f ? SoundPathLine::Glass : SoundPathLine::Blocked);
            add(l.hit, points[i].data(), SoundPathLine::Beyond);
        }
    }
    if (centre.share > 0.0f) {
        float edge[3];
        if (detour(listener, points[0].data(), centre, edge) < 1.0f) {
            add(listener, edge, SoundPathLine::Detour);
            add(edge, points[0].data(), SoundPathLine::Detour);
        }
    }
    return n;
}

audio::ReverbTuning SndEnv_Tuning() {
    std::lock_guard lock(g_tuning_mutex);
    return g_tuning;
}

void SndEnv_CountGameTime(uint64_t ns) {
    if (!g_time.on) return;
    g_time.game_ns += ns;
    g_time.game_max_ns = std::max(g_time.game_max_ns, ns);
    if (++g_time.updates % 600) return;
    const uint64_t jobs = g_time.jobs.exchange(0), worker = g_time.worker_ns.exchange(0);
    const uint64_t voices = g_time.voices.exchange(0), worker_max = g_time.worker_max_ns.exchange(0);
    SndScene_Log("sound time: game thread %.1f us per update (max %.1f), worker %llu jobs, %.1f us per job (max %.1f), "
                 "%.1f voices per job",
                 double(g_time.game_ns) / 600e3, double(g_time.game_max_ns) / 1e3, (unsigned long long)jobs,
                 jobs ? double(worker) / double(jobs) / 1e3 : 0.0, double(worker_max) / 1e3,
                 jobs ? double(voices) / double(jobs) : 0.0);
    g_time.game_ns = g_time.game_max_ns = 0;
}

void SndEnv_Reset() {
    g_worker.drain();
    g_path_worker.reset();
    g_last_probe = 0;
    g_state = State();
    g_scene.reset();
    SndScene_Reset();
}

void SndEnv_BeginRays() { g_scene = SndScene_Current(); }

}  // namespace game
