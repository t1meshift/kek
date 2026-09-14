#ifndef KEK_2D_H
#define KEK_2D_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include "kek.h"
#include "kek_font.h"
#include "kek_math.h"

char kek_2d_clip_line(KEK_IVec2 *p0, KEK_IVec2 *p1, KEK_IRect2 v);
KEK_IVec2 kek_2d_to_screen(KEK_engine* engine, KEK_FVec2 p);
void kek_2d_line(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color);
void kek_2d_rect(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color_fill);
void kek_2d_rect_border(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color_fill, uint8_t color_border);
void kek_2d_circle(KEK_engine* engine, KEK_IVec2 p, uint16_t radius, uint8_t color_fill);
void kek_2d_circle_border(KEK_engine* engine, KEK_IVec2 p, uint16_t radius, uint8_t color_fill, uint8_t color_border);
void kek_2d_triangle(KEK_engine* engine, KEK_IVec2 vertices[3], uint8_t color_fill);
void kek_2d_triangle_border(KEK_engine* engine, KEK_IVec2 vertices[3], uint8_t color_fill, uint8_t color_border);
void kek_2d_text_5x8(KEK_engine* engine, const KEK_font_5x8 *font, KEK_IVec2 p, const char *text, uint8_t color);

#ifdef __cplusplus
}
#endif

#endif // KEK_2D_H
