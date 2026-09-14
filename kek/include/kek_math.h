#ifndef KEK_MATH_H
#define KEK_MATH_H

#ifdef __cplusplus
extern "C" {
#endif

typedef struct KEK_IVec2 {
    int x;
    int y;
} KEK_IVec2;

typedef struct KEK_FVec2 {
    float x;
    float y;
} KEK_FVec2;

typedef struct KEK_IRect2 {
    KEK_IVec2 origin;
    KEK_IVec2 size;
} KEK_IRect2;

typedef struct KEK_FVec3 {
    float x, y, z;
} KEK_FVec3;

typedef struct KEK_Mat3 {
    float m[9]; /* row-major: m[row*3 + col] */
} KEK_Mat3;

KEK_Mat3 kek_mat3_from_euler(KEK_FVec3 r);
KEK_FVec3 kek_mat3_apply(KEK_Mat3 mat, KEK_FVec3 v);

char kek_point_in_rect(KEK_IRect2 rect, KEK_IVec2 p);
float kek_area_triangle(KEK_IVec2 triangle[3]);
float kek_area_triangle_signed(KEK_IVec2 triangle[3]);

void kek_normalize_fvec3(KEK_FVec3* vec);
KEK_FVec3 kek_normalize_fvec3_copy(KEK_FVec3 vec);
void kek_add_fvec3(KEK_FVec3* a, const KEK_FVec3* b);
KEK_FVec3 kek_add_fvec3_copy(KEK_FVec3 a, KEK_FVec3 b);
void kek_mul_fvec3_n(KEK_FVec3* a, float n);
KEK_FVec3 kek_mul_fvec3_n_copy(KEK_FVec3 a, float n);

#ifdef __cplusplus
}
#endif

#endif
