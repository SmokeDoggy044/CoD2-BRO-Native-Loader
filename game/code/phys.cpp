#include "phys.h"

#include <cstring>

#include "lanes.h"
#include "phys_pool.h"
#include "qcommon_core.h"
#include "title_data.h"

namespace game {

namespace {

template <typename T>
T& at(const void* base, uint32_t offset) {
    return *reinterpret_cast<T*>(reinterpret_cast<uintptr_t>(base) + offset);
}

Lanes splat(float s) { return {{s, s, s, s}}; }

// The columns of a frame's 3x3 rotation (rows at +0, +0x10, +0x20), as the title's SHUFPS
// transpose builds them: lane 3 repeats lane 2.
void frame_columns(const float* frame, Lanes* columns) {
    for (int32_t c = 0; c < 3; ++c) columns[c] = {{frame[c], frame[4 + c], frame[8 + c], frame[8 + c]}};
}

// Into the frame: columns * v (x + y, then + z).
Lanes rotate_into(const Lanes* columns, const Lanes& v) {
    return add(add(mul(columns[0], v.v[0]), mul(columns[1], v.v[1])), mul(columns[2], v.v[2]));
}

// Out of the frame: origin + row0 x + row1 y + row2 z, in that order.
Lanes transform_out(const float* frame, const float* v) {
    const float x = v[0], y = v[1], z = v[2];
    return add(add(add(lanes(frame + 12), mul(lanes(frame), x)), mul(lanes(frame + 4), y)), mul(lanes(frame + 8), z));
}

// Rotation only: row0 x + row1 y + row2 z.
Lanes rotate_out(const float* frame, const float* v) {
    const float x = v[0], y = v[1], z = v[2];
    return add(add(mul(lanes(frame), x), mul(lanes(frame + 4), y)), mul(lanes(frame + 8), z));
}

Lanes div(const Lanes& a, const Lanes& b) {
    return {{a.v[0] / b.v[0], a.v[1] / b.v[1], a.v[2] / b.v[2], a.v[3] / b.v[3]}};
}

// dot(a, b) as the title sums a MULPS product: (x + y) + z.
float dot3(const Lanes& a, const Lanes& b) {
    const Lanes product = mul(a, b);
    platform_keep(product);
    return (product.v[0] + product.v[1]) + product.v[2];
}

// CMPLEPS(0, d) select: +extent where 0 <= d (ordered), -extent (sign bit flipped) elsewhere.
Lanes select_extent(const Lanes& d, const Lanes& extent) {
    Lanes out;
    for (int32_t i = 0; i < 4; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &extent.v[i], 4);
        if (!(0.0f <= d.v[i])) bits ^= 0x80000000u;
        std::memcpy(&out.v[i], &bits, 4);
    }
    return out;
}

using ShapeSupport = const float* XB_THISCALL(void* shape, float* out, const float* dir);
using ShapeIntersect = uint8_t XB_THISCALL(void* shape, const float* point, uint32_t arg2, float* hit, uint32_t arg4);

}  // namespace

// Vtable slot of the shape-in-frame wrappers: the shape's own support point (vtable +8) for the
// direction rotated into the frame, moved back out.
float* XB_THISCALL PhysShape_SupportInFrame(void* shape, float* out, const float* frame, const float* dir) {
    Lanes columns[3];
    float rows[12];
    std::memcpy(rows, frame, sizeof(rows));  // REP MOVSD copy taken before the call
    frame_columns(rows, columns);
    alignas(16) float local_dir[4];
    alignas(16) float support[4];
    std::memcpy(local_dir, rotate_into(columns, lanes(dir)).v, 16);
    void* const* vtable = *static_cast<void* const* const*>(shape);
    const float* p = title_function<ShapeSupport>(reinterpret_cast<uintptr_t>(vtable[2]))(shape, support, local_dir);
    alignas(16) float point[4];
    std::memcpy(point, p, 16);
    std::memcpy(out, transform_out(frame, point).v, 16);
    return out;
}

// Vtable slot: the shape's intersection test (vtable +0x10) with the point moved into the frame;
// a hit's point (hit+0) and normal (hit+0x10) are moved back out.
uint8_t XB_THISCALL PhysShape_IntersectInFrame(void* shape, const float* frame, const float* point, uint32_t arg3,
                                               float* hit, uint32_t arg5) {
    // The callee's output block holds the copy of the frame's first two rows until it writes it.
    alignas(16) float local[12];
    std::memcpy(local, frame, sizeof(local));
    Lanes columns[3];
    frame_columns(local, columns);
    const Lanes d = sub(lanes(point), lanes(frame + 12));
    platform_keep(d);
    alignas(16) float local_point[4];
    std::memcpy(local_point, rotate_into(columns, d).v, 16);
    void* const* vtable = *static_cast<void* const* const*>(shape);
    if (!title_function<ShapeIntersect>(reinterpret_cast<uintptr_t>(vtable[4]))(shape, local_point, arg3, local, arg5))
        return 0;
    std::memcpy(hit, transform_out(frame, local).v, 16);
    std::memcpy(hit + 4, rotate_out(frame, local + 4).v, 16);
    return 1;
}

// Separation bias of a contact: -(separation - 0.02) / max(dt, 1/240) (x87), scaled by 0.3 when
// negative, at least -limit; positive biases move to +0x78, negative ones stay at +0x7c.
void XB_THISCALL PhysContact_ComputeBias_2(uint8_t* contact, float dt, float limit) {
    const float min_dt = title_global<float>(0x00830678);  // 1/240
    if (!(dt > min_dt)) dt = min_dt;
    const long double separation = title_function<long double XB_THISCALL(uint8_t*)>(0x000bc510)(contact);  // PhysContact_GetSeparation
    const float bias = static_cast<float>(-((separation - title_global<float>(0x00830670)) / dt));  // 0.02
    float& out = at<float>(contact, 0x7c);
    out = bias;
    if (platform_x87_greater(title_global<float>(0x0083eafc), bias)) out = bias * title_global<float>(0x0083066c);  // 0, 0.3
    const float lowest = 0.0f - limit;
    if (lowest > out) out = lowest;
    const float value = out;
    at<float>(contact, 0x80) = 0.0f;
    if (value >= 0.0f) {
        at<uint32_t>(contact, 0x78) = at<uint32_t>(contact, 0x7c);
        out = 0.0f;
    } else {
        at<float>(contact, 0x78) = 0.0f;
    }
}

// Contact records (0xa0 bytes) come from a 16-byte aligned bump allocator (+0x2bc cursor, +0x2b8
// end) and are appended to a list: contacts at +0x2cc/+0x2d0/+0x2d4 (head, tail, count).
uint8_t* XB_THISCALL PhysWorld_AllocContact(uint8_t* world) {
    uint8_t* contact = reinterpret_cast<uint8_t*>((at<uint32_t>(world, 0x2bc) + 0xf) & ~0xfu);
    const uint32_t next = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(contact)) + 0xa0;
    if (next > at<uint32_t>(world, 0x2b8)) {
        contact = nullptr;
    } else {
        at<uint32_t>(world, 0x2bc) = next;
    }
    ++at<int32_t>(world, 0x2d4);
    uint8_t* const tail = at<uint8_t*>(world, 0x2d0);
    if (tail)
        at<uint8_t*>(tail, 0) = contact;
    else
        at<uint8_t*>(world, 0x2cc) = contact;
    at<uint8_t*>(world, 0x2d0) = contact;
    at<uint32_t>(contact, 0) = 0;
    return contact;
}

// Same allocator, list at +0x2e4/+0x2e8/+0x2ec; the record is set up by 0x000be080.
uint8_t* XB_THISCALL PhysContacts_Add(uint8_t* world, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4, uint32_t a5,
                                      uint32_t a6) {
    uint8_t* contact = reinterpret_cast<uint8_t*>((at<uint32_t>(world, 0x2bc) + 0xf) & ~0xfu);
    const uint32_t next = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(contact)) + 0xa0;
    if (next > at<uint32_t>(world, 0x2b8)) {
        contact = nullptr;
    } else {
        at<uint32_t>(world, 0x2bc) = next;
    }
    ++at<int32_t>(world, 0x2ec);
    uint8_t* const tail = at<uint8_t*>(world, 0x2e8);
    if (tail)
        at<uint8_t*>(tail, 0) = contact;
    else
        at<uint8_t*>(world, 0x2e4) = contact;
    at<uint8_t*>(world, 0x2e8) = contact;
    at<uint32_t>(contact, 0) = 0;
    using Init = void XB_THISCALL(uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    title_function<Init>(0x000be080)(contact, a1, a2, a3, a4, a5, a6);
    return contact;
}

void XB_REGS3 XB_CDECL PhysObjPool_Free(uint8_t* obj, uint32_t, uint8_t* pool) {
    const int32_t index = pool_index(pool, kPhysObjPool, obj);
    const int32_t slot = at<int32_t>(pool, kPhysObjPool.positions + static_cast<uint32_t>(index) * 4);
    if (at<int32_t>(pool, kPhysObjPool.count) == 1) {
        PhysObjPool_Reset(0, pool);
        return;
    }
    pool_swap_out(pool, kPhysObjPool, index, slot);
}

// Takes the next free vehicle physics object (five at most) and constructs it: vtable 0x0083c5f0,
// thirteen 8-byte slots from +0x1b0 with -1 in their first word.
uint8_t* XB_REGS2 XB_CDECL VehiclePhys_AllocFromPool(uint32_t, uint8_t* pool) {
    const int32_t count = at<int32_t>(pool, kVehiclePhysPool.count);
    if (count >= kVehiclePhysPool.entries) return nullptr;
    uint8_t* const phys = at<uint8_t*>(pool, kVehiclePhysPool.pointers + static_cast<uint32_t>(count) * 4);
    at<int32_t>(pool, kVehiclePhysPool.count) = count + 1;
    if (phys) {
        at<uint32_t>(phys, 0) = 0x0083c5f0;  // VehiclePhys vtable
        for (uint32_t i = 0; i < 13; ++i) at<int32_t>(phys, 0x1b0 + i * 8) = -1;
    }
    return phys;
}

void XB_REGS3 XB_CDECL VehiclePhysPool_Free(uint8_t* phys, uint32_t, uint8_t* pool) {
    const int32_t index = pool_index(pool, kVehiclePhysPool, phys);
    const int32_t slot = at<int32_t>(pool, kVehiclePhysPool.positions + static_cast<uint32_t>(index) * 4);
    if (at<int32_t>(pool, kVehiclePhysPool.count) == 1) {
        VehiclePhysPool_Reset(pool);
        return;
    }
    pool_swap_out(pool, kVehiclePhysPool, index, slot);
}

void XB_REGS2 XB_CDECL PhysObjPool_Reset(uint32_t, uint8_t* pool) { pool_reset(pool, kPhysObjPool); }

uint8_t* XB_REGS1 XB_CDECL VehiclePhysPool_Reset(uint8_t* pool) {
    pool_reset(pool, kVehiclePhysPool);
    return pool;  // EAX is never written
}

// out = v (EAX = out, ECX = v), or the splatted bound when v.x is below *lo / above *hi (CMPLESS:
// NaN takes the low bound).
float* XB_REGS3 XB_CDECL Vec4_ClampToScalarRange(float* out, uint32_t, const float* v, const float* lo, const float* hi) {
    alignas(16) float value[4];
    std::memcpy(value, v, 16);
    const float low = *lo;
    if (!(low <= value[0])) {
        const Lanes bound = splat(low);
        std::memcpy(out, bound.v, 16);
        return out;
    }
    const float high = *hi;
    if (!(value[0] <= high)) {
        const Lanes bound = splat(high);
        std::memcpy(out, bound.v, 16);
        return out;
    }
    std::memcpy(out, value, 16);
    return out;
}

// Shape support functions (vtable slot +8): the shape's farthest point along dir.

// Segment (ends at +0x30, +0x40): the end with the larger dot product (the first on ties and NaN).
float* XB_THISCALL PhysSegment_Support(uint8_t* shape, float* out, const float* dir) {
    const Lanes d = lanes(dir);
    const float* a = reinterpret_cast<const float*>(shape + 0x30);
    const float* b = reinterpret_cast<const float*>(shape + 0x40);
    const float da = dot3(d, lanes(a));
    const float db = dot3(d, lanes(b));
    const float* end = db > da ? b : a;
    std::memcpy(out, end, 16);
    return out;
}

// Sphere (centre +0x10, radius +0x1c): centre + dir * radius / |dir| (the scale on the x87).
float* XB_THISCALL PhysSphere_Support(uint8_t* shape, float* out, const float* dir) {
    const Lanes d = lanes(dir);
    const float length_squared = dot3(d, d);
    const float scale = static_cast<float>(at<float>(shape, 0x1c) / platform_x87_sqrt_ext(length_squared));
    const Lanes center = lanes(reinterpret_cast<const float*>(shape + 0x10));
    std::memcpy(out, add(center, mul(d, scale)).v, 16);
    return out;
}

// Box (centre +0x10, half extents +0x20, axis aligned): centre +- extent per lane.
float* XB_THISCALL PhysBox_Support(uint8_t* shape, float* out, const float* dir) {
    const Lanes d = lanes(dir);
    const Lanes extent = select_extent(d, lanes(reinterpret_cast<const float*>(shape + 0x20)));
    const Lanes center = lanes(reinterpret_cast<const float*>(shape + 0x10));
    std::memcpy(out, add(center, extent).v, 16);
    return out;
}

// Capsule (ends +0x30, +0x40, radius +0x50): the end along dir (b when (b - a).dir > 0) plus
// dir * radius / |dir|.
float* XB_THISCALL PhysCapsule_Support(uint8_t* shape, float* out, const float* dir) {
    const Lanes d = lanes(dir);
    const long double length = platform_x87_sqrt_ext(dot3(d, d));
    const float* a = reinterpret_cast<const float*>(shape + 0x30);
    const float* b = reinterpret_cast<const float*>(shape + 0x40);
    const float along = dot3(sub(lanes(b), lanes(a)), d);
    const float* end = along > title_global<float>(0x0083eafc) ? b : a;  // 0
    alignas(16) float point[4];
    std::memcpy(point, end, 16);
    const float scale = static_cast<float>(at<float>(shape, 0x50) / length);
    std::memcpy(out, add(lanes(point), mul(d, scale)).v, 16);
    return out;
}

// Oriented box (frame rows +0x30..+0x60, half extents +0x70): the corner along dir in box space,
// moved back out.
float* XB_THISCALL PhysOrientedBox_Support(uint8_t* shape, float* out, const float* dir) {
    alignas(16) float d[4];
    std::memcpy(d, dir, 16);
    float rows[12];
    std::memcpy(rows, shape + 0x30, sizeof(rows));
    Lanes columns[3];
    frame_columns(rows, columns);
    const Lanes local = rotate_into(columns, lanes(d));
    alignas(16) float corner[4];
    std::memcpy(corner, select_extent(local, lanes(reinterpret_cast<const float*>(shape + 0x70))).v, 16);
    std::memcpy(out, transform_out(reinterpret_cast<const float*>(shape + 0x30), corner).v, 16);
    return out;
}

// Sphere contact tests (vtable slot): a sphere (centre p, radius r) against the shape; on contact
// out+0 = the shape's surface point, out+0x10 = the normal (left unnormalized below 1e-4).

namespace {

// out+0x10 = d, divided by |d| (x87 FSQRT, stored as float) when |d|^2 > 1e-4.
void store_normal(float* out, const Lanes& d, float length_squared) {
    std::memcpy(out + 4, d.v, 16);
    if (length_squared > title_global<float>(0x0083ed50)) {  // 1e-4
        const float length = static_cast<float>(platform_x87_sqrt_ext(length_squared));
        std::memcpy(out + 4, div(lanes(out + 4), splat(length)).v, 16);
    }
}

}  // namespace

// Point shape (+0x10): contact when |p - point|^2 <= r^2; out = the point.
uint8_t XB_THISCALL PhysPoint_SphereContact(uint8_t* shape, const float* p, float r, float* out, uint32_t) {
    const float* point = reinterpret_cast<const float*>(shape + 0x10);
    const Lanes d = sub(lanes(p), lanes(point));
    const float length_squared = dot3(d, d);
    if (length_squared > r * r) return 0;
    const bool normalize = length_squared > title_global<float>(0x0083ed50);  // COMISS before the copies
    platform_keep(normalize);
    std::memcpy(out, point, 16);
    store_normal(out, d, length_squared);
    return 1;
}

// Sphere shape (centre +0x10, radius +0x1c): contact when |p - c|^2 <= (R + r)^2; out = c + n R.
uint8_t XB_THISCALL PhysSphere_SphereContact(uint8_t* shape, const float* p, float r, float* out, uint32_t) {
    const Lanes d = sub(lanes(p), lanes(reinterpret_cast<const float*>(shape + 0x10)));
    const float length_squared = dot3(d, d);
    const float reach = at<float>(shape, 0x1c) + r;
    if (length_squared > reach * reach) return 0;
    store_normal(out, d, length_squared);
    const Lanes n = lanes(out + 4);
    alignas(16) float center[4];
    std::memcpy(center, shape + 0x10, 16);
    std::memcpy(out, add(lanes(center), mul(n, at<float>(shape, 0x1c))).v, 16);
    return 1;
}

// Capsule shape (ends a +0x30, b +0x40, radius +0x50). The title compares squared distances with
// R + r unsquared, and moves along the axis by its full length, not by the projection.
uint8_t XB_THISCALL PhysCapsule_SphereContact(uint8_t* shape, const float* p, float r, float* out, uint32_t) {
    const float* a = reinterpret_cast<const float*>(shape + 0x30);
    const Lanes la = lanes(a);
    const Lanes axis = sub(lanes(reinterpret_cast<const float*>(shape + 0x40)), la);
    const float epsilon = title_global<float>(0x0083ed50);  // 1e-4
    const float reach = r + at<float>(shape, 0x50);
    const float axis_squared = dot3(axis, axis);
    alignas(16) float closest[4];
    if (epsilon > axis_squared) {
        const Lanes d = sub(lanes(p), la);
        const float length_squared = dot3(d, d);
        if (length_squared > reach) return 0;
        store_normal(out, d, length_squared);
        const Lanes n = lanes(out + 4);
        std::memcpy(closest, a, 16);
        std::memcpy(out, add(lanes(closest), mul(n, at<float>(shape, 0x50))).v, 16);
        return 1;
    }
    const Lanes q = lanes(p);
    const float axis_length = static_cast<float>(platform_x87_sqrt_ext(axis_squared));
    const Lanes unit = div(axis, splat(axis_length));
    const float t = dot3(sub(q, la), unit);
    Lanes base;
    if (t > title_global<float>(0x0083eafc)) {  // 0
        if (t > axis_length) {
            base = lanes(reinterpret_cast<const float*>(shape + 0x40));
        } else {
            std::memcpy(closest, a, 16);
            base = add(lanes(closest), mul(unit, axis_length));
        }
    } else {
        base = lanes(a);
    }
    const Lanes d = sub(q, base);
    const float length_squared = dot3(d, d);
    if (length_squared > reach) return 0;
    store_normal(out, d, length_squared);
    const Lanes n = lanes(out + 4);
    std::memcpy(out, add(base, mul(n, at<float>(shape, 0x50))).v, 16);
    return 1;
}

// Box (centre +0x10, half extents +0x20) against the segment a-b: the bounds must overlap on all
// three axes, then a slab test along dir from a. Only AL leaves the function (the hit point is a
// local). The entering-face test is >= on the positive side, > on the negative one, as in the title.
uint8_t XB_THISCALL PhysBox_IntersectSegment(uint8_t* shape, const float* a, const float* b, const float* dir, uint32_t) {
    const Lanes extent = lanes(reinterpret_cast<const float*>(shape + 0x20));
    const Lanes center = lanes(reinterpret_cast<const float*>(shape + 0x10));
    const Lanes lo = sub(center, extent);
    const Lanes hi = add(center, extent);
    const Lanes pa = lanes(a), pb = lanes(b);
    Lanes low, high, gap;
    for (int32_t i = 0; i < 4; ++i) {
        low.v[i] = pa.v[i] < pb.v[i] ? pa.v[i] : pb.v[i];   // MINPS
        high.v[i] = pa.v[i] > pb.v[i] ? pa.v[i] : pb.v[i];  // MAXPS
    }
    const Lanes below = sub(lo, high), above = sub(low, hi);
    int32_t mask = 0;
    for (int32_t i = 0; i < 4; ++i) {
        gap.v[i] = below.v[i] > above.v[i] ? below.v[i] : above.v[i];  // MAXPS
        if (gap.v[i] < 0.0f) mask |= 1 << i;                             // CMPLTPS
    }
    if ((mask & 7) != 7) return 0;
    float hit[4];
    for (int32_t i = 0; i < 3; ++i) {
        const int32_t j = (i + 1) % 3, k = (i + 2) % 3;
        const float d = dir[i];
        if (d > 0.0f) {
            if (!(lo.v[i] >= a[i])) continue;  // JB: NaN continues too
            const float t = (lo.v[i] - a[i]) / d;
            const float y = dir[j] * t + a[j];
            hit[j] = y;
            if (!(y >= lo.v[j]) || !(hi.v[j] >= y)) continue;
            const float z = dir[k] * t + a[k];
            hit[k] = z;
            if (!(z >= lo.v[k])) continue;
            if (hi.v[k] >= z) return 1;
        } else {
            if (!(0.0f > d)) continue;
            if (!(a[i] >= hi.v[i])) continue;
            const float t = (hi.v[i] - a[i]) / d;
            const float y = dir[j] * t + a[j];
            hit[j] = y;
            if (!(y > lo.v[j]) || !(hi.v[j] > y)) continue;
            const float z = dir[k] * t + a[k];
            hit[k] = z;
            if (!(z >= lo.v[k]) || !(hi.v[k] >= z)) continue;
            hit[i] = hi.v[i];
            platform_keep(hit);
            return 1;
        }
    }
    return 0;
}

// Axis-aligned box shape (0x30 bytes): vtable, three -1 words (+4..+0xc), centre +0x10 with the
// bounding radius in its w (+0x1c), half extents +0x20.
uint8_t* XB_THISCALL PhysBox_Construct(uint8_t* box, const float* center, const float* extent) {
    at<uint32_t>(box, 0) = 0x0083263c;  // PhysShape vtable
    at<uint32_t>(box, 0x10) = title_global<uint32_t>(0x007cc650);
    at<uint32_t>(box, 0x14) = title_global<uint32_t>(0x007cc654);
    at<uint32_t>(box, 0x18) = title_global<uint32_t>(0x007cc658);
    at<float>(box, 0x1c) = 0.0f;
    at<int32_t>(box, 4) = -1;
    at<int32_t>(box, 8) = -1;
    at<int32_t>(box, 0xc) = -1;
    at<uint32_t>(box, 0) = 0x007cc6c0;  // PhysBox vtable
    std::memcpy(box + 0x10, center, 12);
    const Lanes e = lanes(extent);
    const Lanes sq = mul(e, e);
    const float length_squared = (sq.v[0] + sq.v[1]) + sq.v[2];
    float radius = length_squared;
    // UCOMISS + JNE: NaN takes the zero path too.
    if (!(length_squared == 0.0f || length_squared != length_squared)) {
        // Newton step on the RSQRTSS estimate: ((3 - x r) x) 0.5, x = l r.
        const float r = platform_rsqrt_estimate(length_squared);
        const float x = length_squared * r;
        radius = ((title_global<float>(0x007cc4b0) - x * r) * x) * title_global<float>(0x007cc4c0);  // 3, 0.5
    }
    at<float>(box, 0x1c) = radius;
    std::memcpy(box + 0x20, extent, 16);
    return box;
}

// Squared distance from p to the box (p clamped: MAXPS against the low corner, then MINPS).
float XB_CDECL PhysBox_PointDistanceSq(const float* p, const uint8_t* box) {
    const Lanes e = lanes(reinterpret_cast<const float*>(box + 0x20));
    const Lanes c = lanes(reinterpret_cast<const float*>(box + 0x10));
    const Lanes lo = sub(c, e);
    const Lanes hi = add(c, e);
    const Lanes q = lanes(p);
    Lanes clamped;
    for (int32_t i = 0; i < 4; ++i) {
        const float m = lo.v[i] > q.v[i] ? lo.v[i] : q.v[i];  // MAXPS lo, p
        clamped.v[i] = hi.v[i] < m ? hi.v[i] : m;              // MINPS hi, m
    }
    const Lanes d = sub(clamped, q);
    const Lanes sq = mul(d, d);
    return (sq.v[0] + sq.v[1]) + sq.v[2];
}

