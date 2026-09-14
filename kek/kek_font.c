#include "kek_font.h"

const KEK_glyph_5x8* kek_font_5x8_get_glyph(const KEK_font_5x8* font, uint32_t codepoint) {
    if (codepoint <= 0xFF) {
        uint32_t index = font->glyph_map[codepoint];
        return &font->glyphs[index];
    }

    uint32_t left = 1;
    uint32_t right = font->glyph_count;

    while (left < right) {
        uint32_t mid = left + (right - left) / 2;
        uint32_t mid_codepoint = font->glyphs[mid].codepoint;

        if (mid_codepoint < codepoint) {
            left = mid + 1;
        } else {
            right = mid;
        }
    }

    if (left < font->glyph_count && font->glyphs[left].codepoint == codepoint) {
        return &font->glyphs[left];
    }

    return &font->fallback_glyph;
}
