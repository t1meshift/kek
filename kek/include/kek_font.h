#ifndef KEK_FONT_H
#define KEK_FONT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

typedef struct KEK_glyph_5x8 {
    uint8_t data[8];
    uint32_t codepoint;
} KEK_glyph_5x8;

typedef struct KEK_font_5x8 {
    uint32_t glyph_map[256];
    KEK_glyph_5x8* glyphs;
    KEK_glyph_5x8 fallback_glyph;
    uint32_t glyph_count;
} KEK_font_5x8;

const KEK_glyph_5x8* kek_font_5x8_get_glyph(const KEK_font_5x8* font, uint32_t codepoint);

extern KEK_font_5x8 KEK_FONT_DEFAULT_5X8;

#ifdef __cplusplus
}
#endif

#endif // KEK_FONT_H