namespace {

// p moved into the frame (rows +0, +0x10, +0x20, origin +0x30).
Lanes point_into(const float* frame, const float* p) {
    Lanes columns[3];
    frame_columns(frame, columns);
    return rotate_into(columns, sub(lanes(p), lanes(frame + 12)));
}

// CMPLTPS mask of lanes 0-2 all set; lane 3 is compared too (a QNaN there raises MXCSR IE).
bool all_less3(const Lanes& a, const Lanes& b) {
    const bool x = a.v[0] < b.v[0];
    const bool y = a.v[1] < b.v[1];
    const bool z = a.v[2] < b.v[2];
    const bool w = a.v[3] < b.v[3];
    platform_keep(w);
    return x && y && z;
}

}  // namespace

// Is p (moved into the frame) strictly inside the box?
uint8_t XB_CDECL PhysBox_ContainsPointInFrame(const uint8_t* box, const float* frame, const float* p) {
    const Lanes local = point_into(frame, p);
    const Lanes c = lanes(reinterpret_cast<const float*>(box + 0x10));
    const Lanes e = lanes(reinterpret_cast<const float*>(box + 0x20));
    const Lanes hi = add(c, e);
    platform_keep(hi);
    if (!all_less3(local, hi)) return 0;
    const Lanes lo = sub(c, e);
    platform_keep(lo);
    return all_less3(lo, local) ? 1 : 0;
}

// Does the sphere (centre +0x10, radius +0x1c) moved into the frame overlap the box? The title
// widens the box by the squared radius.
uint8_t XB_CDECL PhysBox_OverlapsSphereInFrame(const uint8_t* box, const float* frame, const uint8_t* sphere) {
    const Lanes local = point_into(frame, reinterpret_cast<const float*>(sphere + 0x10));
    const float radius = at<float>(sphere, 0x1c);
    const float r2 = radius * radius;
    const Lanes c = lanes(reinterpret_cast<const float*>(box + 0x10));
    const Lanes e = lanes(reinterpret_cast<const float*>(box + 0x20));
    const Lanes far = add(local, splat(r2));
    const Lanes lo = sub(c, e);
    platform_keep(far);
    platform_keep(lo);
    if (!all_less3(lo, far)) return 0;
    const Lanes hi = add(c, e);
    const Lanes near = sub(local, splat(r2));
    platform_keep(hi);
    platform_keep(near);
    return all_less3(near, hi) ? 1 : 0;
}

float XB_CDECL PhysBox_PointDistanceSqInFrame(const float* p, const uint8_t* box, const float* frame) {
    alignas(16) float local[4];
    std::memcpy(local, point_into(frame, p).v, 16);
    return PhysBox_PointDistanceSq(local, box);
}

// GJK closest points of two shapes (out: point a +0, point b +0x10, separation +0x20); on a hit
// with a separation longer than the threshold (0x008bd434) the separation is normalized and the
// points are pulled 0.03 inward along it. Returns the GJK result (1), 2 for a too-short
// separation, or GJK's other results as they are.
int32_t XB_CDECL Phys_GjkContact(uint32_t a, uint32_t b, uint32_t c, float* out) {
    using Closest = int32_t XB_CDECL(uint32_t, uint32_t, uint32_t, float*, uint32_t, uint32_t, uint32_t, uint32_t,
                                     uint32_t, uint32_t);
    const int32_t result = title_function<Closest>(0x000b1b40)(a, b, c, out, title_global<uint32_t>(0x008bd430), 0, 0,
                                                                  0, 0, 0);  // Gjk_ClosestPoints
    if (result != 1) return result;
    const Lanes sq = mul(lanes(out + 8), lanes(out + 8));
    const float length_squared = (sq.v[0] + sq.v[1]) + sq.v[2];
    if (!(length_squared > title_global<float>(0x008bd434))) return 2;
    const float length = static_cast<float>(platform_x87_sqrt_ext(length_squared));
    std::memcpy(out + 8, div(lanes(out + 8), splat(length)).v, 16);
    const Lanes s = mul(lanes(out + 8), title_global<float>(0x007cce08));  // 0.03
    std::memcpy(out, sub(lanes(out), s).v, 16);
    alignas(16) float point[4];
    std::memcpy(point, out + 4, 16);
    std::memcpy(out + 4, add(lanes(point), s).v, 16);
    return 1;
}

namespace {

// Penetration depth of a face hit: the distance to the nearer face along j, replaced by the k
// distance squared when that one is smaller (the title squares only that one).
void box_hit_depth(const uint8_t* box, const Lanes& lo, const Lanes& hi, const float* out, int32_t j, int32_t k,
                   float* depth) {
    const float* const c = reinterpret_cast<const float*>(box + 0x10);
    float m;
    if (out[j] > c[j])
        m = hi.v[j] - out[j];
    else
        m = out[j] - lo.v[j];
    *depth = m;
    float n;
    if (out[k] > c[k])
        n = hi.v[k] - out[k];
    else
        n = out[k] - lo.v[k];
    if (m > n) *depth = n * n;
}

}  // namespace

// Box (centre +0x10, half extents +0x20) against the ray a-b: bounds overlap on all three axes,
// then a slab test per axis (the low face entered from below, the high face from above). On a
// hit out+0 = the point, out+0x10 = the face normal, and *depth (when given) = see box_hit_depth.
uint8_t XB_THISCALL PhysBox_IntersectRay(uint8_t* box, const float* a, const float* b, float* out, uint32_t,
                                         float* depth) {
    const Lanes extent = lanes(reinterpret_cast<const float*>(box + 0x20));
    const Lanes center = lanes(reinterpret_cast<const float*>(box + 0x10));
    const Lanes lo = sub(center, extent);
    const Lanes hi = add(center, extent);
    const Lanes pa = lanes(a), pb = lanes(b);
    Lanes low, high;
    for (int32_t i = 0; i < 4; ++i) {
        low.v[i] = pa.v[i] < pb.v[i] ? pa.v[i] : pb.v[i];   // MINPS
        high.v[i] = pa.v[i] > pb.v[i] ? pa.v[i] : pb.v[i];  // MAXPS
    }
    const Lanes below = sub(lo, high), above = sub(low, hi);
    int32_t mask = 0;
    for (int32_t i = 0; i < 4; ++i) {
        const float gap = below.v[i] > above.v[i] ? below.v[i] : above.v[i];  // MAXPS
        if (gap < 0.0f) mask |= 1 << i;                                         // CMPLTPS
    }
    if ((mask & 7) != 7) return 0;
    const Lanes d = sub(pb, pa);
    platform_keep(d);
    for (int32_t i = 0; i < 3; ++i) {
        const int32_t j = (i + 1) % 3, k = (i + 2) % 3;
        if (lo.v[i] >= a[i] && b[i] >= lo.v[i]) {
            const float t = (lo.v[i] - a[i]) / d.v[i];
            const float y = d.v[j] * t + a[j];
            out[j] = y;
            if (!(y >= lo.v[j]) || !(hi.v[j] >= y)) continue;
            const float z = d.v[k] * t + a[k];
            out[k] = z;
            if (!(z >= lo.v[k]) || !(hi.v[k] >= z)) continue;
            out[i] = lo.v[i];
            out[4 + i] = title_global<float>(0x0083eb34);  // -1
            out[4 + k] = 0.0f;
            out[4 + j] = 0.0f;
            if (depth) box_hit_depth(box, lo, hi, out, j, k, depth);
            return 1;
        }
        if (!(a[i] >= hi.v[i]) || !(hi.v[i] >= b[i])) continue;
        const float t = (hi.v[i] - a[i]) / d.v[i];
        const float y = d.v[j] * t + a[j];
        out[j] = y;
        if (!(y > lo.v[j]) || !(hi.v[j] > y)) continue;
        const float z = d.v[k] * t + a[k];
        out[k] = z;
        if (!(z >= lo.v[k]) || !(hi.v[k] >= z)) continue;
        out[i] = hi.v[i];
        out[4 + i] = title_global<float>(0x0083eb20);  // 1
        out[4 + k] = 0.0f;
        out[4 + j] = 0.0f;
        if (depth) box_hit_depth(box, lo, hi, out, j, k, depth);
        return 1;
    }
    return 0;
}

// Oriented box (frame +0x30, half extents +0x70) against the ray a-b: the ray moved into the box
// frame, tested against an axis-aligned box at the origin, the hit moved back out. The local hit
// block starts as the copy of the frame's rows (as in the title), so a miss still writes
// out+0x10 = the frame's second row rotated out.
uint8_t XB_THISCALL PhysOrientedBox_IntersectRay(uint8_t* shape, const float* a, const float* b, float* out,
                                                 uint32_t arg4, float* depth) {
    const float* const frame = reinterpret_cast<const float*>(shape + 0x30);
    alignas(16) float local_a[4];
    alignas(16) float local_b[4];
    std::memcpy(local_a, point_into(frame, a).v, 16);
    std::memcpy(local_b, point_into(frame, b).v, 16);
    alignas(16) float hit[12];
    std::memcpy(hit, frame, sizeof(hit));
    alignas(16) const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    alignas(16) uint8_t box[0x30];
    PhysBox_Construct(box, zero, reinterpret_cast<const float*>(shape + 0x70));
    const uint8_t result = PhysBox_IntersectRay(box, local_a, local_b, hit, arg4, depth);
    if (result) std::memcpy(out, transform_out(frame, hit).v, 16);
    std::memcpy(out + 4, rotate_out(frame, hit + 4).v, 16);
    return result ? 1 : 0;
}

// Oriented box against the segment a-b (direction dir), in the box frame.
uint8_t XB_THISCALL PhysOrientedBox_IntersectSegment(uint8_t* shape, const float* a, const float* b, const float* dir,
                                                     uint32_t arg4) {
    const float* const frame = reinterpret_cast<const float*>(shape + 0x30);
    alignas(16) float local_a[4];
    alignas(16) float local_b[4];
    alignas(16) float local_dir[4];
    std::memcpy(local_a, point_into(frame, a).v, 16);
    std::memcpy(local_b, point_into(frame, b).v, 16);
    Lanes columns[3];
    frame_columns(frame, columns);
    std::memcpy(local_dir, rotate_into(columns, lanes(dir)).v, 16);
    alignas(16) const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    alignas(16) uint8_t box[0x30];
    PhysBox_Construct(box, zero, reinterpret_cast<const float*>(shape + 0x70));
    return PhysBox_IntersectSegment(box, local_a, local_b, local_dir, arg4);
}

// Contact of a sphere shape (centre +0x10, radius +0x1c) placed in a frame with another shape.
// The other shape's sphere test (vtable +0x10) gives a first normal n (out+0x20); then the sphere
// is moved out along n (frame origin += n, up to six steps) until GJK reports separation, the GJK
// separation is normalized, and the search restarts from the new direction while it keeps turning
// (dot > 0.998 or no better than the last by 0.005 ends it). The points are pulled 0.03 inward and
// the first one moved back by the total offset.
uint8_t XB_CDECL Phys_SphereShapeContact(const uint8_t* sphere, const float* frame, uint8_t* shape, float* out) {
    alignas(16) float moved_frame[16];
    std::memcpy(moved_frame, frame, sizeof(moved_frame));
    const float radius = at<float>(sphere, 0x1c);
    alignas(16) float center[4];
    std::memcpy(center, sphere + 0x10, 16);
    alignas(16) float p[4];
    std::memcpy(p, transform_out(frame, center).v, 16);
    alignas(16) float hit[8] = {};  // the normal half is never written on a miss (zero dead stack)
    int32_t part = -1;
    using SphereContact = uint8_t XB_THISCALL(void*, const float*, float, float*, int32_t*);
    void* const* vtable = *reinterpret_cast<void* const* const*>(shape);
    const uint8_t touching =
        title_function<SphereContact>(reinterpret_cast<uintptr_t>(vtable[4]))(shape, p, radius, hit, &part);
    std::memcpy(out + 8, hit + 4, 16);
    if (!touching) return 0;
    using Closest = int32_t XB_CDECL(const uint8_t*, const float*, uint8_t*, float*, uint32_t, uint32_t, uint32_t,
                                     uint32_t, uint32_t, uint32_t);
    float best = title_global<float>(0x007cce18);  // -1e10
    alignas(16) float n[4];
    std::memcpy(n, hit + 4, 16);
    alignas(16) float moved[4];
    for (;;) {
        uint32_t steps = 0;
        float length_squared;
        for (;;) {
            std::memcpy(moved, add(lanes(moved_frame + 12), lanes(n)).v, 16);
            std::memcpy(moved_frame + 12, moved, 16);
            const int32_t separated = title_function<Closest>(0x000b1b40)(sphere, moved_frame, shape, out,
                                                                           title_global<uint32_t>(0x008bd430), 1, 0, 0,
                                                                           0, 0);  // Gjk_ClosestPoints
            const Lanes sq = mul(lanes(out + 8), lanes(out + 8));
            length_squared = (sq.v[0] + sq.v[1]) + sq.v[2];
            if (!separated) break;
            if (++steps > 5) return 0;
        }
        long double limit = static_cast<long double>(steps) + title_global<float>(0x0083f278);  // 1.1
        limit = limit * limit;
        if (static_cast<long double>(length_squared) > limit) return 0;
        const float length = static_cast<float>(platform_x87_sqrt_ext(length_squared));
        std::memcpy(out + 8, div(lanes(out + 8), splat(length)).v, 16);
        const Lanes turn = mul(lanes(out + 8), lanes(n));
        const float d = (turn.v[0] + turn.v[1]) + turn.v[2];
        if (d > title_global<float>(0x007cce14)) break;                // 0.998
        if (best > d - title_global<float>(0x0083eb88)) break;         // 0.005
        std::memcpy(n, out + 8, 16);
        std::memcpy(moved_frame + 12, frame + 12, 16);
        best = d;
    }
    const Lanes s = mul(lanes(out + 8), title_global<float>(0x007cce08));  // 0.03
    const Lanes delta = sub(sub(lanes(frame + 12), lanes(moved)), s);
    std::memcpy(out, add(lanes(out), delta).v, 16);
    alignas(16) float second[4];
    std::memcpy(second, out + 4, 16);
    std::memcpy(out + 4, add(lanes(second), s).v, 16);
    return 1;
}

// The physics world (phys_world, 0x0090a87c): parameters +0xc..+0x2c, static body +0x40, pools of
// 0x1c0 objects (+0x1d8, used +0x1e4), bodies (+0x1ec, size +0x1f4, used +0x1f8), 0x50 constraints
// (pool +0x228), constraints (+0x254, size +0x25c, used +0x260), 0x10 links (+0x268, size +0x270,
// used +0x274), 0x28c constraints (pool +0x28c) and 0xd0 constraints (pool +0x2a0).

namespace {

uint8_t* phys_world() { return title_global<uint8_t*>(0x0090a87c); }

// Next free entry of a world list (pointer list, size, used count), or null when all are used.
uint8_t* world_list_alloc(uint32_t list, uint32_t size, uint32_t used) {
    uint8_t* const world = phys_world();
    const int32_t count = at<int32_t>(world, used);
    if (count >= at<int32_t>(world, size)) return nullptr;
    uint8_t* const entry = at<uint8_t**>(world, list)[count];
    at<int32_t>(world, used) = count + 1;
    return entry;
}

}  // namespace

uint8_t* XB_THISCALL PhysWorldParams_Init(uint8_t* params) {
    for (uint32_t i = 0; i < 0x30; i += 4) at<uint32_t>(params, i) = 0;
    return params;
}

uint8_t* XB_CDECL PhysWorld_GetStaticBody() { return phys_world() + 0x40; }

void XB_CDECL PhysWorld_SetParams14(uint32_t a, uint32_t b, uint32_t c) {
    at<uint32_t>(phys_world(), 0x14) = a;
    at<uint32_t>(phys_world(), 0x18) = b;
    at<uint32_t>(phys_world(), 0x1c) = c;
}

void XB_CDECL PhysWorld_SetParams20(uint32_t a, uint32_t b, uint32_t c) {
    at<uint32_t>(phys_world(), 0x20) = a;
    at<uint32_t>(phys_world(), 0x24) = b;
    at<uint32_t>(phys_world(), 0x28) = c;
}

void XB_CDECL PhysWorld_SetField2C(uint32_t value) { at<uint32_t>(phys_world(), 0x2c) = value; }

void XB_CDECL PhysWorld_SetCallbackC(uint32_t callback) { at<uint32_t>(phys_world(), 0xc) = callback; }

void XB_CDECL Phys_RunFrame(float dt) {
    title_function<void XB_THISCALL(uint8_t*, float)>(0x000be640)(phys_world(), dt);  // PhysWorld_Step
}

// A full list writes a, b through null (the title faults there).
uint32_t* XB_CDECL PhysWorld_AllocLink10(uint32_t a, uint32_t b) {
    uint32_t* const link = reinterpret_cast<uint32_t*>(world_list_alloc(0x268, 0x270, 0x274));
    link[0] = a;
    link[1] = b;
    return link;
}

uint8_t* XB_CDECL PhysWorld_FindObject1c0ByOwner(uint32_t owner) {
    uint8_t* const world = phys_world();
    uint8_t** it = at<uint8_t**>(world, 0x1d8);
    uint8_t** const end = it + at<int32_t>(world, 0x1e4);
    for (; it != end; ++it)
        if (at<uint32_t>(*it, 0x1b0) == owner) return *it;
    return nullptr;
}

uint8_t* XB_CDECL PhysWorld_AllocBody() { return world_list_alloc(0x1ec, 0x1f4, 0x1f8); }

uint32_t XB_CDECL PhysWorld_AllocConstraint50(uint32_t a, uint32_t b) {
    using Alloc = uint32_t XB_CDECL(uint8_t*, uint32_t, uint32_t, uint32_t);
    return title_function<Alloc>(0x000b5fe0)(phys_world() + 0x228, 0, a, b);  // PhysConstraintPool_Alloc
}

// A new constraint gets -1 in +0x48, +0x50 and +0x58; a full list writes a, b through null.
uint32_t* XB_CDECL PhysWorld_AllocConstraint(uint32_t a, uint32_t b) {
    uint8_t* const constraint = world_list_alloc(0x254, 0x25c, 0x260);
    if (constraint) {
        at<int32_t>(constraint, 0x48) = -1;
        at<int32_t>(constraint, 0x50) = -1;
        at<int32_t>(constraint, 0x58) = -1;
    }
    uint32_t* const ends = reinterpret_cast<uint32_t*>(constraint);
    ends[0] = a;
    ends[1] = b;
    return ends;
}

uint32_t XB_CDECL PhysWorld_AllocConstraint28c(uint32_t a, uint32_t b) {
    using Alloc = uint32_t XB_CDECL(uint8_t*, uint32_t, uint32_t, uint32_t);
    return title_function<Alloc>(0x000b6040)(phys_world() + 0x28c, 0, a, b);  // PhysConstraintPool28c_Alloc
}

void XB_CDECL PhysWorld_FreeConstraintD0(uint32_t constraint) {
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(0x000b5f10)(phys_world() + 0x2a0, constraint);  // PhysConstraintPoolD0_Free
}

uint8_t* XB_CDECL PhysWorld_AllocObject1c0() { return world_list_alloc(0x1d8, 0x1e0, 0x1e4); }

// A full pool writes a, b through null (the title faults there).
uint32_t* XB_CDECL PhysWorld_AllocConstraint1B0(uint32_t a, uint32_t b) {
    uint8_t* const constraint = world_list_alloc(0x27c, 0x284, 0x288);
    if (constraint) PhysConstraint1B0_Init(constraint);
    uint32_t* const ends = reinterpret_cast<uint32_t*>(constraint);
    ends[0] = a;
    ends[1] = b;
    return ends;
}

// The callee leaves words in the dead stack below the call (its saved registers and locals), so
// it runs at the original's ESP with the original's registers (gap entry).
PLATFORM_ASM_ENTRY(PhysWorld_GetBodyPair_Wrapper_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebx\n"
                   "pushl 8(%edx)\n"
                   "pushl 4(%edx)\n" PLATFORM_ASM_CALL(PhysWorld_GetBodyPair_Wrapper) "addl $24, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL PhysWorld_GetBodyPair_Wrapper(uint32_t a, uint32_t b, uint32_t entry_ebx, uint32_t entry_esi,
                                                uint32_t entry_edi, uint32_t entry_esp) {
    const uint32_t pool = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(phys_world() + 0x1fc));
    TitleRegs regs{};
    regs.eax = b;
    regs.ecx = a;
    regs.edx = pool;
    regs.ebx = entry_ebx;
    regs.esi = entry_esi;
    regs.edi = entry_edi;
    const uint32_t args[4] = {pool, 0, a, b};
    return title_call_at_regs(entry_esp, 0x000b62d0, &regs, 4, args);  // PhysWorld_GetBodyPair
}

namespace {

// A constraint (bodies at +0 and +4) links the body when either non-null end is it.
bool constraint_links(const uint32_t* constraint, uint32_t body) {
    if (constraint[0] && constraint[0] == body) return true;
    return constraint[1] && constraint[1] == body;
}

// Frees (thiscall pool free) every constraint of a world pool that links the body; a freed entry's
// slot is taken by the last one, so the same index is checked again.
void world_pool_remove_body(uint32_t pool, uint32_t free_function, uint32_t body) {
    int32_t i = 0;
    while (i < at<int32_t>(phys_world(), pool + 0x10)) {
        uint32_t* const constraint = at<uint32_t**>(phys_world(), pool + 4)[i];
        if (constraint_links(constraint, body))
            title_function<void XB_THISCALL(uint8_t*, uint32_t*)>(free_function)(phys_world() + pool, constraint);
        else
            ++i;
    }
}

// World pools: storage +0, pointer list +4, list positions +8, size +0xc, used +0x10.
int32_t world_pool_reset(uint8_t* pool, uint32_t size) {
    int32_t i = 0;
    uint32_t offset = 0;
    for (; i < at<int32_t>(pool, 0xc); ++i, offset += size) {
        at<int32_t*>(pool, 8)[i] = i;
        at<uint32_t*>(pool, 4)[i] = at<uint32_t>(pool, 0) + offset;
    }
    at<uint32_t>(pool, 0x10) = 0;
    return i;
}

}  // namespace

void XB_CDECL PhysWorld_RemoveBodyConstraints(uint32_t body) {
    world_pool_remove_body(0x1fc, 0x000b5c00, body);  // PhysConstraintPool30_Free
    world_pool_remove_body(0x228, 0x000b5c90, body);  // PhysConstraintPool50_Free
    world_pool_remove_body(0x23c, 0x000b5d10, body);  // PhysConstraintPoolF0_Free
    world_pool_remove_body(0x250, 0x000b5da0, body);  // PhysConstraintPool60_Free
    // The 0x10 link pool's free, inlined (element index by SAR 4).
    int32_t i = 0;
    while (i < at<int32_t>(phys_world(), 0x274)) {
        uint8_t* const pool = phys_world() + 0x264;
        const uint32_t link = at<uint32_t*>(pool, 4)[i];
        if (!constraint_links(reinterpret_cast<const uint32_t*>(link), body)) {
            ++i;
            continue;
        }
        const uint32_t storage = at<uint32_t>(pool, 0);
        const int32_t index = static_cast<int32_t>(link - storage) >> 4;
        const int32_t slot = at<int32_t*>(pool, 8)[index];
        int32_t count = at<int32_t>(pool, 0x10);
        if (count == 1) {
            world_pool_reset(pool, 0x10);
            continue;
        }
        --count;
        at<int32_t>(pool, 0x10) = count;
        const uint32_t last = at<uint32_t*>(pool, 4)[count];
        const int32_t last_index = static_cast<int32_t>(last - storage) >> 4;
        const uint32_t freed = at<uint32_t*>(pool, 4)[slot];
        at<uint32_t*>(pool, 4)[slot] = last;
        at<uint32_t*>(pool, 4)[at<int32_t>(pool, 0x10)] = freed;
        at<int32_t*>(pool, 8)[index] = at<int32_t>(pool, 0x10);
        at<int32_t*>(pool, 8)[last_index] = slot;
    }
    world_pool_remove_body(0x278, 0x000b6250, body);  // PhysConstraintPool1B0_Free
    world_pool_remove_body(0x28c, 0x000b5e80, body);  // PhysConstraintPoolE0_Free
    world_pool_remove_body(0x2a0, 0x000b5f10, body);  // PhysConstraintPoolD0_Free
}

void XB_CDECL PhysWorld_DestroyBody(uint32_t body) {
    PhysWorld_RemoveBodyConstraints(body);
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(0x000b5b70)(phys_world() + 0x1e8, body);  // PhysBodyPool_Free
}

