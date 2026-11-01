#include "phys.h"

#include <cstring>

#include "lanes.h"
#include "phys_pool.h"
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

}  // namespace game
