#ifndef KEK_2D_H
#define KEK_2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek.h"
#include "kek_font.h"
#include "kek_math.h"
#include "kek_texture.h"

char kek_2d_clip_line(KEK_IVec2 *p0, KEK_IVec2 *p1, KEK_IRect2 v);
KEK_IVec2 kek_2d_to_screen(KEK_engine* engine, KEK_FVec2 p);
void kek_2d_line(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color);
void kek_2d_rect(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color_fill);
void kek_2d_rect_border(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color_fill, uint8_t color_border);
void kek_2d_circle(KEK_engine* engine, KEK_IVec2 p, uint16_t radius, uint8_t color_fill);
void kek_2d_circle_border(KEK_engine* engine, KEK_IVec2 p, uint16_t radius, uint8_t color_fill, uint8_t color_border);
/* Vertices are screen pixels and may lie off the frame, but the span
   interpolation multiplies in int: a triangle more than ~46,000 pixels across
   on both axes overflows. Nothing that draws in screen space gets near that. */
void kek_2d_triangle(KEK_engine* engine, KEK_IVec2 vertices[3], uint8_t color_fill);
void kek_2d_triangle_border(KEK_engine* engine, KEK_IVec2 vertices[3], uint8_t color_fill, uint8_t color_border);
void kek_2d_text_5x8(KEK_engine* engine, const KEK_font_5x8 *font, KEK_IVec2 p, const char *text, uint8_t color);
/* Copy a texture rectangle to dest, its top-left screen pixel. Rectangles
   use origin + half-open size; source coordinates outside the texture and
   destination pixels outside the current frame are clipped together. -1
   copies every palette index; 0..255 skips matching source pixels. Invalid
   transparent indices and nonpositive rectangle sizes draw nothing. Blits
   do not touch depth, so draw HUD after 3D. The source must differ from the
   bound render target. */
void kek_2d_blit_texture_region(KEK_engine* engine, const KEK_texture* texture,
                                KEK_IRect2 source, KEK_IVec2 dest, int transparent_index);
/* Position is the screen location of pivot, measured from the selected
   rectangle's origin. Scale {1,1} keeps the original size; a negative axis
   mirrors it. Rotation is in radians, positive clockwise on screen. Samples
   the nearest palette index and keeps the region blit's transparency, clipping
   and depth behavior. Non-finite parameters or a zero scale draw nothing. */
typedef struct KEK_2D_Transform {
    KEK_FVec2 position;
    KEK_FVec2 pivot;
    KEK_FVec2 scale;
    float rotation;
} KEK_2D_Transform;

void kek_2d_blit_texture_region_transform(KEK_engine* engine, const KEK_texture* texture,
                                          KEK_IRect2 source, KEK_2D_Transform transform,
                                          int transparent_index);
/* Transform the entire texture without constructing a source rectangle. */
void kek_2d_blit_texture_transform(KEK_engine* engine, const KEK_texture* texture,
                                   KEK_2D_Transform transform, int transparent_index);
/* Convenience wrapper for the entire texture. */
void kek_2d_blit_texture(KEK_engine* engine, const KEK_texture* texture,
                         KEK_IVec2 dest, int transparent_index);

#ifdef __cplusplus
}
#endif

#endif // KEK_2D_H