void XB_CDECL PhysWorld_DestroyObject1c0(uint32_t object) {
    PhysWorld_RemoveBodyConstraints(object);
    title_function<void XB_THISCALL(uint8_t*, uint32_t)>(0x000b5ae0)(phys_world() + 0x1d4, object);  // PhysPool1c0_Free
}

int32_t XB_THISCALL PhysPool1c0_Reset(uint8_t* pool) { return world_pool_reset(pool, 0x1c0); }
int32_t XB_THISCALL PhysBodyPool_Reset(uint8_t* pool) { return world_pool_reset(pool, 0x170); }

// Clears each used constraint's back-pointer (+0x2c points at a word set to 0) first.
int32_t XB_THISCALL PhysConstraintPool30_Reset(uint8_t* pool) {
    for (int32_t i = 0; i < at<int32_t>(pool, 0x10); ++i) {
        uint32_t* const back = at<uint32_t*>(at<uint8_t**>(pool, 4)[i], 0x2c);
        if (back) *back = 0;
    }
    return world_pool_reset(pool, 0x30);
}

int32_t XB_THISCALL PhysConstraintPool50_Reset(uint8_t* pool) { return world_pool_reset(pool, 0x50); }
int32_t XB_THISCALL PhysConstraintPoolF0_Reset(uint8_t* pool) { return world_pool_reset(pool, 0xf0); }
int32_t XB_THISCALL PhysConstraintPool60_Reset(uint8_t* pool) { return world_pool_reset(pool, 0x60); }
int32_t XB_THISCALL PhysPool10_Reset(uint8_t* pool) { return world_pool_reset(pool, 0x10); }
int32_t XB_THISCALL PhysConstraintPoolE0_Reset(uint8_t* pool) { return world_pool_reset(pool, 0xe0); }
int32_t XB_THISCALL PhysConstraintPoolD0_Reset(uint8_t* pool) { return world_pool_reset(pool, 0xd0); }

namespace {

int32_t negate(int32_t v) { return static_cast<int32_t>(0u - static_cast<uint32_t>(v)); }
int32_t positive_part(int32_t v) { return v > 0 ? v : 0; }

}  // namespace

// The old root's balance gains max(-left.balance, 0) + 1, then the left child's gains
// max(root.balance, 0) + 1 (sums wrap like the title's ADD).
uint8_t* XB_STDCALL PhysPairTree_RotateRight(uint8_t** root) {
    uint8_t* const left = at<uint8_t*>(*root, 0);
    at<uint8_t*>(*root, 0) = at<uint8_t*>(left, 4);
    at<uint8_t*>(left, 4) = *root;
    at<uint32_t>(*root, 8) += static_cast<uint32_t>(positive_part(negate(at<int32_t>(left, 8)))) + 1u;
    at<uint32_t>(left, 8) += static_cast<uint32_t>(positive_part(at<int32_t>(*root, 8))) + 1u;
    *root = left;
    return left;
}

// Mirror: the old root's balance loses max(right.balance, 0) + 1, then the right child's loses
// max(-root.balance, 0) + 1.
uint8_t* XB_STDCALL PhysPairTree_RotateLeft(uint8_t** root) {
    uint8_t* const right = at<uint8_t*>(*root, 4);
    at<uint8_t*>(*root, 4) = at<uint8_t*>(right, 0);
    at<uint8_t*>(right, 0) = *root;
    at<uint32_t>(*root, 8) += ~static_cast<uint32_t>(positive_part(at<int32_t>(right, 8)));
    at<uint32_t>(right, 8) += ~static_cast<uint32_t>(positive_part(negate(at<int32_t>(*root, 8))));
    *root = right;
    return right;
}

uint8_t* XB_THISCALL PhysConstraint1B0_Init(uint8_t* constraint) {
    for (uint32_t i = 0; i < 12; ++i) at<int32_t>(constraint, 0x34 + i * 8) = -1;
    uint8_t* const list = constraint + 0x110;
    at<uint8_t*>(list, 0x80) = list;
    at<uint32_t>(list, 0x84) = 0;
    at<uint32_t>(constraint, 0x30) = 0;
    return constraint;
}

uint8_t XB_THISCALL PhysOrientedBox_SphereContact(uint8_t* shape, const float* centre, uint32_t radius, float* out,
                                                  uint32_t arg4) {
    const float* const frame = reinterpret_cast<const float*>(shape + 0x30);
    alignas(16) float local[4];
    std::memcpy(local, point_into(frame, centre).v, 16);
    // The hit buffer is the frame copy the transpose read: a miss leaves rows 1-2 as the normal.
    alignas(16) float hit[12];
    std::memcpy(hit, frame, sizeof(hit));
    alignas(16) const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    alignas(16) uint8_t box[0x30];
    PhysBox_Construct(box, zero, reinterpret_cast<const float*>(shape + 0x70));
    using Contact = uint8_t XB_THISCALL(uint8_t*, const float*, uint32_t, float*, uint32_t);
    const uint8_t result = title_function<Contact>(0x000ae970)(box, local, radius, hit, arg4);  // PhysBox_SphereContact
    if (result) std::memcpy(out, transform_out(frame, hit).v, 16);
    std::memcpy(out + 4, rotate_out(frame, hit + 4).v, 16);
    return result ? 1 : 0;
}

uint8_t XB_THISCALL PhysOrientedBox_SweepSphere(uint8_t* shape, const float* a, float* b, uint32_t radius,
                                                const float* dir) {
    const float* const frame = reinterpret_cast<const float*>(shape + 0x30);
    Lanes columns[3];
    frame_columns(frame, columns);
    alignas(16) float local_dir[4];
    std::memcpy(local_dir, rotate_into(columns, lanes(dir)).v, 16);
    alignas(16) float local_a[4];
    std::memcpy(local_a, point_into(frame, a).v, 16);
    // b in the box frame, followed by the frame copy's rows 1-2 (the callee's normal slot).
    alignas(16) float hit[12];
    std::memcpy(hit, frame, sizeof(hit));
    std::memcpy(hit, point_into(frame, b).v, 16);
    alignas(16) const float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    alignas(16) uint8_t box[0x30];
    PhysBox_Construct(box, zero, reinterpret_cast<const float*>(shape + 0x70));
    using Sweep = uint8_t XB_THISCALL(uint8_t*, const float*, float*, uint32_t, const float*);
    if (!title_function<Sweep>(0x000ad140)(box, local_a, hit, radius, local_dir)) return 0;  // PhysBox_SweepSphere
    std::memcpy(b, transform_out(frame, hit).v, 16);
    std::memcpy(b + 4, rotate_out(frame, hit + 4).v, 16);
    return 1;
}

// GJK simplex state (globals): support points gjk_y[4] (0x0090a790, vec4), dot products
// gjk_dp[4][4] (0x0090a810), sub-determinants gjk_det[16][4] (0x0090a640), the simplex's point bits
// (gjk_bits 0x0090a748), the new point's index and bit (gjk_last 0x0090a744, gjk_lastBit
// 0x0090a630) and their union (gjk_allBits 0x0090a740).
namespace {

float* gjk_dp(int32_t row) { return &title_global<float>(0x0090a810 + static_cast<uint32_t>(row) * 16); }
float* gjk_det(uint32_t set) { return &title_global<float>(0x0090a640 + set * 16); }

float gjk_dot(int32_t a, int32_t b) {
    const Lanes p = mul(lanes(&title_global<float>(0x0090a790 + static_cast<uint32_t>(a) * 16)),
                        lanes(&title_global<float>(0x0090a790 + static_cast<uint32_t>(b) * 16)));
    platform_keep(p);
    return (p.v[0] + p.v[1]) + p.v[2];
}

}  // namespace

// The new point's dot products with the simplex points, then the sub-determinants of every
// subset holding the new point (Johnson's distance algorithm); with all four points also the
// full set's.
void XB_CDECL Gjk_ComputeDeterminants() {
    const int32_t last = title_global<int32_t>(0x0090a744);
    for (int32_t i = 0, bit = 1; i < 4; ++i, bit <<= 1) {
        if (!(title_global<uint32_t>(0x0090a748) & static_cast<uint32_t>(bit))) continue;
        const float d = gjk_dot(i, last);
        gjk_dp(last)[i] = d;
        gjk_dp(i)[last] = d;
    }
    const uint32_t last_bit = title_global<uint32_t>(0x0090a630);
    const float last_last = gjk_dot(last, last);
    gjk_dp(last)[last] = last_last;
    gjk_det(last_bit)[last] = title_global<float>(0x0083eb20);  // 1
    for (int32_t j = 0; j < 4; ++j) {
        const uint32_t sj = 1u << j;
        if (!(title_global<uint32_t>(0x0090a748) & sj)) continue;
        const uint32_t s2 = title_global<uint32_t>(0x0090a630) | sj;
        gjk_det(s2)[j] = last_last - gjk_dp(last)[j];
        gjk_det(s2)[last] = gjk_dp(j)[j] - gjk_dp(j)[last];
        for (int32_t k = 0; k < j; ++k) {
            const uint32_t sk = 1u << k;
            if (!(title_global<uint32_t>(0x0090a748) & sk)) continue;
            const uint32_t s3 = sk | s2;
            gjk_det(s3)[k] = (gjk_dp(last)[j] - gjk_dp(last)[k]) * gjk_det(s2)[last] +
                             (gjk_dp(j)[j] - gjk_dp(j)[k]) * gjk_det(s2)[j];
            const uint32_t sk_last = title_global<uint32_t>(0x0090a630) | sk;
            gjk_det(s3)[j] = (gjk_dp(last)[k] - gjk_dp(last)[j]) * gjk_det(sk_last)[last] +
                             (gjk_dp(k)[k] - gjk_dp(k)[j]) * gjk_det(sk_last)[k];
            gjk_det(s3)[last] = (gjk_dp(k)[k] - gjk_dp(k)[last]) * gjk_det(sk | sj)[k] +
                                (gjk_dp(j)[k] - gjk_dp(j)[last]) * gjk_det(sk | sj)[j];
        }
    }
    if (title_global<int32_t>(0x0090a740) != 15) return;
    const float* const dp0 = gjk_dp(0);
    const float* const dp1 = gjk_dp(1);
    const float* const dp2 = gjk_dp(2);
    const float* const dp3 = gjk_dp(3);
    gjk_det(15)[0] = ((dp3[1] - dp3[0]) * gjk_det(14)[3] + (dp2[1] - dp2[0]) * gjk_det(14)[2]) +
                     (dp1[1] - dp1[0]) * gjk_det(14)[1];
    const float d1 = ((dp0[0] - dp0[1]) * gjk_det(13)[0] + (dp3[0] - dp3[1]) * gjk_det(13)[3]) +
                     (dp2[0] - dp2[1]) * gjk_det(13)[2];
    const float d2 = ((dp3[0] - dp3[2]) * gjk_det(11)[3] + (dp0[0] - dp0[2]) * gjk_det(11)[0]) +
                     (dp1[0] - dp1[2]) * gjk_det(11)[1];
    const float d3 = ((dp2[0] - dp2[3]) * gjk_det(7)[2] + (dp1[0] - dp1[3]) * gjk_det(7)[1]) +
                     (dp0[0] - dp0[3]) * gjk_det(7)[0];
    gjk_det(15)[1] = d1;
    gjk_det(15)[2] = d2;
    gjk_det(15)[3] = d3;
}

namespace {

// Frees a world pool element (element index by the signed division the title does).
void world_pool_free(uint8_t* pool, uint32_t element, int32_t size, int32_t XB_THISCALL (*reset)(uint8_t*),
                     bool clear_back) {
    const int32_t index = static_cast<int32_t>(element - at<uint32_t>(pool, 0)) / size;
    const int32_t slot = at<int32_t*>(pool, 8)[index];
    if (at<int32_t>(pool, 0x10) == 1) {
        reset(pool);
        return;
    }
    if (clear_back) {
        uint32_t* const back = at<uint32_t*>(reinterpret_cast<uint8_t*>(element), 0x2c);
        if (back) *back = 0;
    }
    const int32_t count = at<int32_t>(pool, 0x10) - 1;
    at<int32_t>(pool, 0x10) = count;
    const uint32_t last = at<uint32_t*>(pool, 4)[count];
    const int32_t last_index = static_cast<int32_t>(last - at<uint32_t>(pool, 0)) / size;
    const uint32_t freed = at<uint32_t*>(pool, 4)[slot];
    at<uint32_t*>(pool, 4)[slot] = last;
    at<uint32_t*>(pool, 4)[at<int32_t>(pool, 0x10)] = freed;
    at<int32_t*>(pool, 8)[index] = at<int32_t>(pool, 0x10);
    at<int32_t*>(pool, 8)[last_index] = slot;
}

}  // namespace

void XB_THISCALL PhysPool1c0_Free(uint8_t* pool, uint32_t element) {
    world_pool_free(pool, element, 0x1c0, PhysPool1c0_Reset, false);
}
void XB_THISCALL PhysBodyPool_Free(uint8_t* pool, uint32_t element) {
    world_pool_free(pool, element, 0x170, PhysBodyPool_Reset, false);
}
void XB_THISCALL PhysConstraintPool30_Free(uint8_t* pool, uint32_t element) {
    world_pool_free(pool, element, 0x30, PhysConstraintPool30_Reset, true);
}
void XB_THISCALL PhysConstraintPool50_Free(uint8_t* pool, uint32_t element) {
    world_pool_free(pool, element, 0x50, PhysConstraintPool50_Reset, false);
}
void XB_THISCALL PhysConstraintPoolF0_Free(uint8_t* pool, uint32_t element) {
    world_pool_free(pool, element, 0xf0, PhysConstraintPoolF0_Reset, false);
}
void XB_THISCALL PhysConstraintPool60_Free(uint8_t* pool, uint32_t element) {
    world_pool_free(pool, element, 0x60, PhysConstraintPool60_Reset, false);
}
void XB_THISCALL PhysConstraintPoolE0_Free(uint8_t* pool, uint32_t element) {
    world_pool_free(pool, element, 0xe0, PhysConstraintPoolE0_Reset, false);
}
void XB_THISCALL PhysConstraintPoolD0_Free(uint8_t* pool, uint32_t element) {
    world_pool_free(pool, element, 0xd0, PhysConstraintPoolD0_Reset, false);
}
void XB_THISCALL PhysConstraintPool1B0_Free(uint8_t* pool, uint32_t element) {
    world_pool_free(pool, element, 0x1b0, PhysConstraintPool1B0_Reset, false);
}

int32_t XB_THISCALL PhysConstraintPool1B0_Reset(uint8_t* pool) { return world_pool_reset(pool, 0x1b0); }

uint8_t* XB_THISCALL PhysPairTree_AllocNode(uint8_t* tree, const uint32_t* key) {
    uint8_t* node = nullptr;
    const int32_t count = at<int32_t>(tree, 8);
    if (count < at<int32_t>(tree, 4)) {
        node = at<uint8_t*>(tree, 0) + count * 0x18;
        at<int32_t>(tree, 8) = count + 1;
    }
    at<uint32_t>(node, 0) = 0;
    at<uint32_t>(node, 4) = 0;
    at<uint32_t>(node, 8) = 0;
    at<uint32_t>(node, 0xc) = key[0];
    at<uint32_t>(node, 0x10) = key[1];
    at<uint32_t>(node, 0x14) = 0;
    return node;
}

namespace {

uint32_t* pool_alloc(uint8_t* pool) {
    const int32_t count = at<int32_t>(pool, 0x10);
    if (count >= at<int32_t>(pool, 0xc)) return nullptr;
    uint32_t* const entry = at<uint32_t**>(pool, 4)[count];
    at<int32_t>(pool, 0x10) = count + 1;
    return entry;
}

}  // namespace

uint32_t* XB_CDECL PhysConstraintPool_Alloc(uint8_t* pool, uint32_t, uint32_t a, uint32_t b) {
    uint32_t* const entry = pool_alloc(pool);
    if (entry) {
        at<int32_t>(entry, 0x30) = -1;
        at<int32_t>(entry, 0x38) = -1;
        at<int32_t>(entry, 0x40) = -1;
        at<uint32_t>(entry, 0x48) = 0;  // 0.0f
    }
    entry[0] = a;
    entry[1] = b;
    return entry;
}

uint32_t* XB_CDECL PhysConstraintPool28c_Alloc(uint8_t* pool, uint32_t, uint32_t a, uint32_t b) {
    uint32_t* const entry = pool_alloc(pool);
    if (entry)
        for (uint32_t offset = 0xa8; offset <= 0xc0; offset += 8) at<int32_t>(entry, offset) = -1;
    entry[0] = a;
    entry[1] = b;
    return entry;
}

namespace {

// Key pairs compare on the first body, then the second (unsigned).
bool pair_less(uint32_t a0, uint32_t a1, uint32_t b0, uint32_t b1) { return a0 != b0 ? a0 < b0 : a1 < b1; }

int32_t& balance(uint8_t* node) { return at<int32_t>(node, 8); }

}  // namespace

int32_t XB_THISCALL PhysPairTree_Insert(uint8_t* tree, uint8_t** slot) {
    uint8_t* node = *slot;
    if (!node) {
        node = PhysPairTree_AllocNode(tree, at<const uint32_t*>(tree, 0x10));
        *slot = node;
        at<uint8_t*>(tree, 0x14) = node;
        return 1;
    }
    const uint32_t* const key = at<const uint32_t*>(tree, 0x10);
    if (pair_less(key[0], key[1], at<uint32_t>(node, 0xc), at<uint32_t>(node, 0x10))) {
        if (!PhysPairTree_Insert(tree, reinterpret_cast<uint8_t**>(node))) return 0;  // left child at +0
        balance(*slot) = static_cast<int32_t>(static_cast<uint32_t>(balance(*slot)) - 1u);
        node = *slot;
        if (balance(node) < -1) {
            if (balance(at<uint8_t*>(node, 0)) == 1) PhysPairTree_RotateLeft(reinterpret_cast<uint8_t**>(node));
            PhysPairTree_RotateRight(slot);
        }
        return balance(*slot);
    }
    if (pair_less(at<uint32_t>(node, 0xc), at<uint32_t>(node, 0x10), key[0], key[1])) {
        if (!PhysPairTree_Insert(tree, reinterpret_cast<uint8_t**>(node + 4))) return 0;
        balance(*slot) = static_cast<int32_t>(static_cast<uint32_t>(balance(*slot)) + 1u);
        node = *slot;
        if (balance(node) > 1) {
            if (balance(at<uint8_t*>(node, 4)) == -1) PhysPairTree_RotateRight(reinterpret_cast<uint8_t**>(node + 4));
            PhysPairTree_RotateLeft(slot);
        }
        return balance(*slot);
    }
    at<uint8_t*>(tree, 0x14) = node;
    return 0;
}

// The original's frame: the sorted pair at entry - 8, EBX/EBP/ESI/EDI saved below it; the
// insert runs at its ESP with its registers (the tree keeps the pair's address).
PLATFORM_ASM_ENTRY(PhysWorld_GetBodyPair_entry,
                   "movl %esp, %edx\n"
                   GAME_GAP_ESP("0x10000")
                   "pushl %edx\n"
                   "pushl %edx\n"
                   "pushl %edi\n"
                   "pushl %esi\n"
                   "pushl %ebp\n"
                   "pushl %ebx\n"
                   "pushl 16(%edx)\n"
                   "pushl 12(%edx)\n" PLATFORM_ASM_CALL(PhysWorld_GetBodyPair) "addl $28, %esp\n"
                   "popl %esp\n"
                   "ret")

uint32_t XB_CDECL PhysWorld_GetBodyPair(uint32_t a, uint32_t b, uint32_t entry_ebx, uint32_t entry_ebp,
                                        uint32_t entry_esi, uint32_t entry_edi, uint32_t entry_esp) {
    const auto word = [](uint32_t address) -> uint32_t& { return *reinterpret_cast<uint32_t*>(uintptr_t{address}); };
    const uint32_t pair = entry_esp - 8;
    word(pair) = a < b ? a : b;
    word(pair + 4) = a < b ? b : a;
    word(entry_esp - 0xc) = entry_ebx;
    word(entry_esp - 0x10) = entry_ebp;
    word(entry_esp - 0x14) = entry_esi;
    word(entry_esp - 0x18) = entry_edi;
    uint8_t* const tree = phys_world() + 0x1b0;
    const uint32_t tree_address = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(tree));
    at<uint32_t>(tree, 0x10) = pair;
    TitleRegs regs{};
    regs.eax = pair;
    regs.ecx = tree_address;
    regs.ebx = a;
    regs.esi = tree_address;
    regs.edi = entry_edi;
    const uint32_t args[1] = {tree_address + 0xc};
    const uint32_t top = entry_esp - 0x18;
    title_call_at_regs(top, 0x000b60b0, &regs, 1, args);  // PhysPairTree_Insert
    word(top - 8) = 0x000b6313;
    uint8_t** const slot = reinterpret_cast<uint8_t**>(at<uint8_t*>(tree, 0x14) + 0x14);
    if (!*slot) {
        uint8_t* const world = phys_world();
        uint8_t* record = nullptr;
        const int32_t count = at<int32_t>(world, 0x20c);
        if (count < at<int32_t>(world, 0x208)) {
            record = at<uint8_t**>(world, 0x200)[count];
            at<int32_t>(world, 0x20c) = count + 1;
            if (record) {
                for (uint32_t i = 0xc; i < 0x24; i += 4) at<uint32_t>(record, i) = 0;
                at<uint8_t*>(record, 0x24) = record + 0xc;
                at<uint8_t*>(record, 0x28) = record + 0x18;
                at<uint32_t>(record, 0x2c) = 0;
            }
        }
        *slot = record;
        at<uint32_t>(record, 0) = a;
        at<uint32_t>(record, 4) = b;
        at<uint8_t**>(*slot, 0x2c) = slot;
    }
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(*slot));
}

void XB_THISCALL PhysConstraint28c_Setup(uint8_t* constraint, const float* v0, const float* v1, const float* v2,
                                         uint32_t f0, uint32_t f1, uint32_t f2, uint32_t f3, uint32_t f4,
                                         uint32_t f5, uint32_t f6) {
    uint32_t t[4];
    std::memcpy(t, v0, 16);
    std::memcpy(constraint + 0x30, t, 16);
    std::memcpy(t, v1, 16);
    std::memcpy(constraint + 0x40, t, 16);
    std::memcpy(t, v2, 16);
    const uint32_t floats[7] = {f0, f1, f2, f3, f4, f5, f6};
    for (uint32_t i = 0; i < 7; ++i) at<uint32_t>(constraint, 0x60 + i * 4) = floats[i];
    std::memcpy(constraint + 0x50, t, 16);
    at<uint32_t>(constraint, 0x88) = 0;
    at<uint32_t>(constraint, 0xa0) = 1;
    at<uint32_t>(constraint, 0xa4) = 0;
    at<float>(constraint, 0x7c) = title_global<float>(0x0083eb20);  // 1
    at<uint32_t>(constraint, 0x8c) = 0;
    at<uint32_t>(constraint, 0x94) = 0;
    at<uint32_t>(constraint, 0x90) = 0;
    at<uint32_t>(constraint, 0x98) = 0;
}

// The second body (+4), its anchor (+0x10) and axis (+0x20) of a constraint; flag 1 of +0xa4.
// Unreferenced.
void XB_THISCALL PhysConstraint_AttachSecondBody(uint8_t* constraint, uint32_t body, const float* anchor,
                                                 const float* axis) {
    at<uint32_t>(constraint, 0xa4) |= 1;
    at<uint32_t>(constraint, 4) = body;
    uint32_t t[4];
    std::memcpy(t, anchor, 16);
    std::memcpy(constraint + 0x10, t, 16);
    std::memcpy(t, axis, 16);
    std::memcpy(constraint + 0x20, t, 16);
}

// The ends of the constraint's segment (centre +0x30, axis +0x40, half length +0x60) moved out
// of the frame: centre - axis * len and centre + axis * len (xyz only). Unreferenced.
void XB_THISCALL PhysConstraint_GetSegmentEnds(const uint8_t* constraint, const float* frame, float* lo, float* hi) {
    const Lanes centre = transform_out(frame, reinterpret_cast<const float*>(constraint + 0x30));
    const Lanes axis = rotate_out(frame, reinterpret_cast<const float*>(constraint + 0x40));
    const Lanes offset = mul(splat(at<float>(constraint, 0x60)), axis);
    const Lanes a = sub(centre, offset);
    std::memcpy(lo, a.v, 12);
    const Lanes b = add(centre, offset);
    std::memcpy(hi, b.v, 12);
}

