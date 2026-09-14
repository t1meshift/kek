#include <math.h>
#include "kek_math.h"

/*
 * Builds a 3x3 rotation matrix matching the kek_3d_rotate convention:
 * apply Z rotation, then Y, then X (each with the engine's sign convention).
 * Derived by expanding kek_3d_rotate analytically so that
 * kek_mat3_apply(kek_mat3_from_euler(r), v) == kek_3d_rotate(v, r).
 */
KEK_Mat3 kek_mat3_from_euler(KEK_FVec3 r) {
    float cx = cosf(r.x), sx = sinf(r.x);
    float cy = cosf(r.y), sy = sinf(r.y);
    float cz = cosf(r.z), sz = sinf(r.z);
    KEK_Mat3 mat;
    mat.m[0] = cz*cy;            mat.m[1] = sz*cy;            mat.m[2] = -sy;
    mat.m[3] = cz*sy*sx-sz*cx;  mat.m[4] = sz*sy*sx+cz*cx;  mat.m[5] = cy*sx;
    mat.m[6] = cz*sy*cx+sz*sx;  mat.m[7] = sz*sy*cx-cz*sx;  mat.m[8] = cy*cx;
    return mat;
}

KEK_FVec3 kek_mat3_apply(KEK_Mat3 mat, KEK_FVec3 v) {
    return (KEK_FVec3) {
        mat.m[0]*v.x + mat.m[1]*v.y + mat.m[2]*v.z,
        mat.m[3]*v.x + mat.m[4]*v.y + mat.m[5]*v.z,
        mat.m[6]*v.x + mat.m[7]*v.y + mat.m[8]*v.z
    };
}

char kek_point_in_rect(KEK_IRect2 r, KEK_IVec2 p) {
    return (
        (r.origin.x <= p.x && r.origin.x + r.size.x > p.x) &&
        (r.origin.y <= p.y && r.origin.y + r.size.y > p.y)
    );
}

float kek_area_triangle(KEK_IVec2 t[3]) {
    return kek_area_triangle_signed(t);
}

float kek_area_triangle_signed(KEK_IVec2 t[3]) {
    KEK_IVec2 a, b, c;
    a = t[0];
    b = t[1];
    c = t[2];

    return ((b.y - a.y)*(b.x + a.x) + (c.y - b.y)*(c.x + b.x) + (a.y - c.y)*(a.x + c.x)) / 2.f;
}

void kek_normalize_fvec3(KEK_FVec3* vec) {
    float x = vec->x;
    float y = vec->y;
    float z = vec->z;
    float len_sq = x * x + y * y + z * z;

    if (len_sq > 0.f) {
        float inv_len = 1.f / sqrtf(len_sq);
        vec->x = x * inv_len;
        vec->y = y * inv_len;
        vec->z = z * inv_len;
    }
}

KEK_FVec3 kek_normalize_fvec3_copy(KEK_FVec3 vec) {
    kek_normalize_fvec3(&vec);
    return vec;
}

void kek_add_fvec3(KEK_FVec3* a, const KEK_FVec3* b) {
    a->x += b->x;
    a->y += b->y;
    a->z += b->z;
}

KEK_FVec3 kek_add_fvec3_copy(KEK_FVec3 a, KEK_FVec3 b) {
    a.x += b.x;
    a.y += b.y;
    a.z += b.z;
    return a;
}

void kek_mul_fvec3_n(KEK_FVec3* a, float n) {
    a->x *= n;
    a->y *= n;
    a->z *= n;
}

KEK_FVec3 kek_mul_fvec3_n_copy(KEK_FVec3 a, float n) {
    a.x *= n;
    a.y *= n;
    a.z *= n;
    return a;
}
