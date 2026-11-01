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

// CMPLTPS mask of lanes 0-2 all set.
bool all_less3(const Lanes& a, const Lanes& b) { return a.v[0] < b.v[0] && a.v[1] < b.v[1] && a.v[2] < b.v[2]; }

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

}  // namespace game