namespace {

using RotateVector = float* XB_CDECL(float*, const uint8_t*, const float*);

// Both bodies' anchor points (A: rotated +0x30 plus +0x60 times the rotated axis +0x40; B: the
// rotated +0x10), each offset by its body's position; returns the separation along the rotated
// axis, (x + y) + z of the product.
float constraint_separation(uint8_t* c, uint32_t rotate, uint32_t offset) {
    const auto rot = title_function<RotateVector>(rotate);
    const auto off = title_function<RotateVector>(offset);
    const uint8_t* const body_a = at<uint8_t*>(c, 0);
    const uint8_t* const body_b = at<uint8_t*>(c, 4);
    alignas(16) float axis[4], anchor[4], anchor_b[4], point_a[4], point_b[4];
    rot(axis, body_a, reinterpret_cast<const float*>(c + 0x40));
    const Lanes scaled = mul(splat(at<float>(c, 0x60)), lanes(axis));
    rot(anchor, body_a, reinterpret_cast<const float*>(c + 0x30));
    alignas(16) float a[4];
    std::memcpy(a, add(lanes(anchor), scaled).v, 16);
    rot(anchor_b, body_b, reinterpret_cast<const float*>(c + 0x10));
    const float* const pb = off(point_b, body_b, anchor_b);
    const float* const pa = off(point_a, body_a, a);
    const Lanes d = mul(sub(lanes(pa), lanes(pb)), lanes(axis));
    platform_keep(d);
    return (d.v[0] + d.v[1]) + d.v[2];
}

}  // namespace

// Flag 2 of +0xa4: set while the separation reaches the limit +0x74 (ordered), else cleared;
// only for constraints with a second body (flag 1).
void XB_THISCALL PhysConstraint_UpdateLimitFlag(uint8_t* c, uint32_t) {
    if (!(at<uint8_t>(c, 0xa4) & 1)) return;
    const float separation = constraint_separation(c, 0x000b73d0, 0x000b7260);  // PhysBody_RotateVector2, PhysBody_OffsetByField70
    const uint32_t flags = at<uint32_t>(c, 0xa4);
    at<uint32_t>(c, 0xa4) = separation >= at<float>(c, 0x74) ? flags | 2 : flags & ~2u;
}

// The separation clamped to the limit +0x74 (0 without a second body) into +0x98; +0x9c = +0xb4
// when +0xc8 is set, else 0. With a contact at +0xd0 its normal impulse times +0x90 / +0x60 is
// added to the speed +0x8c (x87); flag 4 when the contact's +0x8c has bit 4 (or, without one,
// +0xcc's has bit 2). Then the position +0x94 += speed * dt.
void XB_THISCALL PhysConstraint_UpdateDrive(uint8_t* c, float dt) {
    const uint32_t flags = at<uint32_t>(c, 0xa4);
    float separation = 0.0f;
    if (flags & 1) {
        separation = constraint_separation(c, 0x0002ac30, 0x000b71d0);  // PhysBody_RotateVector, PhysBody_OffsetByPosition
        if (separation > at<float>(c, 0x74)) separation = at<float>(c, 0x74);
    }
    at<float>(c, 0x98) = separation;
    if (at<uint32_t>(c, 0xc8))
        at<uint32_t>(c, 0x9c) = at<uint32_t>(c, 0xb4);
    else
        at<uint32_t>(c, 0x9c) = 0;
    const uint8_t* const other = at<uint8_t*>(c, 0xcc);
    if (other) {
        uint8_t* const contact = at<uint8_t*>(c, 0xd0);
        if (contact) {
            if (at<uint8_t>(contact, 0x8c) & 4) at<uint32_t>(c, 0xa4) = flags | 4;
            const long double impulse =
                title_function<long double XB_THISCALL(uint8_t*)>(0x000bcb40)(contact);  // PhysContact_SolveNormalImpulse
            const long double ratio = static_cast<long double>(at<float>(c, 0x90)) / at<float>(c, 0x60);
            at<float>(c, 0x8c) = static_cast<float>(impulse * ratio + at<float>(c, 0x8c));
        } else if (at<uint8_t>(other, 0x8c) & 2) {
            at<uint32_t>(c, 0xa4) = flags | 4;
        }
    }
    at<float>(c, 0x94) = at<float>(c, 0x8c) * dt + at<float>(c, 0x94);
}

namespace {

// out = v when flag 0x10 of the body's +0x144 is set (dword copy), else the body's vector at
// `field` + v (4 lanes).
float* body_offset(float* out, const uint8_t* body, const float* v, uint32_t field) {
    if (at<uint8_t>(body, 0x144) & 0x10) {
        for (int32_t i = 0; i < 4; ++i) reinterpret_cast<uint32_t*>(out)[i] = reinterpret_cast<const uint32_t*>(v)[i];
        return out;
    }
    const Lanes value = lanes(v);
    const Lanes sum = add(lanes(reinterpret_cast<const float*>(body + field)), value);
    std::memcpy(out, sum.v, 16);
    return out;
}

// A 16-byte aligned 0xa0-byte block from the world's frame buffer (cursor +0x2bc, end +0x2b8),
// or 0 when it does not fit.
uint32_t contact_buffer(uint8_t* world) {
    const uint32_t start = (at<uint32_t>(world, 0x2bc) + 15) & ~15u;
    const uint32_t next = start + 0xa0;
    if (next > at<uint32_t>(world, 0x2b8)) return 0;
    at<uint32_t>(world, 0x2bc) = next;
    return start;
}

}  // namespace

float* XB_CDECL PhysBody_OffsetByPosition(float* out, const uint8_t* body, const float* v) {
    return body_offset(out, body, v, 0x30);
}

float* XB_CDECL PhysBody_OffsetByField70(float* out, const uint8_t* body, const float* v) {
    return body_offset(out, body, v, 0x70);
}

uint32_t XB_THISCALL PhysContact_AllocBufferB0(uint8_t* contact, uint8_t* world) {
    const uint32_t buffer = contact_buffer(world);
    at<uint32_t>(contact, 0xb0) = buffer;
    return buffer;  // FUN_000b68a0 reads it
}

uint32_t XB_THISCALL PhysContact_AllocBufferB4(uint8_t* contact, uint8_t* world) {
    const uint32_t buffer = contact_buffer(world);
    at<uint32_t>(contact, 0xb4) = buffer;
    return buffer;  // FUN_000b68a0 reads it
}

uint32_t* XB_CDECL PhysConstraintPoolD0_Alloc(uint8_t* pool, uint32_t, uint32_t a, uint32_t b) {
    uint32_t* const entry = pool_alloc(pool);
    if (entry)
        for (uint32_t offset = 0xac; offset <= 0xbc; offset += 8) at<int32_t>(entry, offset) = -1;
    entry[0] = a;
    entry[1] = b;
    return entry;
}

uint32_t* XB_CDECL PhysWorld_AllocConstraintD0(uint32_t a, uint32_t b) {
    return PhysConstraintPoolD0_Alloc(phys_world() + 0x2a0, 0, a, b);
}

// Flag 1 (+0x144) on each non-null body of the list at +0x28 (count +0x2c). Unreferenced.
uint32_t XB_THISCALL PhysRagdoll_SetBodiesFlag1(uint8_t* ragdoll) {
    uint8_t** it = at<uint8_t**>(ragdoll, 0x28);
    uint8_t** const end = it + at<int32_t>(ragdoll, 0x2c);
    for (; it != end; ++it)
        if (*it) at<uint32_t>(*it, 0x144) |= 1;
    return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(it));
}

namespace {

// MAXPS / MINPS lane rules: the second operand when unordered.
Lanes max_lanes(const Lanes& a, const Lanes& b) {
    Lanes out;
    for (int32_t i = 0; i < 4; ++i) out.v[i] = a.v[i] > b.v[i] ? a.v[i] : b.v[i];
    return out;
}
Lanes min_lanes(const Lanes& a, const Lanes& b) {
    Lanes out;
    for (int32_t i = 0; i < 4; ++i) out.v[i] = a.v[i] < b.v[i] ? a.v[i] : b.v[i];
    return out;
}

void copy_words(float* dst, const float* src) {
    for (int32_t i = 0; i < 4; ++i) reinterpret_cast<uint32_t*>(dst)[i] = reinterpret_cast<const uint32_t*>(src)[i];
}

}  // namespace

uint8_t XB_THISCALL PhysBox_SweepSphere(uint8_t* box, const float* a, float* b, float radius, const float* dir) {
    const Lanes centre = lanes(reinterpret_cast<const float*>(box + 0x10));
    const Lanes extent = lanes(reinterpret_cast<const float*>(box + 0x20));
    alignas(16) float lo[4], hi[4];
    std::memcpy(lo, sub(centre, extent).v, 16);
    std::memcpy(hi, add(centre, extent).v, 16);
    const Lanes start = lanes(a);
    const Lanes end = lanes(b);
    const Lanes r = splat(radius);
    // The swept sphere's bounds against the box, per axis: both gaps below 0 on x, y and z.
    const Lanes below = sub(lanes(lo), add(max_lanes(start, end), r));
    const Lanes above = sub(sub(min_lanes(start, end), r), lanes(hi));
    const Lanes gap = max_lanes(above, below);
    bool overlaps = true;
    for (int32_t i = 0; i < 4; ++i) {  // CMPLTPS compares all four lanes
        const bool negative = gap.v[i] < 0.0f;
        if (i < 3 && !negative) overlaps = false;
    }
    if (!overlaps) return 0;
    const float radius_sq = radius * radius;
    // Face hits: the sphere's leading point moved along dir to each face it starts outside of; a
    // point inside the face's rectangle is the hit, the others are candidates for the edge pass.
    alignas(16) float candidates[3][4];
    int32_t count = 0;
    for (int32_t i = 0; i < 3; ++i) {
        const int32_t k1 = (i + 1) % 3, k2 = (i + 2) % 3;
        const float d = dir[i];
        alignas(16) float p[4];
        float face;
        if (d > 0.0f) {
            if (!(lo[i] > a[i])) continue;
            copy_words(p, a);
            p[i] = radius + p[i];
            if (!(lo[i] > p[i])) {
                std::memcpy(&p[i], &lo[i], 4);
                copy_words(candidates[count++], p);
                continue;
            }
            face = lo[i];
        } else if (0.0f > d) {
            if (!(a[i] > hi[i])) continue;
            copy_words(p, a);
            p[i] = p[i] - radius;
            if (!(p[i] > hi[i])) {
                std::memcpy(&p[i], &hi[i], 4);
                copy_words(candidates[count++], p);
                continue;
            }
            face = hi[i];
        } else {
            continue;
        }
        const float t = (face - p[i]) / d;
        alignas(16) float q[4];
        std::memcpy(q, add(lanes(p), mul(lanes(dir), splat(t))).v, 16);
        if (q[k1] >= lo[k1] && hi[k1] >= q[k1] && q[k2] >= lo[k2] && hi[k2] >= q[k2]) {
            const float skin = radius + title_global<float>(0x007cc6e8);  // 0.002
            copy_words(b, q);
            float normal;
            if (d > 0.0f) {
                b[i] = b[i] - skin;
                normal = title_global<float>(0x0083eb34);  // -1
            } else {
                b[i] = skin + b[i];
                normal = title_global<float>(0x0083eb20);  // 1
            }
            b[4 + i] = normal;
            b[4 + k2] = 0.0f;
            b[4 + k1] = 0.0f;
            return 1;
        }
        copy_words(candidates[count++], q);
    }
    if (!count) return 0;
    // Edge/corner pass: each candidate clamped into the box; the sphere's first contact with it
    // along a -> b (fraction of |a - b|, x87), skipping points within 1e-5 of the previous one.
    const Lanes delta = sub(start, end);
    const Lanes dd = mul(delta, delta);
    platform_keep(dd);
    const float length_sq = (dd.v[0] + dd.v[1]) + dd.v[2];
    const float length = platform_x87_sqrt(length_sq);
    float best = length;
    alignas(16) float previous[4] = {};
    for (int32_t k = 0; k < count; ++k) {
        const Lanes clamped = min_lanes(lanes(hi), max_lanes(lanes(lo), lanes(candidates[k])));
        std::memcpy(candidates[k], clamped.v, 16);
        if (k != 0) {
            const Lanes diff = sub(lanes(previous), clamped);
            const Lanes sq = mul(diff, diff);
            platform_keep(sq);
            if (title_global<float>(0x0083effc) > (sq.v[0] + sq.v[1]) + sq.v[2]) continue;  // 1e-5
        }
        const Lanes w = sub(start, clamped);
        const Lanes ww = mul(w, w);
        platform_keep(ww);
        const float w_sq = (ww.v[0] + ww.v[1]) + ww.v[2];
        if (w_sq > radius_sq) {
            const Lanes dw = mul(delta, w);
            platform_keep(dw);
            const float projection = (dw.v[0] + dw.v[1]) + dw.v[2];
            if (projection > 0.0f) {
                const float disc = (radius_sq - w_sq) * length_sq + projection * projection;
                if (disc >= 0.0f) {
                    const long double t =
                        (static_cast<long double>(projection) - platform_x87_sqrt_ext(disc)) / length;
                    const float rounded = static_cast<float>(t);
                    if (static_cast<long double>(best) > t) best = rounded;
                }
            }
        }
        copy_words(previous, candidates[k]);
    }
    if (!(length > best)) return 0;
    if (0.0f > best)
        std::memcpy(b, add(lanes(a), mul(splat(best), delta)).v, 16);
    else
        copy_words(b, a);
    for (int32_t i = 0; i < 4; ++i)
        reinterpret_cast<uint32_t*>(b)[4 + i] = reinterpret_cast<const uint32_t*>(dir)[i] ^ 0x80000000u;
    return 1;
}

namespace {

const float* box_axis(int32_t i) { return &title_global<float>(0x007cc520 + static_cast<uint32_t>(i) * 16); }

// axis * splat(sign) into the normal slot (out + 4).
void store_axis_normal(float* out, int32_t axis, float sign) {
    std::memcpy(out + 4, mul(lanes(box_axis(axis)), splat(sign)).v, 16);
}

// CMPLTPS: all four lanes compared (signalling), bit i = a[i] < b[i].
uint32_t less_mask(const Lanes& a, const Lanes& b) {
    uint32_t mask = 0;
    for (int32_t i = 0; i < 4; ++i)
        if (a.v[i] < b.v[i]) mask |= 1u << i;
    return mask;
}

}  // namespace

uint8_t XB_THISCALL PhysBox_SphereContact(uint8_t* box, const float* p, float radius, float* out, uint32_t) {
    copy_words(out, p);
    const Lanes centre = lanes(reinterpret_cast<const float*>(box + 0x10));
    const Lanes extent = lanes(reinterpret_cast<const float*>(box + 0x20));
    const Lanes lo = sub(centre, extent);
    const Lanes hi = add(centre, extent);
    const Lanes r = splat(radius);
    const float minus_one = title_global<float>(0x007cc510);
    const float one = title_global<float>(0x007cc500);
    // Separated along an axis: no contact, the normal points away from that side.
    const Lanes below = sub(add(lanes(p), r), lo);
    platform_keep(below);
    for (int32_t i = 0; i < 3; ++i)
        if (0.0f >= below.v[i]) {
            store_axis_normal(out, i, minus_one);
            return 0;
        }
    const Lanes above = sub(sub(lanes(p), r), hi);
    platform_keep(above);
    for (int32_t i = 0; i < 3; ++i)
        if (above.v[i] >= 0.0f) {
            store_axis_normal(out, i, one);
            return 0;
        }
    // The centre clamped into the box; each clamped axis gets -1 / +1 in the normal.
    const float low_side = title_global<float>(0x0083eb34);  // -1
    const float high_side = title_global<float>(0x0083eb20);  // 1
    float normal[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    Lanes q = lanes(p);
    int32_t clamped = 0;
    if (!(lo.v[0] <= q.v[0])) {
        q.v[0] = lo.v[0];
        normal[0] = low_side;
        clamped = 1;
    } else if (!(q.v[0] <= hi.v[0])) {
        q.v[0] = hi.v[0];
        normal[0] = high_side;
        clamped = 1;
    }
    for (int32_t i = 1; i < 3; ++i) {
        if (less_mask(q, lo) & (1u << i)) {
            q.v[i] = lo.v[i];
            normal[i] = low_side;
            ++clamped;
        } else if (less_mask(hi, q) & (1u << i)) {
            q.v[i] = hi.v[i];
            normal[i] = high_side;
            ++clamped;
        }
    }
    if (clamped) {
        std::memcpy(out + 4, sub(lanes(p), q).v, 16);
        const Lanes dd = mul(lanes(out + 4), lanes(out + 4));
        platform_keep(dd);
        const float length_sq = (dd.v[0] + dd.v[1]) + dd.v[2];
        if (length_sq > radius * radius) {
            if (clamped == 1) {
                std::memcpy(out + 4, normal, 16);
                return 0;
            }
            const float length = platform_x87_sqrt(length_sq);
            std::memcpy(out + 4, div(lanes(out + 4), splat(length)).v, 16);
            return 0;
        }
        std::memcpy(out, q.v, 16);
        if (clamped != 1 && !(title_global<float>(0x0083ed50) > length_sq)) {  // 1e-4
            const float length = platform_x87_sqrt(length_sq);
            std::memcpy(out + 4, div(lanes(out + 4), splat(length)).v, 16);
            return 1;
        }
        std::memcpy(out + 4, normal, 16);
        if (clamped == 1) return 1;
        // Edge (two axes) or corner (three): the summed axes normalized.
        const float scale = clamped == 2 ? title_global<float>(0x007cc6f0) : title_global<float>(0x007cc6ec);
        std::memcpy(out + 4, mul(lanes(out + 4), splat(scale)).v, 16);
        return 1;
    }
    // Centre inside: pushed out through the nearest face.
    float best = p[0] - lo.v[0];
    store_axis_normal(out, 0, minus_one);
    const float to_high = hi.v[0] - p[0];
    if (best > to_high) {
        best = to_high;
        store_axis_normal(out, 0, one);
    }
    for (int32_t i = 1; i < 3; ++i) {
        const Lanes from_low = sub(lanes(p), lo);
        platform_keep(from_low);
        if (best > from_low.v[i]) {
            best = from_low.v[i];
            store_axis_normal(out, i, minus_one);
        }
        const Lanes from_high = sub(hi, lanes(p));
        platform_keep(from_high);
        if (best > from_high.v[i]) {
            best = from_high.v[i];
            store_axis_normal(out, i, one);
        }
    }
    const Lanes n = lanes(out + 4);
    std::memcpy(out, add(lanes(p), mul(splat(best), n)).v, 16);
    return 1;
}

namespace {

float* gjk_vec(uint32_t base, int32_t i) { return &title_global<float>(base + static_cast<uint32_t>(i) * 16); }
float* gjk_p(int32_t i) { return gjk_vec(0x0090a750, i); }
float* gjk_y(int32_t i) { return gjk_vec(0x0090a790, i); }
float* gjk_q(int32_t i) { return gjk_vec(0x0090a7d0, i); }
uint32_t& gjk_bits() { return title_global<uint32_t>(0x0090a748); }
uint32_t& gjk_all_bits() { return title_global<uint32_t>(0x0090a740); }
int32_t& gjk_last() { return title_global<int32_t>(0x0090a744); }
uint32_t& gjk_last_bit() { return title_global<uint32_t>(0x0090a630); }

float sum3(const Lanes& v) { return (v.v[0] + v.v[1]) + v.v[2]; }

float self_dot(const Lanes& v) {
    const Lanes sq = mul(v, v);
    platform_keep(sq);
    return sum3(sq);
}

// Johnson's test for the subset `set` (with the new point): each point in it needs a
// sub-determinant of at least 0.001 (the title's epsilon), each one outside it none above 0.
bool gjk_subset_valid(uint32_t set) {
    for (int32_t i = 0; i < 4; ++i) {
        const uint32_t bit = 1u << i;
        if (!(gjk_all_bits() & bit)) continue;
        if (set & bit) {
            if (title_global<float>(0x0083eb4c) > gjk_det(set)[i]) return false;  // 0.001
        } else if (gjk_det(set | bit)[i] > title_global<float>(0x0083eafc)) {  // 0
            return false;
        }
    }
    return true;
}

}  // namespace

int32_t XB_CDECL Gjk_ClosestPoints(void* shape_a, const float* frame, void* shape_b, float* out, float limit,
                                   uint8_t keep_going) {
    float* const out_v = out + 8;
    alignas(16) float v[4];
    copy_words(v, out_v);
    float dist_sq = self_dot(lanes(v));
    float lower = 0.0f;
    uint32_t iterations = 0;
    gjk_bits() = 0;
    gjk_all_bits() = 0;
    if (title_global<float>(0x008bd438) > dist_sq) return 2;
    uint32_t bits = 0;
    while (dist_sq > title_global<float>(0x008bd438)) {
        // Free simplex slot.
        uint32_t last_bit = 1;
        int32_t last = 0;
        gjk_last() = 0;
        gjk_last_bit() = 1;
        if (bits & 1) {
            do {
                last_bit <<= 1;
                ++last;
            } while (bits & last_bit);
            gjk_last_bit() = last_bit;
            gjk_last() = last;
        }
        // Support points: A's for -v (into A's frame and back out), B's for v.
        alignas(16) float rows[12];
        copy_words(rows, frame);
        copy_words(rows + 4, frame + 4);
        copy_words(rows + 8, frame + 8);
        Lanes columns[3];
        frame_columns(rows, columns);
        Lanes neg_v;
        for (int32_t i = 0; i < 4; ++i) {
            uint32_t bits_v;
            std::memcpy(&bits_v, &v[i], 4);
            bits_v ^= 0x80000000u;
            std::memcpy(&neg_v.v[i], &bits_v, 4);
        }
        alignas(16) float dir[4];
        std::memcpy(dir, rotate_into(columns, neg_v).v, 16);
        alignas(16) float support_a[4];
        void* const* vtable_a = *static_cast<void* const* const*>(shape_a);
        const float* sa = title_function<ShapeSupport>(reinterpret_cast<uintptr_t>(vtable_a[2]))(shape_a, support_a, dir);
        alignas(16) float point[4];
        std::memcpy(point, sa, 16);
        std::memcpy(gjk_p(gjk_last()), transform_out(frame, point).v, 16);
        alignas(16) float support_b[4];
        void* const* vtable_b = *static_cast<void* const* const*>(shape_b);
        const float* sb = title_function<ShapeSupport>(reinterpret_cast<uintptr_t>(vtable_b[2]))(shape_b, support_b, v);
        last = gjk_last();
        copy_words(gjk_q(last), sb);
        alignas(16) float w[4];
        std::memcpy(w, sub(lanes(gjk_p(last)), lanes(gjk_q(last))).v, 16);
        const Lanes vw = mul(lanes(v), lanes(w));
        platform_keep(vw);
        const float dot = sum3(vw);
        if (dot > title_global<float>(0x0083eafc)) {  // 0
            const float dot_sq = dot * dot;
            if (dot_sq > lower * dist_sq) lower = dot_sq / dist_sq;
        }
        if (lower > limit && !keep_going) {
            copy_words(out, gjk_p(last));
            copy_words(out + 4, gjk_q(last));
            copy_words(out_v, v);
            return 0;
        }
        bool done = iterations != 0 && lower > title_global<float>(0x008bd43c) * dist_sq;
        // A support point already in the simplex (within 1e-4): no progress.
        for (int32_t i = 0; i < 4 && !done; ++i) {
            if (!(gjk_all_bits() & (1u << i))) continue;
            if (title_global<float>(0x0083ed50) > self_dot(sub(lanes(w), lanes(gjk_y(i))))) done = true;
        }
        if (done) {
            bits = gjk_bits();
            break;
        }
        last_bit = gjk_last_bit();
        bits = gjk_bits();
        copy_words(gjk_y(last), w);
        gjk_all_bits() = last_bit | bits;
        ++iterations;
        if (iterations > 20) break;
        Gjk_ComputeDeterminants();
        // The smallest valid subset: subsets of the old simplex from the largest mask down, each
        // with the new point.
        uint32_t set = bits;
        bool found = false;
        for (; set != 0; --set) {
            if ((set & bits) != set) continue;
            if (gjk_subset_valid(set | last_bit)) {
                found = true;
                break;
            }
        }
        if (found) {
            bits = set | last_bit;
            gjk_bits() = bits;
            const Lanes zero = lanes(&title_global<float>(0x007ccc10));
            Lanes acc = zero;
            float sum = 0.0f;
            if (bits & 1) {
                sum = gjk_det(bits)[0];
                acc = add(zero, mul(lanes(gjk_y(0)), splat(sum)));
            }
            for (int32_t i = 1; i < 4; ++i) {
                if (!(bits & (1u << i))) continue;
                const float d = gjk_det(bits)[i];
                sum = sum + d;
                acc = add(acc, mul(lanes(gjk_y(i)), splat(d)));
            }
            const float inv = title_global<float>(0x0083eb20) / sum;  // 1
            std::memcpy(v, mul(acc, splat(inv)).v, 16);
        } else {
            // Only the new point: valid unless its tests fail (then no progress: intersecting).
            for (int32_t i = 0; i < 4; ++i) {
                const uint32_t bit = 1u << i;
                if (!(gjk_all_bits() & bit)) continue;
                if (last_bit & bit) {
                    if (title_global<float>(0x0083eb4c) > gjk_det(last_bit)[i]) return 2;
                } else if (gjk_det(bit | last_bit)[i] > 0.0f) {
                    return 2;
                }
            }
            copy_words(v, w);
            bits = last_bit;
            gjk_bits() = bits;
        }
        const float previous = dist_sq;
        dist_sq = self_dot(lanes(v));
        if (dist_sq > previous && iterations > 1) return 2;
        if (static_cast<int32_t>(bits) >= 15) break;
    }
    if (bits == 15) return 2;
    // Closest points: the simplex's p and q weighted by the sub-determinants.
    copy_words(out, &title_global<float>(0x007ccd90));  // (0, 0, 0, 1)
    copy_words(out + 4, &title_global<float>(0x007ccd90));
    float sum = 0.0f;
    for (int32_t i = 0; i < 4; ++i) {
        if (!(bits & (1u << i))) continue;
        const float d = gjk_det(bits)[i];
        sum = i == 0 ? d : sum + d;
        std::memcpy(out, add(lanes(out), mul(lanes(gjk_p(i)), splat(d))).v, 16);
        std::memcpy(out + 4, add(lanes(out + 4), mul(lanes(gjk_q(i)), splat(gjk_det(bits)[i]))).v, 16);
    }
    const float inv = title_global<float>(0x0083eb20) / sum;
    std::memcpy(out, mul(lanes(out), splat(inv)).v, 16);
    std::memcpy(out + 4, mul(lanes(out + 4), splat(inv)).v, 16);
    copy_words(out_v, v);
    return dist_sq > limit ? 0 : 1;
}

uint8_t XB_CDECL Phys_ShapesOverlapGjk(void* shape_a, const float* frame_a, void* shape_b, const float* frame_b,
                                       float limit) {
    const uint8_t* const a = static_cast<const uint8_t*>(shape_a);
    const uint8_t* const b = static_cast<const uint8_t*>(shape_b);
    // Bounding spheres (centre +0x10, radius +0x1c) first.
    const float radii = at<float>(a, 0x1c) + at<float>(b, 0x1c);
    const Lanes centre_b = transform_out(frame_b, reinterpret_cast<const float*>(b + 0x10));
    const Lanes centre_a = transform_out(frame_a, reinterpret_cast<const float*>(a + 0x10));
    if (self_dot(sub(centre_a, centre_b)) > radii * radii) return 0;
    // A's frame relative to B's: rows and origin rotated into B.
    alignas(16) float rows_b[12];
    copy_words(rows_b, frame_b);
    copy_words(rows_b + 4, frame_b + 4);
    copy_words(rows_b + 8, frame_b + 8);
    Lanes columns[3];
    frame_columns(rows_b, columns);
    alignas(16) float relative[16];
    std::memcpy(relative + 12, rotate_into(columns, sub(lanes(frame_a + 12), lanes(frame_b + 12))).v, 16);
    std::memcpy(relative + 8, rotate_into(columns, lanes(frame_a + 8)).v, 16);
    std::memcpy(relative + 4, rotate_into(columns, lanes(frame_a + 4)).v, 16);
    std::memcpy(relative, rotate_into(columns, lanes(frame_a)).v, 16);
    // GJK (keep going past the limit) from A's centre minus B's, in B's frame; the title's result
    // buffer is the relative frame's first copy.
    alignas(16) float frame[16];
    copy_words(frame, relative);
    copy_words(frame + 4, relative + 4);
    copy_words(frame + 8, relative + 8);
    copy_words(frame + 12, relative + 12);
    std::memcpy(relative + 8, sub(transform_out(frame, reinterpret_cast<const float*>(a + 0x10)),
                                  lanes(reinterpret_cast<const float*>(b + 0x10))).v,
                16);
    return Gjk_ClosestPoints(shape_a, frame, shape_b, relative, limit * limit, 1) != 0 ? 1 : 0;
}


// Push 5: joints, contact records, constraint setters.

namespace {

// A 16-byte aligned record of `size` bytes from the world's frame buffer (cursor +0x2bc, end
// +0x2b8; 0 when it does not fit), appended to the list at `head` (head, tail, count) with its
// link word cleared.
uint8_t* alloc_linked(uint8_t* world, uint32_t size, uint32_t head) {
    uint8_t* record = reinterpret_cast<uint8_t*>((at<uint32_t>(world, 0x2bc) + 0xf) & ~0xfu);
    const uint32_t next = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(record)) + size;
    if (next > at<uint32_t>(world, 0x2b8)) {
        record = nullptr;
    } else {
        at<uint32_t>(world, 0x2bc) = next;
    }
    ++at<int32_t>(world, head + 8);
    uint8_t* const tail = at<uint8_t*>(world, head + 4);
    if (tail)
        at<uint8_t*>(tail, 0) = record;
    else
        at<uint8_t*>(world, head) = record;
    at<uint8_t*>(world, head + 4) = record;
    at<uint32_t>(record, 0) = 0;
    return record;
}

// a to +0x10 and b to +0x20, each loaded whole before its store (MOVUPS).
void set_anchor_pair(uint8_t* c, const float* a, const float* b) {
    uint32_t t[4];
    std::memcpy(t, a, 16);
    std::memcpy(c + 0x10, t, 16);
    std::memcpy(t, b, 16);
    std::memcpy(c + 0x20, t, 16);
}

// The normalized axis (4 words) followed by cos and sin of the angle (x87 FCOS/FSIN).
void set_axis_angle(uint8_t* out, const float* axis, float angle) {
    alignas(16) float n[4];
    const float* const p = Vec3A_NormalizeDup(n, axis);
    const float c = platform_x87_cos(angle);
    std::memcpy(out, p, 16);
    at<float>(out, 0x10) = c;
    at<float>(out, 0x14) = platform_x87_sin(angle);
}

void set_normalized(uint8_t* out, const float* v) {
    alignas(16) float n[4];
    const float* const p = Vec3A_NormalizeDup(n, v);
    std::memcpy(out, p, 16);
}

void store4(float* out, const Lanes& v) { std::memcpy(out, v.v, 16); }

// SHUFPS 0xc9: (y, z, x, w).
Lanes yzx(const Lanes& a) { return {{a.v[1], a.v[2], a.v[0], a.v[3]}}; }

// +0xc0 = 0x000b8990(+0xa0) and +0xd0 = +0xa0 x +0xc0 (both re-read).
void set_frame_axes(uint8_t* joint) {
    alignas(16) float t[4];
    const float* const p =
        title_function<float* XB_CDECL(float*, const float*)>(0x000b8990)(t, reinterpret_cast<float*>(joint + 0xa0));
    std::memcpy(joint + 0xc0, p, 16);
    const Lanes v = lanes(reinterpret_cast<const float*>(joint + 0xc0));
    const Lanes u = lanes(reinterpret_cast<const float*>(joint + 0xa0));
    const Lanes cross = sub(mul(u, yzx(v)), mul(yzx(u), v));
    std::memcpy(joint + 0xd0, yzx(cross).v, 16);
}

// a x b as the title's SHUFPS 0xc9 code computes it: yzx(a * yzx(b) - yzx(a) * b), all four lanes.
Lanes cross_lanes(const Lanes& a, const Lanes& b) { return yzx(sub(mul(a, yzx(b)), mul(yzx(a), b))); }

// rows +0, +0x10, +0x20 of m times v: (row0 x + row1 y) + row2 z.
Lanes mat_rotate(const float* m, const Lanes& v) {
    return add(add(mul(lanes(m), splat(v.v[0])), mul(lanes(m + 4), splat(v.v[1]))), mul(lanes(m + 8), splat(v.v[2])));
}

// joint+out = normalize(rotation(+0xb0 copy, angle) * v).
void set_rotated_axis(uint8_t* joint, const float* v, float angle, uint32_t out) {
    alignas(16) float axis[4];
    std::memcpy(axis, joint + 0xb0, 16);
    alignas(16) float m[16];
    Mat34_FromRotationVector(m, axis, angle);
    alignas(16) float rotated[4];
    std::memcpy(rotated, mat_rotate(m, lanes(v)).v, 16);
    alignas(16) float n[4];
    const float* const p = Vec3A_Normalize(n, rotated);
    std::memcpy(joint + out, p, 16);
}

// The constraint's body for rate terms: the first one unless missing or flagged 0x10, else the second.
const uint8_t* rate_body(const uint8_t* c) {
    const uint8_t* body = at<uint8_t*>(c, 0);
    if (!body || (at<uint8_t>(body, 0x144) & 0x10)) body = at<uint8_t*>(c, 4);
    return body;
}

}  // namespace

// Contact records of 0xc0 bytes, list at +0x2f0/+0x2f4/+0x2f8.
uint8_t* XB_THISCALL PhysWorld_AllocContactC0(uint8_t* world) { return alloc_linked(world, 0xc0, 0x2f0); }

// PhysBody_RotateVector with the body's second rotation (rows +0x40, +0x50, +0x60).
float* XB_CDECL PhysBody_RotateVector2(float* out, const uint8_t* body, const float* v) {
    if (body[0x144] & 0x10) {
        const uint32_t* const from = reinterpret_cast<const uint32_t*>(v);
        uint32_t* const to = reinterpret_cast<uint32_t*>(out);
        for (int i = 0; i < 4; ++i) to[i] = from[i];
        return out;
    }
    const Lanes in = lanes(v);
    const float* const m = reinterpret_cast<const float*>(body + 0x40);
    const Lanes r = add(add(mul(lanes(m), splat(in.v[0])), mul(lanes(m + 4), splat(in.v[1]))),
                        mul(lanes(m + 8), splat(in.v[2])));
    store4(out, r);
    return out;
}

void XB_THISCALL PhysJoint_SetAnchors(uint8_t* joint, const float* a, const float* b) { set_anchor_pair(joint, a, b); }

// +0x1a0 = value; flag 0x100 of +0x30 while it is above 0 (ordered).
void XB_THISCALL PhysJoint_SetValue1A0(uint8_t* joint, float value) {
    at<float>(joint, 0x1a0) = value;
    const uint32_t flags = at<uint32_t>(joint, 0x30);
    at<uint32_t>(joint, 0x30) = value > title_global<float>(0x0083eafc) ? flags | 0x100 : flags & ~0x100u;
}

// Unreferenced.
void XB_THISCALL PhysAngleLimit_Set(uint8_t* limit, const float* axis, float angle) {
    set_axis_angle(limit, axis, angle);
}

// Unreferenced.
void XB_THISCALL PhysVec_SetNormalized(float* out, const float* v) {
    set_normalized(reinterpret_cast<uint8_t*>(out), v);
}

// +0xa0 and +0xe0 = the normalized axes; with flag 0x10 of +0x30 also +0xc0 = 0x000b8990(+0xa0)
// and +0xd0 = +0xa0 x +0xc0. Unreferenced.
void XB_THISCALL PhysJoint_SetAxes(uint8_t* joint, const float* a, const float* b) {
    set_normalized(joint + 0xa0, a);
    set_normalized(joint + 0xe0, b);
    if (at<uint8_t>(joint, 0x30) & 0x10) set_frame_axes(joint);
}

// The next of four 0x20-byte angle limits (+0x190, count +0x194); the original writes through
// null when all four are used.
void XB_THISCALL PhysJoint_AddAngleLimit(uint8_t* joint, const float* axis, float angle) {
    const int32_t count = at<int32_t>(joint, 0x194);
    uint8_t* slot = nullptr;
    if (count < 4) {
        slot = at<uint8_t*>(joint, 0x190) + static_cast<uint32_t>(count) * 0x20;
        at<int32_t>(joint, 0x194) = count + 1;
    }
    set_axis_angle(slot, axis, angle);
}

// Moves body B (+4) so that its anchor (+0x20) meets body A's (+0x10); returns the squared move.
float XB_THISCALL PhysJoint_SnapBodyToAnchor(uint8_t* joint) {
    uint8_t* const body_b = at<uint8_t*>(joint, 4);
    alignas(16) float rb[4], pb[4], ra[4], pa[4];
    const float* const b =
        PhysBody_OffsetByPosition(pb, body_b, PhysBody_RotateVector(rb, body_b, reinterpret_cast<float*>(joint + 0x20)));
    const uint8_t* const body_a = at<uint8_t*>(joint, 0);
    const float* const a =
        PhysBody_OffsetByPosition(pa, body_a, PhysBody_RotateVector(ra, body_a, reinterpret_cast<float*>(joint + 0x10)));
    alignas(16) float delta[4];
    const Lanes d = sub(lanes(a), lanes(b));
    std::memcpy(delta, d.v, 16);
    PhysBody_Translate(body_b, delta);
    const Lanes sq = mul(d, d);
    platform_keep(sq);
    return (sq.v[0] + sq.v[1]) + sq.v[2];
}

// PhysContact_ComputeBias_2 for this world's contacts (separation from 0x000bcc60).
void XB_THISCALL PhysContact_ComputeBias(uint8_t* contact, float dt, float limit) {
    const float min_dt = title_global<float>(0x007cd880);  // 1/240
    if (!(dt > min_dt)) dt = min_dt;
    const long double separation = title_function<long double XB_THISCALL(uint8_t*)>(0x000bcc60)(contact);
    const float bias = static_cast<float>(-((separation - title_global<float>(0x007cd878)) / dt));  // 0.02
    float& out = at<float>(contact, 0x7c);
    out = bias;
    if (platform_x87_greater(title_global<float>(0x0083eafc), bias)) out = bias * title_global<float>(0x007cd874);  // 0, 0.3
    const float lowest = 0.0f - limit;
    if (lowest > out) out = lowest;
    const float value = out;
    at<float>(contact, 0x80) = 0.0f;
    if (value >= 0.0f) {
        at<uint32_t>(contact, 0x78) = at<uint32_t>(contact, 0x7c);
        out = 0.0f;
    } else {
        at<float>(contact, 0x78) = 0.0f;
    }
}

// body +0x30 += delta (4 lanes).
void XB_CDECL PhysBody_Translate(uint8_t* body, const float* delta) {
    const Lanes d = lanes(delta);
    float* const position = reinterpret_cast<float*>(body + 0x30);
    store4(position, add(lanes(position), d));
}

// +0xe0 = 0x000bc620's vector * (-0.5 / max(dt, 1/240)); +0xd0 = the constant at 0x007cd650.
void XB_THISCALL PhysContact140_SetBias(uint8_t* contact, float dt) {
    const float min_dt = title_global<float>(0x007cd880);
    if (!(dt > min_dt)) dt = min_dt;
    alignas(16) float t[4];
    const float* const v = title_function<const float* XB_THISCALL(uint8_t*, float*)>(0x000bc620)(contact, t);
    const Lanes value = lanes(v);
    const float scale = title_global<float>(0x0083eb28) / dt;
    std::memcpy(contact + 0xe0, mul(splat(scale), value).v, 16);
    for (uint32_t i = 0; i < 4; ++i) at<uint32_t>(contact, 0xd0 + i * 4) = title_global<uint32_t>(0x007cd650 + i * 4);
}

// Contact records of 0x140 bytes, list at +0x2d8/+0x2dc/+0x2e0, built by 0x000bdfa0.
void XB_THISCALL PhysWorld_AddContact140(uint8_t* world, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t a4,
                                         uint32_t a5, float dt) {
    uint8_t* const contact = alloc_linked(world, 0x140, 0x2d8);
    using Init = void XB_THISCALL(uint8_t*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
    title_function<Init>(0x000bdfa0)(contact, a1, a2, a3, a4, a5);
    PhysContact140_SetBias(contact, dt);
}

void XB_THISCALL PhysAttach_SetAnchors(uint8_t* attach, const float* a, const float* b) {
    set_anchor_pair(attach, a, b);
}

// +0x48 = (+0x44^2 + +0x3c^2) + +0x34^2.
void XB_THISCALL PhysConstraint_UpdateSumSquares48(uint8_t* c, uint32_t) {
    const float z = at<float>(c, 0x44), y = at<float>(c, 0x3c), x = at<float>(c, 0x34);
    at<float>(c, 0x48) = (z * z + y * y) + x * x;
}

// Anchors a/b, +0x30 = value30, position +0x34 = target +0x38 = target, rate +0x3c = 0, +0x40 = 0,
// +0x44 = 1.
void XB_THISCALL PhysConstraint60_Init(uint8_t* c, const float* a, const float* b, float value30, float target) {
    set_anchor_pair(c, a, b);
    at<float>(c, 0x30) = value30;
    at<float>(c, 0x34) = target;
    at<float>(c, 0x38) = target;
    at<float>(c, 0x3c) = 0.0f;
    at<float>(c, 0x40) = 0.0f;
    at<uint32_t>(c, 0x44) = 1;
}

// Rate +0x3c = (target +0x38 - position +0x34) / (body +0x13c * dt).
void XB_THISCALL PhysConstraint60_ComputeTargetRate(uint8_t* c, const float* dt) {
    const float scale = at<float>(rate_body(c), 0x13c);
    const float diff = at<float>(c, 0x38) - at<float>(c, 0x34);
    at<float>(c, 0x3c) = diff / (scale * *dt);
}

void XB_THISCALL PhysConstraint60_AdvanceTarget(uint8_t* c, float dt) {
    at<float>(c, 0x34) = at<float>(c, 0x3c) * dt + at<float>(c, 0x34);
}

void XB_THISCALL PhysConstraint60_SnapToTarget(uint8_t* c, uint32_t) { at<uint32_t>(c, 0x34) = at<uint32_t>(c, 0x38); }

// Frame +0x10 = saved frame +0x50 (16 words).
void XB_THISCALL PhysConstraintFrame_Restore(uint8_t* c, uint32_t) {
    for (uint32_t i = 0; i < 16; ++i) at<uint32_t>(c, 0x10 + i * 4) = at<uint32_t>(c, 0x50 + i * 4);
}

// +0xa0 = value, +0xa4 = 1, both frames (+0x10, +0x50) = frame, +0x90 = 0, byte +0xa8 = 1. Unreferenced.
uint8_t* XB_THISCALL PhysConstraintFrame_Init(uint8_t* c, float value, const float* frame) {
    at<float>(c, 0xa0) = value;
    at<float>(c, 0xa4) = title_global<float>(0x0083eb20);
    for (uint32_t i = 0; i < 16; ++i) at<uint32_t>(c, 0x10 + i * 4) = reinterpret_cast<const uint32_t*>(frame)[i];
    for (uint32_t i = 0; i < 16; ++i) at<uint32_t>(c, 0x50 + i * 4) = reinterpret_cast<const uint32_t*>(frame)[i];
    for (uint32_t i = 0; i < 4; ++i) at<float>(c, 0x90 + i * 4) = 0.0f;
    at<uint8_t>(c, 0xa8) = 1;
    return c;
}

// Clears both frames' origins (+0x40, +0x80), then 0x000bb4f0(frame, saved frame, body +0x13c * dt,
// zero vector, +0x90).
void XB_THISCALL PhysConstraintFrame_Update(uint8_t* c, const float* dt) {
    for (uint32_t i = 0; i < 4; ++i) at<float>(c, 0x40 + i * 4) = 0.0f;
    for (uint32_t i = 0; i < 4; ++i) at<float>(c, 0x80 + i * 4) = 0.0f;
    const float scale = at<float>(rate_body(c), 0x13c) * *dt;
    alignas(16) float zero[4] = {0.0f, 0.0f, 0.0f, 0.0f};
    using Solve = void XB_CDECL(uint8_t*, uint8_t*, float, float*, uint8_t*);
    title_function<Solve>(0x000bb4f0)(c + 0x10, c + 0x50, scale, zero, c + 0x90);
}

// A 0x140 contact between body A (+0) at its rotated anchor +0x10 and body B (+4) at +0x20, with +0x30.
void XB_THISCALL PhysAttach_AddWorldContact(uint8_t* attach, uint8_t* world, float dt) {
    uint8_t* const body_b = at<uint8_t*>(attach, 4);
    uint8_t* const body_a = at<uint8_t*>(attach, 0);
    alignas(16) float rb[4], ra[4];
    const float* const b = PhysBody_RotateVector(rb, body_b, reinterpret_cast<float*>(attach + 0x20));
    const float* const a = PhysBody_RotateVector(ra, body_a, reinterpret_cast<float*>(attach + 0x10));
    auto word = [](const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); };
    PhysWorld_AddContact140(world, word(body_a), word(a), word(body_b), word(b), word(attach + 0x30), dt);
}

namespace {

uint32_t word_of(const void* p) { return static_cast<uint32_t>(reinterpret_cast<uintptr_t>(p)); }

// One axis contact of PhysJoint_AddAxisContacts: body B's rotated axis at `axis`, normal n.
void add_axis_contact(uint8_t* joint, uint8_t* world, const float* p, const float* n, float dt, uint32_t axis,
                      uint32_t extra) {
    uint8_t* const body = at<uint8_t*>(joint, 4);
    alignas(16) float r[4];
    PhysBody_RotateVector(r, body, reinterpret_cast<float*>(joint + axis));
    alignas(16) float rc[4], pc[4];
    std::memcpy(rc, r, 16);
    std::memcpy(pc, p, 16);
    uint8_t* const contact = PhysContacts_Add(world, at<uint32_t>(joint, 0), word_of(pc), word_of(body), word_of(rc),
                                              word_of(n), word_of(joint + extra));
    at<float>(contact, 0x60) = title_global<float>(0x0083eb74);  // -1e7
    at<float>(contact, 0x64) = 0.0f;
    PhysContact_ComputeBias(contact, dt, 5.0f);
}

}  // namespace

// Flag 0x40 of +0x30: a contact at p along -n for the axis +0xf0 (+0x64); flag 0x80: along n for
// the axis +0x100 (+0x6c).
void XB_THISCALL PhysJoint_AddAxisContacts(uint8_t* joint, uint8_t* world, const float* p, const float* n, float dt) {
    if (at<uint8_t>(joint, 0x30) & 0x40) {
        alignas(16) uint32_t negated[4];
        std::memcpy(negated, n, 16);
        for (uint32_t& w : negated) w ^= 0x80000000u;  // XORPS with 0x007cd5f0
        add_axis_contact(joint, world, p, reinterpret_cast<float*>(negated), dt, 0xf0, 0x64);
    }
    if (at<uint8_t>(joint, 0x30) & 0x80) add_axis_contact(joint, world, p, n, dt, 0x100, 0x6c);
}

// +0xf0 / +0x100 = v rotated about +0xb0 by angle_a / angle_b, normalized. Unreferenced.
void XB_THISCALL PhysJoint_SetLimitAxes(uint8_t* joint, const float* v, float angle_a, float angle_b) {
    set_rotated_axis(joint, v, angle_a, 0xf0);
    set_rotated_axis(joint, v, angle_b, 0x100);
}

// +0xa0, +0xb0, +0xe0 = the normalized a, b, c; flag 0x10 with the frame axes +0xc0/+0xd0; then
// the limit axes from v.
void XB_THISCALL PhysJoint_SetupAxes10(uint8_t* joint, const float* a, const float* b, const float* c, const float* v,
                                       float angle_a, float angle_b) {
    set_normalized(joint + 0xa0, a);
    set_normalized(joint + 0xb0, b);
    set_normalized(joint + 0xe0, c);
    at<uint32_t>(joint, 0x30) |= 0x10;
    set_frame_axes(joint);
    set_rotated_axis(joint, v, angle_a, 0xf0);
    set_rotated_axis(joint, v, angle_b, 0x100);
}

// As PhysJoint_SetupAxes10 with flag 0x20 and no frame axes.
void XB_THISCALL PhysJoint_SetupAxes20(uint8_t* joint, const float* a, const float* b, const float* c, const float* v,
                                       float angle_a, float angle_b) {
    set_normalized(joint + 0xa0, a);
    set_normalized(joint + 0xb0, b);
    set_normalized(joint + 0xe0, c);
    at<uint32_t>(joint, 0x30) |= 0x20;
    set_rotated_axis(joint, v, angle_a, 0xf0);
    set_rotated_axis(joint, v, angle_b, 0x100);
}

// Joint limit flags in +0x30. With flag 0x200 all are set (0xc0 and one bit per angle limit).
// Else, with flag 0x20, bit i per angle limit i (+0x190, 0x20 each) while its cos +0x10 + 0.05
// is at least the rotated limit axis . the rotated +0xb0, and body A's +0xe0 axis is turned by
// the rotation between the rotated +0xa0 and +0xb0; flag 0x40 while (axis x B's +0xf0) . the
// rotated +0xb0 is at least -0.05, flag 0x80 while 0.05 is at least (axis x B's +0x100) . it.
void XB_THISCALL PhysJoint_UpdateLimitFlags(uint8_t* joint, uint32_t) {
    if (at<uint32_t>(joint, 0x30) & 0x200) {
        at<uint32_t>(joint, 0x30) |= 0xc0;
        for (int32_t i = 0; i < at<int32_t>(joint, 0x194); ++i) at<uint32_t>(joint, 0x30) |= 1u << i;
        return;
    }
    alignas(16) float a[4], b[4], c[4], d[4];
    PhysBody_RotateVector2(a, at<uint8_t*>(joint, 4), reinterpret_cast<float*>(joint + 0xf0));
    PhysBody_RotateVector2(b, at<uint8_t*>(joint, 4), reinterpret_cast<float*>(joint + 0x100));
    PhysBody_RotateVector2(c, at<uint8_t*>(joint, 4), reinterpret_cast<float*>(joint + 0xb0));
    PhysBody_RotateVector2(d, at<uint8_t*>(joint, 0), reinterpret_cast<float*>(joint + 0xe0));
    Lanes axis;
    if (at<uint8_t>(joint, 0x30) & 0x20) {
        for (int32_t i = 0; i < at<int32_t>(joint, 0x194); ++i) {
            const float* const limit = reinterpret_cast<float*>(at<uint8_t*>(joint, 0x190) + static_cast<uint32_t>(i) * 0x20);
            alignas(16) float r[4];
            const float* const rotated = PhysBody_RotateVector2(r, at<uint8_t*>(joint, 0), limit);
            const float dot = dot3(lanes(c), lanes(rotated));
            if (limit[4] + title_global<float>(0x007cd884) >= dot)  // 0.05
                at<uint32_t>(joint, 0x30) |= 1u << i;
            else
                at<uint32_t>(joint, 0x30) &= ~(1u << i);
        }
        alignas(16) float cc[4], e[4], ec[4];
        std::memcpy(cc, c, 16);
        PhysBody_RotateVector2(e, at<uint8_t*>(joint, 0), reinterpret_cast<float*>(joint + 0xa0));
        std::memcpy(ec, e, 16);
        alignas(16) float m[16];
        Mat34_RotationBetweenVectors(m, ec, cc);
        axis = mat_rotate(m, lanes(d));
    } else {
        axis = lanes(d);
    }
    uint32_t flags = at<uint32_t>(joint, 0x30);
    const float s1 = dot3(cross_lanes(axis, lanes(a)), lanes(c));
    flags = s1 >= title_global<float>(0x0083f0f4) ? flags | 0x40 : flags & ~0x40u;  // -0.05
    const float s2 = dot3(cross_lanes(axis, lanes(b)), lanes(c));
    at<uint32_t>(joint, 0x30) = flags;
    at<uint32_t>(joint, 0x30) = title_global<float>(0x007cd884) >= s2 ? flags | 0x80 : flags & ~0x80u;
}

// Two contacts (normals a4 then a5; a6 then a6 + 8) between the same bodies and points, each
// with the range -1e7..1e7 and +0x7c = separation / max(dt, 1/240) * -0.5 (x87).
void XB_THISCALL PhysContacts_AddPair(uint8_t* world, uint32_t a0, const float* v1, uint32_t a2, const float* v3,
                                      uint32_t a4, uint32_t a5, uint32_t a6, float dt) {
    for (int32_t pass = 0; pass < 2; ++pass) {
        alignas(16) float c3[4], c1[4];
        std::memcpy(c3, v3, 16);
        std::memcpy(c1, v1, 16);
        uint8_t* const contact =
            PhysContacts_Add(world, a0, word_of(c1), a2, word_of(c3), pass ? a5 : a4, pass ? a6 + 8 : a6);
        at<float>(contact, 0x60) = title_global<float>(0x0083eb74);  // -1e7
        at<float>(contact, 0x64) = title_global<float>(0x0083eb70);  // 1e7
        const float min_dt = title_global<float>(0x007cd880);
        const float d = dt > min_dt ? dt : min_dt;
        const long double ratio = title_function<long double XB_THISCALL(uint8_t*)>(0x000bcc60)(contact) / d;
        at<float>(contact, 0x78) = 0.0f;
        at<float>(contact, 0x80) = 0.0f;
        at<float>(contact, 0x7c) = static_cast<float>(ratio * title_global<float>(0x0083eb28));  // -0.5
    }
}

namespace {

// |v| from a squared length, the title's way: 0 (and NaN) stay as they are, else the RSQRTSS
// estimate with one Newton step, ((3 - l r r) * l r) * 0.5.
float newton_length(float l2) {
    if (l2 == 0.0f || l2 != l2) return l2;
    const float r = platform_rsqrt_estimate(l2);
    const float a = l2 * r;
    const float b = a * r;
    return ((title_global<float>(0x007cd630) - b) * a) * title_global<float>(0x007cd640);
}

float dot_sum(const Lanes& a, const Lanes& b) { return dot3(a, b); }

}  // namespace

// All contacts of a joint for one step: the anchor contact (0x140 record, +0x34); with flag 0x100
// a distance contact between the bodies' +0xe0 points (+0x4c; flag 0x800: a soft one from +0x1a0);
// with flag 0x10 the axis pair (+0x54) and, with 0xc0, the axis-limit contacts; with flag 0x20
// one contact per active angle limit (unless 0x400; +0x74 + 8 i) and, with 0xc0, the axis-limit
// contacts of the twisted frame.
void XB_THISCALL PhysJoint_AddContacts(uint8_t* joint, uint8_t* world, float dt) {
    {
        uint8_t* const body_b = at<uint8_t*>(joint, 4);
        uint8_t* const body_a = at<uint8_t*>(joint, 0);
        alignas(16) float rb[4], ra[4];
        const float* const b = PhysBody_RotateVector(rb, body_b, reinterpret_cast<float*>(joint + 0x20));
        const float* const a = PhysBody_RotateVector(ra, body_a, reinterpret_cast<float*>(joint + 0x10));
        PhysWorld_AddContact140(world, word_of(body_a), word_of(a), word_of(body_b), word_of(b), word_of(joint + 0x34), dt);
    }
    if (at<uint32_t>(joint, 0x30) & 0x100) {
        uint8_t* const body_b = at<uint8_t*>(joint, 4);
        uint8_t* const body_a = at<uint8_t*>(joint, 0);
        const Lanes d = sub(lanes(reinterpret_cast<float*>(body_b + 0xe0)), lanes(reinterpret_cast<float*>(body_a + 0xe0)));
        const float len = newton_length(dot_sum(d, d));
        alignas(16) float n[4];
        if (len > title_global<float>(0x0083ed50)) {  // 1e-4
            const float inv = title_global<float>(0x0083eb20) / len;
            std::memcpy(n, mul(d, splat(inv)).v, 16);
        } else {
            const Lanes fallback = lanes(&title_global<float>(0x007cd6b0));
            std::memcpy(n, mul(fallback, splat(title_global<float>(0x007cd680))).v, 16);
        }
        alignas(16) uint32_t zero_a[4], zero_b[4];
        for (uint32_t i = 0; i < 4; ++i) zero_a[i] = zero_b[i] = title_global<uint32_t>(0x007cd650 + i * 4);
        uint8_t* const contact = PhysContacts_Add(world, word_of(body_a), word_of(zero_b), word_of(body_b), word_of(zero_a),
                                                  word_of(n), word_of(joint + 0x4c));
        if (at<uint32_t>(joint, 0x30) & 0x800) {
            at<float>(contact, 0x60) = title_global<float>(0x0083eb74);  // -1e7
            at<float>(contact, 0x64) = title_global<float>(0x0083eb70);  // 1e7
            const float inv = title_global<float>(0x0083eb20) / (at<float>(joint, 0x1a0) * dt);
            at<float>(contact, 0x78) = 0.0f;
            at<float>(contact, 0x7c) = 0.0f;
            const float sum = at<float>(contact, 0x84) + inv;
            at<float>(contact, 0x80) = inv;
            at<float>(contact, 0x84) = sum;
        } else {
            at<float>(contact, 0x78) = 0.0f;
            at<float>(contact, 0x7c) = 0.0f;
            at<float>(contact, 0x80) = 0.0f;
            const float range = at<float>(joint, 0x1a0) * dt;
            at<float>(contact, 0x60) = 0.0f - range;
            at<float>(contact, 0x64) = range;
        }
    }
    alignas(16) float p[4], q[4];
    PhysBody_RotateVector(p, at<uint8_t*>(joint, 0), reinterpret_cast<float*>(joint + 0xa0));
    PhysBody_RotateVector(q, at<uint8_t*>(joint, 4), reinterpret_cast<float*>(joint + 0xb0));
    if (at<uint8_t>(joint, 0x30) & 0x10) {
        uint8_t* const body_a = at<uint8_t*>(joint, 0);
        uint8_t* const body_b = at<uint8_t*>(joint, 4);
        alignas(16) float r1[4], r2[4];
        const float* const n1 = PhysBody_RotateVector(r1, body_a, reinterpret_cast<float*>(joint + 0xd0));
        const float* const n2 = PhysBody_RotateVector(r2, body_a, reinterpret_cast<float*>(joint + 0xc0));
        PhysContacts_AddPair(world, word_of(body_a), p, word_of(body_b), q, word_of(n2), word_of(n1), word_of(joint + 0x54),
                             dt);
        if (at<uint8_t>(joint, 0x30) & 0xc0) {
            alignas(16) float r3[4];
            const float* const axis = PhysBody_RotateVector(r3, at<uint8_t*>(joint, 0), reinterpret_cast<float*>(joint + 0xe0));
            PhysJoint_AddAxisContacts(joint, world, axis, q, dt);
        }
    }
    const uint32_t flags = at<uint32_t>(joint, 0x30);
    if (!(flags & 0x20)) return;
    if (!(flags & 0x400)) {
        for (int32_t i = 0; i < at<int32_t>(joint, 0x194); ++i) {
            const float* const limit = reinterpret_cast<float*>(at<uint8_t*>(joint, 0x190) + static_cast<uint32_t>(i) * 0x20);
            if (!(at<uint32_t>(joint, 0x30) & (1u << i))) continue;
            uint8_t* const body_a = at<uint8_t*>(joint, 0);
            alignas(16) float rl[4];
            PhysBody_RotateVector(rl, body_a, limit);
            const Lanes r = lanes(rl);
            const Lanes qv = lanes(q);
            const float along = dot_sum(qv, r);
            const Lanes perp = sub(qv, mul(r, splat(along)));
            const float len = newton_length(dot_sum(perp, perp));
            if (title_global<float>(0x0083ed50) > len) continue;
            const Lanes cos_part = mul(splat(limit[4]), r);
            const float s = limit[5] / len;
            alignas(16) float t[4];
            const Lanes tv = add(mul(splat(s), perp), cos_part);
            std::memcpy(t, tv.v, 16);
            const Lanes cross = cross_lanes(tv, r);
            const float inv = title_global<float>(0x0083eb20) / limit[5];
            alignas(16) float w[4], qc[4];
            std::memcpy(w, mul(splat(inv), cross).v, 16);
            std::memcpy(qc, q, 16);
            uint8_t* const contact = PhysContacts_Add(world, word_of(body_a), word_of(t), at<uint32_t>(joint, 4), word_of(qc),
                                                      word_of(w), word_of(joint + 0x74 + static_cast<uint32_t>(i) * 8));
            at<float>(contact, 0x60) = title_global<float>(0x0083eb74);  // -1e7
            at<float>(contact, 0x64) = 0.0f;
            PhysContact_ComputeBias(contact, dt, 5.0f);
        }
    }
    if (!(at<uint8_t>(joint, 0x30) & 0xc0)) return;
    alignas(16) float qc[4], pc[4], m[16];
    std::memcpy(qc, q, 16);
    std::memcpy(pc, p, 16);
    Mat34_RotationBetweenVectors(m, pc, qc);
    alignas(16) float r[4];
    const float* const axis = PhysBody_RotateVector(r, at<uint8_t*>(joint, 0), reinterpret_cast<float*>(joint + 0xe0));
    alignas(16) float turned[4];
    std::memcpy(turned, mat_rotate(m, lanes(axis)).v, 16);
    PhysJoint_AddAxisContacts(joint, world, turned, q, dt);
}

namespace {

Lanes negate_bits(const Lanes& v) {
    Lanes out;
    for (int32_t i = 0; i < 4; ++i) {
        uint32_t bits;
        std::memcpy(&bits, &v.v[i], 4);
        bits ^= 0x80000000u;
        std::memcpy(&out.v[i], &bits, 4);
    }
    return out;
}

using ContactInit = void XB_THISCALL(uint8_t*, uint8_t*, const float*, uint8_t*, const float*, const float*, uint8_t*,
                                     const float*);

void contact_init(uint8_t* contact, uint8_t* a, const float* pa, uint8_t* b, const float* pb, const float* n,
                  uint8_t* extra, const float* w) {
    title_function<ContactInit>(0x000bddd0)(contact, a, pa, b, pb, n, extra, w);  // PhysContact_Init
}

// newton_length with the constants at 0x007cd390 / 0x007cd3a0 (3, 0.5).
float newton_length_3b0(float l2) {
    if (l2 == 0.0f || l2 != l2) return l2;
    const float r = platform_rsqrt_estimate(l2);
    const float a = l2 * r;
    const float b = a * r;
    return ((title_global<float>(0x007cd390) - b) * a) * title_global<float>(0x007cd3a0);
}

}  // namespace

// Contacts of a two-body slider constraint (flag 1 of +0xa4; flag 4 cleared, +0xc8..+0xd0 and +0x90
// reset first): an optional end contact (flag 2), the slide contact (0xc0 record at +0xc8), its
// perpendicular pair (+0xcc, +0xd0: a free one when the drive mode +0xa0 has no limit left, else a
// limit or motor contact) and the projected speed +0x90.
void XB_THISCALL PhysConstraint_AddContacts(uint8_t* c, uint8_t* world, float dt) {
    at<uint32_t>(c, 0xc8) = 0;
    at<uint32_t>(c, 0xcc) = 0;
    at<uint32_t>(c, 0xd0) = 0;
    const uint32_t flags = at<uint32_t>(c, 0xa4) & ~4u;
    at<float>(c, 0x90) = 0.0f;
    at<uint32_t>(c, 0xa4) = flags;
    if (!(flags & 1)) return;
    uint8_t* const body_a = at<uint8_t*>(c, 0);
    alignas(16) float axis[4], anchor_a_rot[4], anchor_a[4], anchor_b[4], point_b[4], point_a[4];
    PhysBody_RotateVector(axis, body_a, reinterpret_cast<float*>(c + 0x40));
    const Lanes half = mul(splat(at<float>(c, 0x60)), lanes(axis));
    PhysBody_RotateVector(anchor_a_rot, body_a, reinterpret_cast<float*>(c + 0x30));
    uint8_t* const body_b = at<uint8_t*>(c, 4);
    std::memcpy(anchor_a, add(lanes(anchor_a_rot), half).v, 16);
    PhysBody_RotateVector(anchor_b, body_b, reinterpret_cast<float*>(c + 0x10));
    const float* const pb = PhysBody_OffsetByPosition(point_b, body_b, anchor_b);
    const float* const pa = PhysBody_OffsetByPosition(point_a, body_a, anchor_a);
    const Lanes d = sub(lanes(pa), lanes(pb));
    if (at<uint8_t>(c, 0xa4) & 2) {
        alignas(16) float r[4], neg[4], zero[4], end[4];
        const float* const rp = PhysBody_RotateVector(r, body_b, reinterpret_cast<float*>(c + 0x20));
        std::memcpy(neg, negate_bits(lanes(rp)).v, 16);
        uint8_t* const contact = PhysWorld_AllocContact(world);
        for (uint32_t i = 0; i < 4; ++i) reinterpret_cast<uint32_t*>(zero)[i] = title_global<uint32_t>(0x007cd3b0 + i * 4);
        std::memcpy(end, sub(lanes(anchor_a), mul(splat(at<float>(c, 0x74)), lanes(axis))).v, 16);
        contact_init(contact, at<uint8_t*>(c, 0), end, at<uint8_t*>(c, 4), anchor_b, neg, c + 0xa8, zero);
        at<float>(contact, 0x60) = title_global<float>(0x0083eb74);  // -1e7
        at<float>(contact, 0x64) = 0.0f;
        PhysContact_ComputeBias_2(contact, dt, 5.0f);
    }
    alignas(16) float r2[4];
    const float* const r2p = PhysBody_RotateVector(r2, at<uint8_t*>(c, 4), reinterpret_cast<float*>(c + 0x20));
    const Lanes n = negate_bits(lanes(r2p));
    const float along = dot3(d, n);
    const Lanes shift = mul(splat(along), n);
    alignas(16) float normal[4], slide_a[4], slide_b[4], w[4];
    std::memcpy(normal, n.v, 16);
    std::memcpy(slide_a, sub(lanes(anchor_a), shift).v, 16);
    std::memcpy(slide_b, sub(lanes(anchor_b), shift).v, 16);
    std::memcpy(w, mul(splat(0.0f - at<float>(c, 0x78)), lanes(axis)).v, 16);
    uint8_t* const record = PhysWorld_AllocContactC0(world);
    at<uint32_t>(record, 0xb0) = 0;
    at<uint32_t>(record, 0xb4) = 0;
    at<uint8_t*>(c, 0xc8) = record + 0x10;
    contact_init(record + 0x10, at<uint8_t*>(c, 0), slide_a, at<uint8_t*>(c, 4), slide_b, normal, c + 0xb0, w);
    uint8_t* const slide = at<uint8_t*>(c, 0xc8);
    at<float>(slide, 0x60) = title_global<float>(0x0083eb74);
    at<float>(slide, 0x64) = 0.0f;
    const float k = at<float>(c, 0x6c) * dt;
    const float inv = title_global<float>(0x0083eb20) / (at<float>(c, 0x70) * dt + k * dt);
    uint8_t* const p = at<uint8_t*>(c, 0xc8);
    const long double separation = title_function<long double XB_THISCALL(uint8_t*)>(0x000bc510)(p);  // PhysContact_GetSeparation
    const long double scaled = separation * ((static_cast<long double>(k) * inv) * dt);
    const long double rate = static_cast<long double>(title_global<float>(0x0083eb34)) / dt;  // -1
    at<float>(p, 0x7c) = 0.0f;
    const float sum = at<float>(p, 0x84) + inv;
    at<float>(p, 0x80) = inv;
    at<float>(p, 0x84) = sum;
    at<float>(p, 0x78) = static_cast<float>(scaled * rate);
    alignas(16) float u[4];
    PhysBody_RotateVector(u, at<uint8_t*>(c, 0), reinterpret_cast<float*>(c + 0x50));
    const Lanes uv = lanes(u);
    const float u_along = dot3(uv, n);
    const Lanes perp = sub(uv, mul(n, splat(u_along)));
    const float len = newton_length_3b0(dot3(perp, perp));
    if (!(len > title_global<float>(0x0083eb4c))) return;  // 0.001
    alignas(16) float side[4];
    std::memcpy(side, mul(perp, splat(title_global<float>(0x0083eb20) / len)).v, 16);
    const uint32_t buffer_b0 = PhysContact_AllocBufferB0(record, world);
    uint8_t* const q = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(buffer_b0));
    at<uint8_t*>(c, 0xcc) = q;
    contact_init(q, at<uint8_t*>(c, 0), slide_a, at<uint8_t*>(c, 4), slide_b, side, c + 0xb8, w);
    uint8_t* const qc = at<uint8_t*>(c, 0xcc);
    at<float>(qc, 0x78) = 0.0f;
    at<float>(qc, 0x7c) = 0.0f;
    at<float>(qc, 0x80) = 0.0f;
    alignas(16) float twist[4];
    std::memcpy(twist, negate_bits(cross_lanes(lanes(side), n)).v, 16);
    alignas(16) float t1[4];
    using Vector = const float* XB_THISCALL(uint8_t*, float*);
    const float* const v1 = title_function<Vector>(0x000bca50)(at<uint8_t*>(c, 0xc8), t1);
    const Lanes prod = mul(lanes(v1), lanes(twist));
    platform_keep(prod);
    at<float>(c, 0x8c) = ((prod.v[0] + prod.v[1]) + prod.v[2]) / at<float>(c, 0x60);
    const int32_t mode = at<int32_t>(c, 0xa0);
    const bool free_end = mode == 0 ? title_global<float>(0x0083ed50) > at<float>(c, 0x84)
                                    : mode == 1 && title_global<float>(0x0083ed50) > at<float>(c, 0x88);
    if (free_end) {
        uint8_t* const slide_contact = at<uint8_t*>(c, 0xc8);
        uint8_t* const free_contact = at<uint8_t*>(c, 0xcc);
        const uint32_t bits = at<uint32_t>(free_contact, 0x8c) | 1;
        at<uint32_t>(free_contact, 0x88) = at<uint32_t>(c, 0x68);
        at<uint32_t>(free_contact, 0x8c) = bits;
        at<uint8_t*>(free_contact, 0x90) = slide_contact;
        at<float>(free_contact, 0x60) = 0.0f;
        at<float>(free_contact, 0x64) = 0.0f;
        return;
    }
    at<float>(at<uint8_t*>(c, 0xcc), 0x60) = title_global<float>(0x0083eb74);  // -1e7
    at<float>(at<uint8_t*>(c, 0xcc), 0x64) = title_global<float>(0x0083eb70);  // 1e7
    uint8_t* const t = reinterpret_cast<uint8_t*>(static_cast<uintptr_t>(PhysContact_AllocBufferB4(record, world)));
    at<uint8_t*>(c, 0xd0) = t;
    contact_init(t, at<uint8_t*>(c, 0), slide_a, at<uint8_t*>(c, 4), slide_b, twist, c + 0xc0, w);
    uint8_t* const tc = at<uint8_t*>(c, 0xd0);
    if (at<int32_t>(c, 0xa0) == 0) {
        const float stiffness = (at<float>(c, 0x84) * at<float>(c, 0x7c)) * dt;
        const float target = at<float>(c, 0x80) * at<float>(c, 0x7c);
        const float length2 = at<float>(c, 0x60) * at<float>(c, 0x60);
        const float soft = title_global<float>(0x0083eb20) / (stiffness / length2);
        const float sum = at<float>(tc, 0x84) + soft;
        at<float>(tc, 0x78) = at<float>(c, 0x60) * target;
        at<float>(tc, 0x7c) = 0.0f;
        at<float>(tc, 0x80) = soft;
        at<float>(tc, 0x84) = sum;
        uint8_t* const tl = at<uint8_t*>(c, 0xd0);
        if (target > title_global<float>(0x0083ed50)) {  // 1e-4
            at<float>(tl, 0x60) = 0.0f;
            at<float>(tl, 0x64) = title_global<float>(0x0083eb70);
        } else {
            at<float>(tl, 0x60) = title_global<float>(0x0083eb74);
            at<float>(tl, 0x64) = title_global<float>(0x007cd5e4) > target ? 0.0f : title_global<float>(0x0083eb70);  // -1e-4
        }
    } else {
        at<float>(tc, 0x78) = 0.0f;
        at<float>(tc, 0x7c) = 0.0f;
        at<float>(tc, 0x80) = 0.0f;
        const float x = at<float>(c, 0x88) * dt;
        uint8_t* const tl = at<uint8_t*>(c, 0xd0);
        at<float>(tl, 0x60) = 0.0f - x;
        at<float>(tl, 0x64) = x;
    }
    at<uint32_t>(at<uint8_t*>(record, 0xb0), 0x88) = at<uint32_t>(c, 0x68);
    at<float>(at<uint8_t*>(record, 0xb4), 0x88) = at<float>(c, 0x64);
    alignas(16) float t2[4];
    const float* const v2 = title_function<Vector>(0x000bc990)(at<uint8_t*>(c, 0xd0), t2);
    const Lanes prod2 = mul(lanes(v2), lanes(twist));
    platform_keep(prod2);
    at<float>(c, 0x90) = (prod2.v[0] + prod2.v[1]) + prod2.v[2];
}

// Segment a-b against the box (centre +0x10, extents +0x20) in the frame: both ends moved into
// it, rejected when their bounds miss the box; a near-zero segment tests a alone; else the six
// face planes (normals 0x007cc7d0, 0x007cc7c0, 0x007cc7b0, each through the low then the high
// corner) are crossed and the crossing point checked against the face's other two axes (the
// original's test needs both of them outside on one side to reject). Unreferenced.
uint8_t XB_CDECL PhysBox_IntersectSegmentInFrame(const uint8_t* box, const float* frame, const float* a, const float* b) {
    float rows[12];
    alignas(16) float a_copy[4];
    std::memcpy(a_copy, a, 16);
    std::memcpy(rows, frame, sizeof(rows));
    Lanes columns[3];
    frame_columns(rows, columns);
    const Lanes origin = lanes(frame + 12);
    const Lanes la = rotate_into(columns, sub(lanes(a_copy), origin));
    alignas(16) float b_copy[4];
    std::memcpy(b_copy, b, 16);
    std::memcpy(rows, frame, sizeof(rows));
    frame_columns(rows, columns);
    const Lanes lb = rotate_into(columns, sub(lanes(b_copy), origin));
    const Lanes centre = lanes(reinterpret_cast<const float*>(box + 0x10));
    const Lanes extent = lanes(reinterpret_cast<const float*>(box + 0x20));
    alignas(16) float lo[4], hi[4], start[4];
    std::memcpy(lo, sub(centre, extent).v, 16);
    if ((less_mask(lanes(lo), max_lanes(la, lb)) & 7) != 7) return 0;
    std::memcpy(hi, add(centre, extent).v, 16);
    if ((less_mask(min_lanes(la, lb), lanes(hi)) & 7) != 7) return 0;
    const Lanes dir = sub(lb, la);
    const float length2 = dot3(dir, dir);
    if (title_global<float>(0x0083ed50) > length2) return PhysBox_ContainsPointInFrame(box, frame, a);  // 1e-4
    const float length = platform_x87_sqrt(length2);
    const Lanes unit = div(dir, splat(length));
    platform_keep(unit);
    alignas(16) float unit_v[4];
    std::memcpy(unit_v, unit.v, 16);
    std::memcpy(start, la.v, 16);
    const Lanes one = splat(title_global<float>(0x007cc790));
    static const uint32_t kNormals[3] = {0x007cc7d0, 0x007cc7c0, 0x007cc7b0};
    static const uint32_t kAxes[3] = {3, 5, 6};
    for (int32_t face = 0; face < 6; ++face) {
        alignas(16) float normal[4];
        std::memcpy(normal, mul(lanes(&title_global<float>(kNormals[face / 2])), one).v, 16);
        const float* const corner = (face & 1) ? hi : lo;
        const float t = RayPlaneIntersectFrac(corner, normal, start, unit_v);
        if (!platform_x87_greater_equal(t, title_global<float>(0x0083eafc))) continue;
        if (!(length >= t)) continue;
        const Lanes p = add(la, mul(splat(t), unit));
        const uint32_t axes = kAxes[face / 2];
        if ((less_mask(lanes(hi), p) & axes) == axes) continue;
        if ((less_mask(p, lanes(lo)) & axes) != axes) return 1;
    }
    return 0;
}


// Push 6: shape contacts, hinges, distance joints, contact helpers.

namespace {

// newton_length with the constants at 0x007cd8d0 / 0x007cd8e0 (3, 0.5).
float newton_length_8d0(float l2) {
    if (l2 == 0.0f || l2 != l2) return l2;
    const float r = platform_rsqrt_estimate(l2);
    const float a = l2 * r;
    const float b = a * r;
    return ((title_global<float>(0x007cd8d0) - b) * a) * title_global<float>(0x007cd8e0);
}

// The zero vector at 0x007cd8f0, copied as words.
void zero_vector_8f0(uint32_t* out) {
    for (uint32_t i = 0; i < 4; ++i) out[i] = title_global<uint32_t>(0x007cd8f0 + i * 4);
}

using ContactVector = const float* XB_THISCALL(uint8_t*, float*);

}  // namespace

// Contact points of two convex shapes (A in `frame`, B in its own space): GJK closest points
// (out = point on A, out + 4 on B, out + 8 = separation), moved apart by 0.03 along the unit
// separation. Touching shapes: B's sphere test (vtable +0x10) of A's bounding sphere gives a
// push-out normal; A's frame is pushed along it (at most 6 steps) until GJK separates them,
// restarting with the new direction while it still turns (dot <= 0.998 and above the best one
// - 0.005). 1 = points found, 0 = none. Dead: no callers, no stored pointer, no immediate.
uint8_t XB_CDECL Phys_ShapeContact(void* shape_a, const float* frame, void* shape_b, float* out, uint32_t, uint32_t,
                                   uint32_t, uint32_t) {
    float* const sep = out + 8;
    alignas(16) float points[12];
    copy_words(points + 8, sep);
    const int32_t first = Gjk_ClosestPoints(shape_a, frame, shape_b, points, title_global<float>(0x008bd430), 0);
    copy_words(sep, points + 8);
    if (first == 0) return 0;
    if (first == 1) {
        const float sq = self_dot(lanes(sep));
        if (sq > title_global<float>(0x008bd434)) {
            const float len = platform_x87_sqrt(sq);
            store4(sep, div(lanes(sep), splat(len)));
            const Lanes offset = mul(lanes(sep), splat(title_global<float>(0x007cce08)));  // 0.03
            store4(out, sub(lanes(points), offset));
            store4(out + 4, add(lanes(points + 4), offset));
            return 1;
        }
    }
    alignas(16) float moved[16];
    for (int32_t i = 0; i < 4; ++i) copy_words(moved + i * 4, frame + i * 4);
    const uint8_t* const a = static_cast<const uint8_t*>(shape_a);
    const uint32_t radius = at<uint32_t>(a, 0x1c);
    alignas(16) float centre[4];
    store4(centre, transform_out(frame, reinterpret_cast<const float*>(a + 0x10)));
    alignas(16) float hit[8] = {};
    uint32_t unused = 0xffffffffu;
    void* const* vtable = *static_cast<void* const* const*>(shape_b);
    const uint8_t touching = title_function<ShapeIntersect>(reinterpret_cast<uintptr_t>(vtable[4]))(
        shape_b, centre, radius, hit, word_of(&unused));
    copy_words(sep, hit + 4);
    if (!touching) return 0;
    float best = title_global<float>(0x007cce18);  // -1e10
    alignas(16) float origin[4];
    for (;;) {
        int32_t steps = 0;
        float sq;
        for (;;) {
            store4(origin, add(lanes(moved + 12), lanes(hit + 4)));
            copy_words(moved + 12, origin);
            copy_words(points + 8, sep);
            const int32_t r = Gjk_ClosestPoints(shape_a, moved, shape_b, points, title_global<float>(0x008bd430), 1);
            copy_words(sep, points + 8);
            sq = self_dot(lanes(sep));
            if (r == 0) break;
            if (++steps > 5) return 0;
        }
        const float t = static_cast<float>(steps + 1) + title_global<float>(0x007cce00);  // 0.1
        if (sq > t * t) return 0;
        const float len = platform_x87_sqrt(sq);
        store4(sep, div(lanes(sep), splat(len)));
        const float d = dot3(lanes(sep), lanes(hit + 4));
        if (d > title_global<float>(0x007cce14) || best > d - title_global<float>(0x0083eb88)) {  // 0.998, 0.005
            const Lanes offset = mul(lanes(sep), splat(title_global<float>(0x007cce08)));
            const Lanes shift = sub(sub(lanes(frame + 12), lanes(origin)), offset);
            store4(out, add(lanes(points), shift));
            copy_words(out + 4, points + 4);
            return 1;
        }
        copy_words(hit + 4, sep);
        copy_words(moved + 12, frame + 12);
        best = d;
    }
}

// Hinge limit flags in +0xc: bit 0 while (A x B) . D >= -0.05, bit 1 while 0.05 >= (A x C) . D,
// A = body 0's +0x70, B, C, D = body 1's +0x80, +0x90, +0x40 (second rotation).
void XB_THISCALL PhysHinge_UpdateLimitFlags(uint8_t* hinge, uint32_t) {
    alignas(16) float a[4], b[4], c[4], d[4];
    PhysBody_RotateVector2(a, at<uint8_t*>(hinge, 0), reinterpret_cast<float*>(hinge + 0x70));
    PhysBody_RotateVector2(b, at<uint8_t*>(hinge, 4), reinterpret_cast<float*>(hinge + 0x80));
    PhysBody_RotateVector2(c, at<uint8_t*>(hinge, 4), reinterpret_cast<float*>(hinge + 0x90));
    PhysBody_RotateVector2(d, at<uint8_t*>(hinge, 4), reinterpret_cast<float*>(hinge + 0x40));
    uint32_t flags = at<uint32_t>(hinge, 0xc);
    const float s1 = dot3(cross_lanes(lanes(a), lanes(b)), lanes(d));
    flags = s1 >= title_global<float>(0x0083f0f4) ? flags | 1 : flags & ~1u;  // -0.05
    const float s2 = dot3(cross_lanes(lanes(a), lanes(c)), lanes(d));
    at<uint32_t>(hinge, 0xc) = flags;
    at<uint32_t>(hinge, 0xc) = title_global<float>(0x0083edc8) >= s2 ? flags | 2 : flags & ~2u;  // 0.05
}

// Hinge contacts for one step: the anchor contact (+0x10 / +0x20, 0x140 record +0xa4), the axis
// pair (body 0's +0x30 against body 1's +0x40, normals +0x50 / +0x60, +0xc4), with +0xa0 > 1e-5 a
// friction contact about the axis (+0xbc, range +-(+0xa0 dt)), and per limit flag a one-sided
// contact (body 0's +0x70 against body 1's +0x80 / +0x90, +0xd4 / +0xdc).
void XB_THISCALL PhysHinge_AddContacts(uint8_t* hinge, uint8_t* world, float dt) {
    {
        uint8_t* const body_b = at<uint8_t*>(hinge, 4);
        uint8_t* const body_a = at<uint8_t*>(hinge, 0);
        alignas(16) float rb[4], ra[4];
        const float* const b = PhysBody_RotateVector(rb, body_b, reinterpret_cast<float*>(hinge + 0x20));
        const float* const a = PhysBody_RotateVector(ra, body_a, reinterpret_cast<float*>(hinge + 0x10));
        PhysWorld_AddContact140(world, word_of(body_a), word_of(a), word_of(body_b), word_of(b), word_of(hinge + 0xa4), dt);
    }
    uint8_t* const body_a = at<uint8_t*>(hinge, 0);
    alignas(16) float p[4], q[4];
    PhysBody_RotateVector(p, body_a, reinterpret_cast<float*>(hinge + 0x30));
    PhysBody_RotateVector(q, at<uint8_t*>(hinge, 4), reinterpret_cast<float*>(hinge + 0x40));
    {
        alignas(16) float r1[4], r2[4];
        const float* const n1 = PhysBody_RotateVector(r1, body_a, reinterpret_cast<float*>(hinge + 0x60));
        const float* const n2 = PhysBody_RotateVector(r2, body_a, reinterpret_cast<float*>(hinge + 0x50));
        PhysContacts_AddPair(world, word_of(body_a), p, at<uint32_t>(hinge, 4), q, word_of(n2), word_of(n1),
                             word_of(hinge + 0xc4), dt);
    }
    if (at<float>(hinge, 0xa0) > title_global<float>(0x0083effc)) {  // 1e-5
        alignas(16) uint32_t zero_a[4], zero_b[4];
        zero_vector_8f0(zero_a);
        zero_vector_8f0(zero_b);
        uint8_t* const contact = PhysContacts_Add(world, at<uint32_t>(hinge, 0), word_of(zero_b), at<uint32_t>(hinge, 4),
                                                  word_of(zero_a), word_of(q), word_of(hinge + 0xbc));
        at<float>(contact, 0x78) = 0.0f;
        at<float>(contact, 0x7c) = 0.0f;
        at<float>(contact, 0x80) = 0.0f;
        const float range = at<float>(hinge, 0xa0) * dt;
        at<float>(contact, 0x60) = 0.0f - range;
        at<float>(contact, 0x64) = range;
    }
    const uint32_t flags = at<uint32_t>(hinge, 0xc);
    if (!(flags & 1) && !(flags & 2)) return;
    alignas(16) float ref[4];
    PhysBody_RotateVector(ref, at<uint8_t*>(hinge, 0), reinterpret_cast<float*>(hinge + 0x70));
    if (flags & 1) {
        uint8_t* const body_b = at<uint8_t*>(hinge, 4);
        alignas(16) float limit[4], limit_copy[4], ref_copy[4], n[4];
        PhysBody_RotateVector(limit, body_b, reinterpret_cast<float*>(hinge + 0x80));
        copy_words(limit_copy, limit);
        copy_words(ref_copy, ref);
        store4(n, negate_bits(lanes(q)));
        uint8_t* const contact = PhysContacts_Add(world, at<uint32_t>(hinge, 0), word_of(ref_copy), word_of(body_b),
                                                  word_of(limit_copy), word_of(n), word_of(hinge + 0xd4));
        at<float>(contact, 0x60) = title_global<float>(0x0083eb74);  // -1e7
        at<float>(contact, 0x64) = 0.0f;
        PhysContact_ComputeBias(contact, dt, 5.0f);
    }
    if (at<uint8_t>(hinge, 0xc) & 2) {
        uint8_t* const body_b = at<uint8_t*>(hinge, 4);
        alignas(16) float limit[4], limit_copy[4], ref_copy[4];
        PhysBody_RotateVector(limit, body_b, reinterpret_cast<float*>(hinge + 0x90));
        copy_words(limit_copy, limit);
        copy_words(ref_copy, ref);
        uint8_t* const contact = PhysContacts_Add(world, at<uint32_t>(hinge, 0), word_of(ref_copy), word_of(body_b),
                                                  word_of(limit_copy), word_of(q), word_of(hinge + 0xdc));
        at<float>(contact, 0x60) = title_global<float>(0x0083eb74);
        at<float>(contact, 0x64) = 0.0f;
        PhysContact_ComputeBias(contact, dt, 5.0f);
    }
}

// Distance joint contacts (flag 1 of +0x44): between the anchors (+0x10 / +0x20) a contact
// along -n keeping the distance below +0x34 (bias offset -+0x34, +0x78 += +0x3c, +0x48) and,
// with +0x30 > 0.01, one along n (offset +0x30, +0x50); with +0x40 > 1e-5 and the distance
// within +0x34 +- 0.1 a soft tangential contact (1 / (+0x40 dt), +0x58) along the first
// contact's 0x000bca50 / 0x000bc990 vector without its n part; flag 2 picks which anchor is
// taken relative to the other body.
void XB_THISCALL PhysDistanceJoint_AddContacts(uint8_t* joint, uint8_t* world, float dt) {
    if (!(at<uint8_t>(joint, 0x44) & 1)) return;
    uint8_t* const body_a = at<uint8_t*>(joint, 0);
    alignas(16) float ra[4], rb[4], wa[4], wb[4];
    PhysBody_RotateVector(ra, body_a, reinterpret_cast<float*>(joint + 0x10));
    uint8_t* const body_b = at<uint8_t*>(joint, 4);
    PhysBody_RotateVector(rb, body_b, reinterpret_cast<float*>(joint + 0x20));
    PhysBody_OffsetByPosition(wa, body_a, ra);
    PhysBody_OffsetByPosition(wb, body_b, rb);
    const Lanes d = sub(lanes(wb), lanes(wa));
    const float sq = self_dot(d);
    if (title_global<float>(0x0083efdc) > sq) return;  // 1e-6
    const float inv = platform_x87_half_over_sqrt(sq, title_global<float>(0x0083eb20));  // 1 / |d|
    alignas(16) float n[4];
    store4(n, mul(splat(inv), d));
    uint8_t* const first = PhysWorld_AllocContact(world);
    {
        alignas(16) uint32_t zero[4];
        zero_vector_8f0(zero);
        alignas(16) float neg[4];
        store4(neg, negate_bits(lanes(n)));
        contact_init(first, at<uint8_t*>(joint, 0), ra, at<uint8_t*>(joint, 4), rb, neg, joint + 0x48,
                     reinterpret_cast<float*>(zero));
    }
    at<float>(first, 0x60) = title_global<float>(0x0083eb74);  // -1e7
    at<float>(first, 0x64) = 0.0f;
    PhysContact_ComputeBiasWithOffset(first, dt, 0.0f - at<float>(joint, 0x34), 50.0f);
    at<float>(first, 0x78) = at<float>(joint, 0x3c) + at<float>(first, 0x78);
    if (at<float>(joint, 0x30) > title_global<float>(0x0083eb94)) {  // 0.01
        uint8_t* const second = PhysWorld_AllocContact(world);
        alignas(16) uint32_t zero[4];
        zero_vector_8f0(zero);
        contact_init(second, at<uint8_t*>(joint, 0), ra, at<uint8_t*>(joint, 4), rb, n, joint + 0x50,
                     reinterpret_cast<float*>(zero));
        at<float>(second, 0x60) = title_global<float>(0x0083eb74);
        at<float>(second, 0x64) = 0.0f;
        PhysContact_ComputeBiasWithOffset(second, dt, at<float>(joint, 0x30), 50.0f);
    }
    if (!(at<float>(joint, 0x40) > title_global<float>(0x0083effc))) return;  // 1e-5
    const float step = title_global<float>(0x0083ebf8);  // 0.1
    const float lo = at<float>(joint, 0x34) - step;
    if (!(sq >= lo * lo)) return;
    const float hi = at<float>(joint, 0x34) + step;
    if (!(hi * hi >= sq)) return;
    alignas(16) float t[4];
    title_function<ContactVector>(0x000bca50)(first, t);
    Lanes perp = sub(lanes(t), mul(lanes(n), splat(dot3(lanes(t), lanes(n)))));
    float len = newton_length_8d0(self_dot(perp));
    if (title_global<float>(0x0083ed50) > len) {  // 1e-4
        alignas(16) float t2[4], tc[4];
        copy_words(tc, title_function<ContactVector>(0x000bc990)(first, t2));
        perp = sub(lanes(tc), mul(lanes(n), splat(dot3(lanes(tc), lanes(n)))));
        len = newton_length_8d0(self_dot(perp));
    }
    if (!(len > title_global<float>(0x0083ed50))) return;
    alignas(16) float dir[4];
    store4(dir, mul(perp, splat(title_global<float>(0x0083eb20) / len)));
    uint8_t* const soft = PhysWorld_AllocContact(world);
    alignas(16) float pa[4], pb[4], rel[4];
    if (at<uint8_t>(joint, 0x44) & 2) {
        copy_words(pa, PhysBody_WorldToRelative(rel, at<uint8_t*>(joint, 0), wb));
        copy_words(pb, rb);
    } else {
        copy_words(pa, ra);
        copy_words(pb, PhysBody_WorldToRelative(rel, at<uint8_t*>(joint, 4), wa));
    }
    alignas(16) uint32_t zero[4];
    zero_vector_8f0(zero);
    contact_init(soft, at<uint8_t*>(joint, 0), pa, at<uint8_t*>(joint, 4), pb, dir, joint + 0x58,
                 reinterpret_cast<float*>(zero));
    const float rate = title_global<float>(0x0083eb20) / (at<float>(joint, 0x40) * dt);
    at<float>(soft, 0x80) = rate;
    at<float>(soft, 0x84) = rate + at<float>(soft, 0x84);
    at<float>(soft, 0x60) = title_global<float>(0x0083eb74);
    at<float>(soft, 0x78) = 0.0f;
    at<float>(soft, 0x7c) = 0.0f;
    at<float>(soft, 0x64) = title_global<float>(0x0083eb70);  // 1e7
}

// out = p - body +0x30 (p itself for bodies with flag 0x10 in +0x144).
float* XB_CDECL PhysBody_WorldToRelative(float* out, const uint8_t* body, const float* p) {
    if (body[0x144] & 0x10) {
        copy_words(out, p);
        return out;
    }
    store4(out, sub(lanes(p), lanes(reinterpret_cast<const float*>(body + 0x30))));
    return out;
}

// As PhysContact_ComputeBias_2 with the separation + offset and the constants at 0x007cdb14..
// (0.3, 0.02, 1/240): -((separation + offset) - 0.02) / max(dt, 1/240) on the x87.
void XB_THISCALL PhysContact_ComputeBiasWithOffset(uint8_t* contact, float dt, float offset, float limit) {
    const float min_dt = title_global<float>(0x007cdb20);
    if (!(dt > min_dt)) dt = min_dt;
    const long double separation = PhysContact_GetSeparation(contact);
    const float bias = static_cast<float>(-(((separation + offset) - title_global<float>(0x007cdb18)) / dt));
    float& out = at<float>(contact, 0x7c);
    out = bias;
    if (platform_x87_greater(title_global<float>(0x0083eafc), bias)) out = bias * title_global<float>(0x007cdb14);
    const float lowest = 0.0f - limit;
    if (lowest > out) out = lowest;
    const float value = out;
    at<float>(contact, 0x80) = 0.0f;
    if (value >= 0.0f) {
        at<uint32_t>(contact, 0x78) = at<uint32_t>(contact, 0x7c);
        out = 0.0f;
    } else {
        at<float>(contact, 0x78) = 0.0f;
    }
}

// (A's point - B's point) . normal +0x10: A's point = +0x20 + (+0x94 -> +0x34) position, B's
// = +0x30, plus (+0x98 -> +0x34) position when +0x98 is set.
float XB_THISCALL PhysContact_GetSeparation(const uint8_t* contact) {
    const uint8_t* const other = at<uint8_t*>(contact, 0x98);
    Lanes pb;
    if (other)
        pb = add(lanes(reinterpret_cast<const float*>(at<uint8_t*>(other, 0x34) + 0x30)),
                 lanes(reinterpret_cast<const float*>(contact + 0x30)));
    else
        pb = lanes(reinterpret_cast<const float*>(contact + 0x30));
    const uint8_t* const body_a = at<uint8_t*>(at<uint8_t*>(contact, 0x94), 0x34);
    const Lanes pa = add(lanes(reinterpret_cast<const float*>(body_a + 0x30)), lanes(reinterpret_cast<const float*>(contact + 0x20)));
    return dot3(sub(pa, pb), lanes(reinterpret_cast<const float*>(contact + 0x10)));
}


// Hinge setup: +0xc flags cleared, anchors +0x10 / +0x20, unit axes +0x30 (body 0), +0x40 (body
// 1), +0x70 (reference), +0x50 = 0x000b8990(+0x30) and +0x60 = +0x30 x +0x50, the limit axes
// +0x80 / +0x90 = normalize(rotation(+0x40, angle) * v) for both angles, +0xa0 = friction.
// No callers, no stored pointer, no immediate: dead.
void XB_THISCALL PhysHinge_Setup(uint8_t* hinge, const float* anchor_a, const float* anchor_b, const float* axis_a,
                                 const float* axis_b, const float* reference, const float* v, float angle_lo,
                                 float angle_hi, float friction) {
    at<uint32_t>(hinge, 0xc) = 0;
    alignas(16) float t[4];
    std::memcpy(t, anchor_a, 16);
    copy_words(reinterpret_cast<float*>(hinge + 0x10), t);
    std::memcpy(t, anchor_b, 16);
    copy_words(reinterpret_cast<float*>(hinge + 0x20), t);
    alignas(16) float n[4];
    copy_words(reinterpret_cast<float*>(hinge + 0x30), Vec3A_NormalizeDup(n, axis_a));
    copy_words(reinterpret_cast<float*>(hinge + 0x40), Vec3A_NormalizeDup(n, axis_b));
    copy_words(reinterpret_cast<float*>(hinge + 0x70), Vec3A_NormalizeDup(n, reference));
    alignas(16) float perp[4];
    copy_words(reinterpret_cast<float*>(hinge + 0x50),
               title_function<float* XB_CDECL(float*, const float*)>(0x000b8990)(perp, reinterpret_cast<float*>(hinge + 0x30)));
    store4(reinterpret_cast<float*>(hinge + 0x60), cross_lanes(lanes(reinterpret_cast<float*>(hinge + 0x30)),
                                                               lanes(reinterpret_cast<float*>(hinge + 0x50))));
    const float angles[2] = {angle_lo, angle_hi};
    for (int32_t i = 0; i < 2; ++i) {
        alignas(16) float axis[4], m[16], rotated[4], unit[4];
        copy_words(axis, reinterpret_cast<float*>(hinge + 0x40));
        Mat34_FromRotationVector(m, axis, angles[i]);
        alignas(16) float vv[4];
        std::memcpy(vv, v, 16);
        store4(rotated, mat_rotate(m, lanes(vv)));
        copy_words(reinterpret_cast<float*>(hinge + 0x80 + i * 0x10), Vec3A_Normalize(unit, rotated));
    }
    at<float>(hinge, 0xa0) = friction;
}

// Angular motor contacts (enabled byte +0xa8): the impulse words +0xb0 / +0xb8 / +0xc0 decay by
// 0.9 first; then one contact per axis of the frame +0x10 turned by body 0's rotation (points:
// that axis and body 1's matching row, normal: the next axis; +0xac + 8 i), range
// +-(+0xa4 * +0xa0) dt, target -(0x000bcc60 term / dt) - (body 0's rotation * +0x90) . normal.
void XB_THISCALL PhysAngularMotor_AddContacts(uint8_t* motor, uint8_t* world, float dt) {
    const float decay = title_global<float>(0x0083ec18);  // 0.9
    at<float>(motor, 0xb0) = at<float>(motor, 0xb0) * decay;
    at<float>(motor, 0xb8) = at<float>(motor, 0xb8) * decay;
    at<float>(motor, 0xc0) = at<float>(motor, 0xc0) * decay;
    if (!at<uint8_t>(motor, 0xa8)) return;
    const uint8_t* const body_a = at<uint8_t*>(motor, 0);
    const float range = (at<float>(motor, 0xa4) * at<float>(motor, 0xa0)) * dt;
    alignas(16) float rotation[12], frame[12], axes[12];
    for (int32_t i = 0; i < 3; ++i) {
        copy_words(rotation + i * 4, reinterpret_cast<const float*>(body_a) + i * 4);
        copy_words(frame + i * 4, reinterpret_cast<const float*>(motor + 0x10) + i * 4);
    }
    for (int32_t i = 0; i < 3; ++i) store4(axes + i * 4, mat_rotate(rotation, lanes(frame + i * 4)));
    alignas(16) float v[4], spin[4];
    copy_words(v, reinterpret_cast<const float*>(motor + 0x90));
    store4(spin, mat_rotate(reinterpret_cast<const float*>(body_a), lanes(v)));
    const float low = 0.0f - range;
    const float inv_dt = static_cast<float>(static_cast<long double>(title_global<float>(0x0083eb20)) / dt);
    for (int32_t i = 0; i < 3; ++i) {
        const uint8_t* const body_b = at<uint8_t*>(motor, 4);
        alignas(16) float pa[4], pb[4];
        copy_words(pa, axes + i * 4);
        copy_words(pb, reinterpret_cast<const float*>(body_b) + i * 4);
        const float* const n = axes + ((i + 1) % 3) * 4;
        uint8_t* const contact = PhysContacts_Add(world, at<uint32_t>(motor, 0), word_of(pa), word_of(body_b), word_of(pb),
                                                  word_of(n), word_of(motor + 0xac + static_cast<uint32_t>(i) * 8));
        at<float>(contact, 0x60) = low;
        at<float>(contact, 0x64) = range;
        const long double term = title_function<long double XB_THISCALL(uint8_t*)>(0x000bcc60)(contact);
        const long double t = (term * inv_dt) * title_global<float>(0x0083eb34);  // -1
        at<float>(contact, 0x7c) = 0.0f;
        at<float>(contact, 0x80) = 0.0f;
        at<float>(contact, 0x78) = static_cast<float>(t);
        const float d = dot3(lanes(spin), lanes(n));
        uint32_t bits;
        std::memcpy(&bits, &d, 4);
        bits ^= 0x80000000u;
        float neg;
        std::memcpy(&neg, &bits, 4);
        at<float>(contact, 0x78) = static_cast<float>(t + neg);
    }
}

// Rows +0x10 / +0x20 / +0x30 turned by rotation(+0x90 rotation vector, dt): row = M * row.
void XB_THISCALL PhysFrame_RotateByVelocity(uint8_t* object, float dt) {
    alignas(16) float m[12], rows[12];
    {
        alignas(16) float built[16];
        Mat34_FromRotationVector(built, reinterpret_cast<float*>(object + 0x90), dt);
        std::memcpy(m, built, sizeof(m));
    }
    std::memcpy(rows, object + 0x10, sizeof(rows));
    alignas(16) float out[12];
    for (int32_t i = 0; i < 3; ++i) store4(out + i * 4, mat_rotate(m, lanes(rows + i * 4)));
    for (int32_t i = 0; i < 3; ++i) copy_words(reinterpret_cast<float*>(object + 0x10 + i * 0x10), out + i * 4);
}

// Solid sphere: mass = r^3 * 4/3 pi, inertia (3 words) = mass r^2 * 0.4.
void XB_CDECL Phys_SphereMassProperties(float radius, float* inertia, float* mass) {
    const float m = ((radius * radius) * radius) * title_global<float>(0x007cddb0);
    *mass = m;
    const float i = ((m * radius) * radius) * title_global<float>(0x0083ec44);
    inertia[0] = i;
    inertia[1] = i;
    inertia[2] = i;
}

// Mass-weighted centre (+0x30, weights 1 / +0x130) into com (lane 3 starts at 1 and is divided
// too), the weighted +0xd0 sum into momentum and the total weight into total.
void XB_CDECL PhysBodies_GetCenterOfMass(uint8_t* const* bodies, int32_t count, float* com, float* momentum,
                                         float* total) {
    for (uint32_t i = 0; i < 4; ++i) reinterpret_cast<uint32_t*>(com)[i] = title_global<uint32_t>(0x007cdd10 + i * 4);
    for (uint32_t i = 0; i < 4; ++i) reinterpret_cast<uint32_t*>(momentum)[i] = title_global<uint32_t>(0x007cdb90 + i * 4);
    *total = 0.0f;
    const float one = title_global<float>(0x0083eb20);
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* const body = bodies[i];
        if (!body) continue;
        const Lanes position = lanes(reinterpret_cast<const float*>(body + 0x30));
        alignas(16) float sum[4];
        copy_words(sum, com);
        const float w = one / at<float>(body, 0x130);
        const Lanes weight = splat(w);
        store4(com, add(lanes(sum), mul(weight, position)));
        const Lanes velocity = lanes(reinterpret_cast<const float*>(body + 0xd0));
        copy_words(sum, momentum);
        store4(momentum, add(lanes(sum), mul(weight, velocity)));
        *total = *total + w;
    }
    store4(com, div(lanes(com), splat(*total)));
}

// Bounding sphere of `count` 12-byte points: centre = the mean (4-lane loads), radius = the
// largest distance (x87 square root). No callers, no stored pointer, no immediate: dead.
void XB_CDECL Phys_PointsBoundingSphere(const float* points, int32_t count, float* radius, float* centre) {
    Lanes sum;
    for (uint32_t i = 0; i < 4; ++i) sum.v[i] = title_global<float>(0x007cdb90 + i * 4);
    for (int32_t i = 0; i < count; ++i) sum = add(sum, lanes(points + i * 3));
    const Lanes mean = div(sum, splat(static_cast<float>(count)));
    *radius = 0.0f;
    for (int32_t i = 0; i < count; ++i) {
        const float sq = self_dot(sub(lanes(points + i * 3), mean));
        if (sq > *radius) *radius = sq;
    }
    *radius = platform_x87_sqrt(*radius);
    alignas(16) float c[4];
    store4(c, mean);
    centre[0] = c[0];
    centre[1] = c[1];
    centre[2] = c[2];
}

// Solid box of full extents size: mass = x y z, inertia = mass / 12 * (y^2 + z^2, x^2 + z^2,
// x^2 + y^2), summed from the 4-lane products as the title does.
void XB_CDECL Phys_BoxMassProperties(const float* size, float* inertia, float* mass) {
    *mass = (size[0] * size[1]) * size[2];
    const float z = size[2];
    const float y = size[1];
    const float x = size[0];
    const Lanes squares = {{x * x, y * y, z * z, x * x}};
    const float k = ((size[0] * y) * size[2]) * title_global<float>(0x007cddb8);  // 1/12
    const Lanes b = mul(splat(k), squares);
    const Lanes plus_y = add(b, splat(b.v[1]));
    const Lanes plus_z = add(b, splat(b.v[2]));
    platform_keep(plus_y);
    platform_keep(plus_z);
    inertia[0] = plus_z.v[1];
    inertia[1] = plus_z.v[0];
    inertia[2] = plus_y.v[0];
}

// Box around `count` 12-byte points (4-lane loads): size = max - min, centre = (min + max) / 2
// (three words each). No callers, no stored pointer, no immediate: dead.
void XB_CDECL Phys_PointsBoxCenterSize(const float* points, int32_t count, float* size, float* centre) {
    const Lanes big = splat(title_global<float>(0x0083eb70));    // 1e7
    const Lanes small = splat(title_global<float>(0x0083eb74));  // -1e7
    Lanes lo = big, hi = small;
    for (int32_t i = 0; i < count; ++i) {
        const Lanes p = lanes(points + i * 3);
        if (!(lo.v[0] <= p.v[0])) lo.v[0] = p.v[0];
        if (!(p.v[0] <= hi.v[0])) hi.v[0] = p.v[0];
        if (less_mask(p, lo) & 2) lo.v[1] = p.v[1];
        if (less_mask(hi, p) & 2) hi.v[1] = p.v[1];
        if (less_mask(p, lo) & 4) lo.v[2] = p.v[2];
        if (less_mask(hi, p) & 4) hi.v[2] = p.v[2];
    }
    alignas(16) float c[4], s[4];
    store4(c, mul(splat(title_global<float>(0x0083eb24)), add(lo, hi)));
    centre[0] = c[0];
    centre[1] = c[1];
    centre[2] = c[2];
    store4(s, sub(hi, lo));
    size[0] = s[0];
    size[1] = s[1];
    size[2] = s[2];
}


namespace {

// The zero vector at 0x007cde20, copied as words.
void zero_vector_e20(uint8_t* out) {
    for (uint32_t i = 0; i < 4; ++i) at<uint32_t>(out, i * 4) = title_global<uint32_t>(0x007cde20 + i * 4);
}

// 4-lane reciprocal with one Newton step, (2 - r v) r, r = the RCPPS estimate.
Lanes refined_reciprocal(const Lanes& v) {
    Lanes r;
    for (int32_t i = 0; i < 4; ++i) r.v[i] = platform_rcp_estimate(v.v[i]);
    return mul(sub(lanes(&title_global<float>(0x007cddf0)), mul(r, v)), r);  // 2
}

}  // namespace

// Velocity of a point given in a frame's rotation (rows +0, +0x10, +0x20): 0x000bb4f0 fills the
// linear (out) and angular velocity, then out += angular x (rows * local).
float* XB_CDECL Phys_GetVelocityAtLocalPoint(uint32_t a0, const float* frame, const float* local, uint32_t a3,
                                             float* out, float* angular) {
    title_function<void XB_CDECL(uint32_t, const float*, uint32_t, float*, float*)>(0x000bb4f0)(a0, frame, a3, out,
                                                                                               angular);
    alignas(16) float p[4], rows[12];
    copy_words(p, local);
    for (int32_t i = 0; i < 3; ++i) copy_words(rows + i * 4, frame + i * 4);
    const Lanes r = mat_rotate(rows, lanes(p));
    const Lanes w = lanes(angular);
    alignas(16) float v[4];
    copy_words(v, out);
    store4(out, add(lanes(v), cross_lanes(w, r)));
    return out;
}

// +0x130 = 1 / mass.
void XB_THISCALL PhysBody_SetMass(uint8_t* body, float mass) { at<float>(body, 0x130) = title_global<float>(0x0083eb20) / mass; }

// +0xb0 = 1 / inertia (4 lanes, refined estimate).
void XB_THISCALL PhysBody_SetInertia(uint8_t* body, const float* inertia) {
    alignas(16) float in[4];
    std::memcpy(in, inertia, 16);
    store4(reinterpret_cast<float*>(body + 0xb0), refined_reciprocal(lanes(in)));
}

// +0x110 += force; +0x120 += scale * ((point - +0x30) x force).
void XB_THISCALL PhysBody_AddForceAtPoint(uint8_t* body, const float* force, const float* point, float scale) {
    {
        alignas(16) float f[4], sum[4];
        std::memcpy(f, force, 16);
        copy_words(sum, reinterpret_cast<float*>(body + 0x110));
        store4(reinterpret_cast<float*>(body + 0x110), add(lanes(sum), lanes(f)));
    }
    const Lanes position = lanes(reinterpret_cast<float*>(body + 0x30));
    alignas(16) float f[4], p[4], sum[4];
    std::memcpy(f, force, 16);
    std::memcpy(p, point, 16);
    const Lanes torque = cross_lanes(sub(lanes(p), position), lanes(f));
    copy_words(sum, reinterpret_cast<float*>(body + 0x120));
    store4(reinterpret_cast<float*>(body + 0x120), add(lanes(sum), mul(splat(scale), torque)));
}

// A 0x1c0 object: flags +0x144 = 0x20, frame (16 words) copied to +0, velocities +0xd0 / +0xe0
// zero, +0x1b0 = the frame pointer, +0x150 / +0x154 = 0, +0x13c / +0x140 = 1.
uint8_t* XB_THISCALL PhysObject1c0_Init(uint8_t* object, const float* frame) {
    at<uint32_t>(object, 0x144) = 0;
    for (uint32_t i = 0; i < 16; ++i) at<uint32_t>(object, i * 4) = reinterpret_cast<const uint32_t*>(frame)[i];
    zero_vector_e20(object + 0xd0);
    zero_vector_e20(object + 0xe0);
    const uint32_t flags = at<uint32_t>(object, 0x144) | 0x20;
    at<const float*>(object, 0x1b0) = frame;
    at<uint32_t>(object, 0x150) = 0;
    at<uint32_t>(object, 0x154) = 0;
    at<float>(object, 0x13c) = title_global<float>(0x0083eb20);
    at<float>(object, 0x140) = title_global<float>(0x0083eb20);
    at<uint32_t>(object, 0x144) = flags;
    return object;
}

// Body setup: +0x130 = 1 / mass, +0xb0 = 1 / inertia, frame (16 words) at +0, velocities
// +0xd0 / +0xe0 (and their copies +0xf0 / +0x100), +0x148, +0x160, forces +0x110 / +0x120 zero,
// +0x134 / +0x13c / +0x140 = 1, +0x144 / +0x14c = 0, +0xc0 = (0, -1, 0, 0), +0x138 / +0x168 =
// 1000, +0x164 = 0; with flag 1 of the world (0x0090a87c) the predicted transform for its step.
uint8_t* XB_THISCALL PhysBody_Init(uint8_t* body, float mass, const float* inertia, const float* frame,
                                   const float* velocity, const float* angular, float value148, uint32_t value160) {
    const float one = title_global<float>(0x0083eb20);
    at<float>(body, 0x130) = one / mass;
    {
        alignas(16) float in[4];
        std::memcpy(in, inertia, 16);
        store4(reinterpret_cast<float*>(body + 0xb0), refined_reciprocal(lanes(in)));
    }
    for (uint32_t i = 0; i < 16; ++i) at<uint32_t>(body, i * 4) = reinterpret_cast<const uint32_t*>(frame)[i];
    alignas(16) float t[4];
    std::memcpy(t, velocity, 16);
    copy_words(reinterpret_cast<float*>(body + 0xd0), t);
    std::memcpy(t, angular, 16);
    at<float>(body, 0x148) = value148;
    copy_words(reinterpret_cast<float*>(body + 0xe0), t);
    at<uint32_t>(body, 0x160) = value160;
    zero_vector_e20(body + 0x110);
    zero_vector_e20(body + 0x120);
    copy_words(reinterpret_cast<float*>(body + 0xf0), reinterpret_cast<float*>(body + 0xd0));
    copy_words(reinterpret_cast<float*>(body + 0x100), reinterpret_cast<float*>(body + 0xe0));
    at<float>(body, 0x134) = one;
    at<float>(body, 0x13c) = one;
    at<float>(body, 0x140) = one;
    at<uint32_t>(body, 0x144) = 0;
    at<uint32_t>(body, 0x14c) = 0;
    at<float>(body, 0xc0) = 0.0f;
    at<float>(body, 0xc4) = title_global<float>(0x0083eb34);  // -1
    at<float>(body, 0xc8) = 0.0f;
    at<float>(body, 0xcc) = 0.0f;
    const float limit = title_global<float>(0x0083eb9c);  // 1000
    at<float>(body, 0x138) = limit;
    at<float>(body, 0x164) = 0.0f;
    at<float>(body, 0x168) = limit;
    if (title_global<uint8_t*>(0x0090a87c)[0] & 1) {
        float dt = at<float>(title_global<uint8_t*>(0x0090a87c), 4);
        PhysBody_PredictTransform(body, &dt);
    }
    return body;
}

// A fixed body (flag 0x10 of +0x144): identity frame, zero velocities, +0x150 / +0x154 = 0.
uint32_t XB_THISCALL PhysBody_InitFixed(uint8_t* body) {
    at<uint32_t>(body, 0x144) = 0;
    Matrix_Identity(reinterpret_cast<float*>(body));
    zero_vector_e20(body + 0xd0);
    zero_vector_e20(body + 0xe0);
    const uint32_t flags = at<uint32_t>(body, 0x144) | 0x10;
    at<uint32_t>(body, 0x150) = 0;
    at<uint32_t>(body, 0x154) = 0;
    at<uint32_t>(body, 0x144) = flags;
    return flags;
}

// The transform after one step (h = +0x13c * dt): +0x70 = position + h (velocity + +0x130 h
// force), rows +0x40..+0x60 = rotation(+0xe0 angular velocity, h) * rows.
void XB_CDECL PhysBody_PredictTransform(uint8_t* body, const float* dt) {
    const float h = at<float>(body, 0x13c) * *dt;
    const float k = at<float>(body, 0x130) * h;
    const Lanes force = lanes(reinterpret_cast<float*>(body + 0x110));
    alignas(16) float velocity[4], position[4];
    copy_words(velocity, reinterpret_cast<float*>(body + 0xd0));
    copy_words(position, reinterpret_cast<float*>(body + 0x30));
    const Lanes v = add(lanes(velocity), mul(splat(k), force));
    alignas(16) float p[4];
    store4(p, add(lanes(position), mul(splat(h), v)));
    copy_words(reinterpret_cast<float*>(body + 0x70), p);
    alignas(16) float m[16], rows[12];
    Mat34_FromRotationVector(m, reinterpret_cast<float*>(body + 0xe0), h);
    alignas(16) float mc[12];
    std::memcpy(mc, m, sizeof(mc));
    std::memcpy(rows, body, sizeof(rows));
    for (int32_t i = 0; i < 3; ++i) {
        alignas(16) float r[4];
        store4(r, mat_rotate(mc, lanes(rows + i * 4)));
        copy_words(reinterpret_cast<float*>(body + 0x40 + i * 0x10), r);
    }
}

// out = (+0x10 + (+0x130 -> +0x34) position) - (+0x20 + (+0x134 -> +0x34) position when set).
float* XB_THISCALL PhysContact140_GetSeparationVector(const uint8_t* contact, float* out) {
    const uint8_t* const other = at<uint8_t*>(contact, 0x134);
    Lanes pb;
    if (other)
        pb = add(lanes(reinterpret_cast<const float*>(at<uint8_t*>(other, 0x34) + 0x30)),
                 lanes(reinterpret_cast<const float*>(contact + 0x20)));
    else
        pb = lanes(reinterpret_cast<const float*>(contact + 0x20));
    const uint8_t* const body_a = at<uint8_t*>(at<uint8_t*>(contact, 0x130), 0x34);
    const Lanes pa = add(lanes(reinterpret_cast<const float*>(body_a + 0x30)), lanes(reinterpret_cast<const float*>(contact + 0x10)));
    alignas(16) float d[4];
    store4(d, sub(pa, pb));
    std::memcpy(out, d, 16);
    return out;
}


// Launches bodies at a target: from their mass centre (PhysBodies_GetCenterOfMass) the horizontal
// distance h (x87) and slope dz / h; a Newton search (at most 101 steps, from s = 100) for the
// horizontal speed s whose ballistic velocity (s, 0, s slope - g / s) along the horizontal unit
// direction (g = ((body 0's +0x134 * -9.8) h) * 0.5) is nearest the current mean velocity;
// |s| in (0.001, 1) becomes +-1. Each body's +0x110 gets total / sum(w^2 inverses) times the
// velocity change, over its w^2 (w = +0x130), with lane 3 = the current horizontal speed.
// *dist_sq = |target - centre|^2; nothing else when h < 0.01.
void XB_CDECL PhysBodies_LaunchTowardTarget(uint8_t* const* bodies, int32_t count, const float* target,
                                            float* dist_sq) {
    alignas(16) float com[4], momentum[4];
    float total;
    PhysBodies_GetCenterOfMass(bodies, count, com, momentum, &total);
    const Lanes v0 = div(lanes(momentum), splat(total));
    const Lanes d = sub(lanes(target), lanes(com));
    const Lanes sq = mul(d, d);
    platform_keep(sq);
    *dist_sq = (sq.v[0] + sq.v[1]) + sq.v[2];
    Lanes flat_sq = d;
    flat_sq.v[0] = d.v[0] * d.v[0];
    flat_sq = add(flat_sq, splat(sq.v[1]));
    platform_keep(flat_sq);
    const float h = platform_x87_sqrt(flat_sq.v[0]);
    if (platform_x87_greater(title_global<float>(0x0083eb94), h)) return;  // 0.01
    const float g = ((at<float>(bodies[0], 0x134) * title_global<float>(0x007cddb4)) * h) *  // -9.8
                    title_global<float>(0x0083eb24);                                         // 0.5
    const float slope = d.v[2] / h;
    const Lanes flat = {{d.v[0], d.v[1], 0.0f, d.v[0]}};
    const Lanes u = div(flat, splat(h));
    const float vh = dot3(v0, u);
    const Lanes current = {{vh, 0.0f, v0.v[2], vh}};
    const float g2 = g * title_global<float>(0x0083ec5c);  // -2
    const float one = title_global<float>(0x0083eb20);
    float s = title_global<float>(0x0083eb90);  // 100
    bool converged = false;
    int32_t steps = 0;
    do {
        const Lanes v = {{s, 0.0f, s * slope - g / s, s}};
        const float s2 = s * s;
        const Lanes dv = {{one, 0.0f, g / s2 + slope, one}};
        const Lanes delta = sub(v, current);
        const float s3 = s2 * s;
        const Lanes ddv = {{0.0f, 0.0f, g2 / s3, 0.0f}};
        const float num = dot3(dv, delta);
        const float denom = dot3(ddv, delta) + dot3(dv, dv);
        const long double next = static_cast<long double>(s) - static_cast<long double>(num) / denom;
        const float next_s = static_cast<float>(next);
        long double change = next - s;
        if (change < 0) change = -change;
        if (static_cast<long double>(title_global<float>(0x0083eb94)) > change) converged = true;
        s = next_s;
        ++steps;
    } while (!(steps > 100) && !converged);
    float a;
    {
        uint32_t bits;
        std::memcpy(&bits, &s, 4);
        bits &= 0x7fffffffu;
        std::memcpy(&a, &bits, 4);
    }
    if (platform_x87_greater(one, a) && a > title_global<float>(0x0083eb4c))  // 0.001
        s = s >= 0.0f ? one : title_global<float>(0x0083eb34);
    const float vz = s * slope - g / s;
    const Lanes launch = add(mul(splat(s), u), Lanes{{0.0f, 0.0f, vz, 0.0f}});
    float sum = 0.0f;
    for (int32_t i = 0; i < count; ++i) {
        const uint8_t* const body = bodies[i];
        if (!body) continue;
        const float w = at<float>(body, 0x130);
        sum = one / (w * w) + sum;
    }
    const Lanes impulse = mul(splat(total / sum), sub(launch, v0));
    for (int32_t i = 0; i < count; ++i) {
        uint8_t* const body = bodies[i];
        if (!body) continue;
        const float w = at<float>(body, 0x130);
        Lanes share = div(impulse, splat(w * w));
        platform_keep(share);
        share.v[3] = vh;
        alignas(16) float acc[4];
        copy_words(acc, reinterpret_cast<float*>(body + 0x110));
        store4(reinterpret_cast<float*>(body + 0x110), add(lanes(acc), share));
    }
}

}  // namespace game
