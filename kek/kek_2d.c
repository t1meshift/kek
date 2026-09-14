#include <stdlib.h>
#include <math.h>
#include "include/kek_math.h"
#include "kek_macro.h"
#include "kek_2d.h"
#include "kek_font.h"
#include "kek.h"


char kek_2d_clip_line(KEK_IVec2 *p0, KEK_IVec2 *p1, KEK_IRect2 v) {
    int xmin, ymin, xmax, ymax;
    float x0, y0, x1, y1, dx, dy;
    float nx0, ny0, nx1, ny1, t;
    unsigned c0, c1;

    /* Reject invalid input early. */
    if (!p0 || !p1 || v.size.x <= 0 || v.size.y <= 0)
        return 0;

    /*
     * Rectangle is treated as inclusive:
     *   [xmin .. xmax], [ymin .. ymax]
     *
     * So size {1,1} means exactly one valid pixel.
     */
    xmin = v.origin.x;
    ymin = v.origin.y;
    xmax = xmin + v.size.x - 1;
    ymax = ymin + v.size.y - 1;

    /*
     * Keep original line endpoints in float form.
     *
     * x0,y0,x1,y1 are the original segment.
     * nx0,ny0,nx1,ny1 are the potentially clipped results.
     *
     * This is useful because both clipped endpoints are computed
     * against the same original line equation, rather than mutating
     * the line after clipping one endpoint first.
     */
    x0 = nx0 = (float)p0->x;
    y0 = ny0 = (float)p0->y;
    x1 = nx1 = (float)p1->x;
    y1 = ny1 = (float)p1->y;

    /* Direction vector of the original line. */
    dx = x1 - x0;
    dy = y1 - y0;

    /*
     * 4-bit region code:
     *   bit 0: left
     *   bit 1: right
     *   bit 2: top
     *   bit 3: bottom
     *
     * This gives us cheap trivial accept/reject before doing any
     * actual clipping math.
     */
    c0 = (x0 < xmin ? 1u : 0u) |
         (x0 > xmax ? 2u : 0u) |
         (y0 < ymin ? 4u : 0u) |
         (y0 > ymax ? 8u : 0u);

    c1 = (x1 < xmin ? 1u : 0u) |
         (x1 > xmax ? 2u : 0u) |
         (y1 < ymin ? 4u : 0u) |
         (y1 > ymax ? 8u : 0u);

    /* Trivial accept: both endpoints already inside. */
    if (!(c0 | c1))
        return 1;

    /*
     * Trivial reject: both endpoints lie outside on the same side.
     *
     * Example:
     *   both left of the rectangle
     *   both above the rectangle
     *
     * In such cases the segment cannot cross the viewport.
     */
    if (c0 & c1)
        return 0;

    /*
     * Clip first endpoint if it is outside.
     *
     * MD-style idea:
     *   1. If point is left/right, try clipping against that vertical edge.
     *   2. If resulting y is outside the vertical span, fall back to top/bottom.
     *   3. If point is only top/bottom, clip directly against that horizontal edge.
     *
     * Future optimization ideas:
     *   - precompute 1/dx and 1/dy to replace divisions with multiplies
     *   - switch float math to fixed-point
     *   - possibly macro-ize c0/c1 blocks if code size matters more than clarity
     */
    if (c0) {
        if (c0 & 1u) {
            /* Intersect with left edge: x = xmin */
            t = ((float)xmin - x0) / dx;
            nx0 = (float)xmin;
            ny0 = y0 + t * dy;

            /*
             * If the candidate point misses vertically, then the true
             * visible intersection for this outside endpoint must be
             * on top or bottom instead.
             */
            if (ny0 < ymin || ny0 > ymax) {
                if (c0 & 4u) {
                    /* Intersect with top edge: y = ymin */
                    t = ((float)ymin - y0) / dy;
                    nx0 = x0 + t * dx;
                    ny0 = (float)ymin;
                } else {
                    /* Intersect with bottom edge: y = ymax */
                    t = ((float)ymax - y0) / dy;
                    nx0 = x0 + t * dx;
                    ny0 = (float)ymax;
                }
            }
        } else if (c0 & 2u) {
            /* Intersect with right edge: x = xmax */
            t = ((float)xmax - x0) / dx;
            nx0 = (float)xmax;
            ny0 = y0 + t * dy;

            if (ny0 < ymin || ny0 > ymax) {
                if (c0 & 4u) {
                    /* Intersect with top edge: y = ymin */
                    t = ((float)ymin - y0) / dy;
                    nx0 = x0 + t * dx;
                    ny0 = (float)ymin;
                } else {
                    /* Intersect with bottom edge: y = ymax */
                    t = ((float)ymax - y0) / dy;
                    nx0 = x0 + t * dx;
                    ny0 = (float)ymax;
                }
            }
        } else if (c0 & 4u) {
            /* Point is above only: intersect with top edge directly. */
            t = ((float)ymin - y0) / dy;
            nx0 = x0 + t * dx;
            ny0 = (float)ymin;
        } else {
            /* Point is below only: intersect with bottom edge directly. */
            t = ((float)ymax - y0) / dy;
            nx0 = x0 + t * dx;
            ny0 = (float)ymax;
        }
    }

    /* Same logic for the second endpoint. */
    if (c1) {
        if (c1 & 1u) {
            /* Intersect with left edge: x = xmin */
            t = ((float)xmin - x0) / dx;
            nx1 = (float)xmin;
            ny1 = y0 + t * dy;

            if (ny1 < ymin || ny1 > ymax) {
                if (c1 & 4u) {
                    /* Intersect with top edge: y = ymin */
                    t = ((float)ymin - y0) / dy;
                    nx1 = x0 + t * dx;
                    ny1 = (float)ymin;
                } else {
                    /* Intersect with bottom edge: y = ymax */
                    t = ((float)ymax - y0) / dy;
                    nx1 = x0 + t * dx;
                    ny1 = (float)ymax;
                }
            }
        } else if (c1 & 2u) {
            /* Intersect with right edge: x = xmax */
            t = ((float)xmax - x0) / dx;
            nx1 = (float)xmax;
            ny1 = y0 + t * dy;

            if (ny1 < ymin || ny1 > ymax) {
                if (c1 & 4u) {
                    /* Intersect with top edge: y = ymin */
                    t = ((float)ymin - y0) / dy;
                    nx1 = x0 + t * dx;
                    ny1 = (float)ymin;
                } else {
                    /* Intersect with bottom edge: y = ymax */
                    t = ((float)ymax - y0) / dy;
                    nx1 = x0 + t * dx;
                    ny1 = (float)ymax;
                }
            }
        } else if (c1 & 4u) {
            /* Point is above only: intersect with top edge directly. */
            t = ((float)ymin - y0) / dy;
            nx1 = x0 + t * dx;
            ny1 = (float)ymin;
        } else {
            /* Point is below only: intersect with bottom edge directly. */
            t = ((float)ymax - y0) / dy;
            nx1 = x0 + t * dx;
            ny1 = (float)ymax;
        }
    }

    /*
     * Convert clipped float coordinates back to integer space.
     *
     * roundf() is simple and readable for now.
     * Later, for DOS/fixed-point work, this is one of the places
     * most likely to change.
     */
    p0->x = (int)roundf(nx0);
    p0->y = (int)roundf(ny0);
    p1->x = (int)roundf(nx1);
    p1->y = (int)roundf(ny1);

    /*
     * Final safety clamp.
     *
     * In theory the computed points should already lie on or inside
     * the rectangle. In practice, float rounding near the boundary can
     * produce values like xmax+1 or ymin-1, so we clamp defensively.
     */
    if (p0->x < xmin) p0->x = xmin; else if (p0->x > xmax) p0->x = xmax;
    if (p0->y < ymin) p0->y = ymin; else if (p0->y > ymax) p0->y = ymax;
    if (p1->x < xmin) p1->x = xmin; else if (p1->x > xmax) p1->x = xmax;
    if (p1->y < ymin) p1->y = ymin; else if (p1->y > ymax) p1->y = ymax;

    return 1;
}

KEK_IVec2 kek_2d_to_screen(KEK_engine* e, KEK_FVec2 p) {
    // assuming p is normalized
    return (KEK_IVec2) {
        .x = (p.x + 1.f) * e->w / 2.f,
        .y = (1.f - p.y) * e->h / 2.f
    };
}

void kek_2d_line(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color) {
    KEK_IRect2 viewport = {
        (KEK_IVec2) {0, 0},
        (KEK_IVec2) { engine->w, engine->h }
    };

    if (!kek_2d_clip_line(&p0, &p1, viewport)) {
        return;
    }

    int x0 = p0.x;
    int x1 = p1.x;
    int y0 = p0.y;
    int y1 = p1.y;

    if (y0 == y1) {
        kek_line(engine, y0, KEK_MIN(x0, x1), KEK_MAX(x0, x1), color);
        return;
    }

    int32_t dx, sx, dy, sy, error, e2;
    dx = abs(x1 - x0);
    sx = x0 < x1 ? 1 : -1;
    dy = -abs(y1 - y0);
    sy = y0 < y1 ? 1 : -1;
    error = dx + dy;

    do {
        kek_blit(engine, x0, y0, color);
        e2 = error << 1;
        if (e2 >= dy) {
            error = error + dy;
            x0 = x0 + sx;
        }
        if (e2 <= dx) {
            error = error + dx;
            y0 = y0 + sy;
        }
    } while ((x1 - x0) * sx > 0 || (y1 - y0) * sy > 0);
}

void kek_2d_rect(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color_fill) {
    int w, h, sx, sy, x, y;
    w = engine->w;
    h = engine->h;
    int x0 = KEK_MAX(0, KEK_MIN(p0.x, w - 1));
    int x1 = KEK_MAX(0, KEK_MIN(p1.x, w - 1));
    int y0 = KEK_MAX(0, KEK_MIN(p0.y, h - 1));
    int y1 = KEK_MAX(0, KEK_MIN(p1.y, h - 1));
    sx = x0 < x1 ? 1 : -1;
    sy = y0 < y1 ? 1 : -1;

    for (y = y0; y < y1*sy; y += sy) {
        kek_line(engine, y, x0, x1, color_fill);
    }
}

void kek_2d_rect_border(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color_fill, uint8_t color_border) {
    kek_2d_rect(engine, p0, p1, color_fill);
    kek_2d_line(engine, p0, p1, color_border);
    kek_2d_line(engine, p0, p1, color_border);
    kek_2d_line(engine, p0, p1, color_border);
    kek_2d_line(engine, p0, p1, color_border);
}

void kek_2d_circle(KEK_engine* engine, KEK_IVec2 p, uint16_t radius, uint8_t color_fill) {
    // Jesko's method, idk if this even works correct
    int t1, t2, mx, my;
    t1 = radius / 16;
    t2 = 0;
    mx = radius;
    my = 0;
    while (mx >= my) {
        kek_line(engine, p.y + my, p.x - mx, p.x + mx, color_fill);
        kek_line(engine, p.y - my, p.x - mx, p.x + mx, color_fill);
        kek_line(engine, p.y + mx, p.x - my, p.x + my, color_fill);
        kek_line(engine, p.y - mx, p.x - my, p.x + my, color_fill);
        ++my;
        t1 += my;
        t2 = t1 - mx;
        if (t2 >= 0) {
            t1 = t2;
            --mx;
        }
    }
}

void kek_2d_circle_border(KEK_engine* engine, KEK_IVec2 p, uint16_t radius, uint8_t color_fill, uint8_t color_border) {
    kek_2d_circle(engine, p, radius, color_fill);
    // Jesko's method for the border, idk if this even works correct
    int t1, t2, mx, my;
    t1 = radius / 16;
    t2 = 0;
    mx = radius;
    my = 0;
    while (mx >= my) {
        kek_blit(engine, p.x + mx, p.y + my, color_border);
        kek_blit(engine, p.x + mx, p.y - my, color_border);
        kek_blit(engine, p.x - mx, p.y + my, color_border);
        kek_blit(engine, p.x - mx, p.y - my, color_border);
        kek_blit(engine, p.x + my, p.y + mx, color_border);
        kek_blit(engine, p.x + my, p.y - mx, color_border);
        kek_blit(engine, p.x - my, p.y + mx, color_border);
        kek_blit(engine, p.x - my, p.y - mx, color_border);
        ++my;
        t1 += my;
        t2 = t1 - mx;
        if (t2 >= 0) {
            t1 = t2;
            --mx;
        }
    }
}

void kek_2d_triangle(KEK_engine* engine, KEK_IVec2 vertices[3], uint8_t color_fill) {
    KEK_IVec2 a, b, c;
    int total_height, segment_height, y, x1, x2;

    a = vertices[0];
    b = vertices[1];
    c = vertices[2];

    if (a.y > b.y) {
        KEK_SWAP(KEK_IVec2, a, b);
    }
    if (a.y > c.y) {
        KEK_SWAP(KEK_IVec2, a, c);
    }
    if (b.y > c.y) {
        KEK_SWAP(KEK_IVec2, b, c);
    }

    total_height = c.y - a.y;

    if (a.y != b.y) {
        segment_height = b.y - a.y;
        int ay = KEK_MAX(0, KEK_MIN(a.y, engine->h - 1));
        int by = KEK_MAX(0, KEK_MIN(b.y, engine->h - 1));
        for (y = ay; y <= by; ++y) {
            x1 = a.x + ((c.x - a.x)*(y - a.y)) / total_height;
            x2 = a.x + ((b.x - a.x)*(y - a.y)) / segment_height;
            if (y < 0 || y >= engine->h) {
                continue;
            }
            int xl = KEK_MIN(x1, x2);
            int xr = KEK_MAX(x1, x2);
            xl = KEK_MAX(0, KEK_MIN(xl, engine->w - 1));
            xr = KEK_MAX(0, KEK_MIN(xr, engine->w - 1));
            kek_line(engine, y, xl, xr, color_fill);
        }
    }

    if (b.y != c.y) {
        segment_height = c.y - b.y;
        int by = KEK_MAX(0, KEK_MIN(b.y, engine->h - 1));
        int cy = KEK_MAX(0, KEK_MIN(c.y, engine->h - 1));
        for (y = by; y <= cy; ++y) {
            x1 = a.x + ((c.x - a.x)*(y - a.y)) / total_height;
            x2 = b.x + ((c.x - b.x)*(y - b.y)) / segment_height;
            if (y < 0 || y >= engine->h) {
                continue;
            }
            int xl = KEK_MIN(x1, x2);
            int xr = KEK_MAX(x1, x2);
            xl = KEK_MAX(0, KEK_MIN(xl, engine->w - 1));
            xr = KEK_MAX(0, KEK_MIN(xr, engine->w - 1));
            kek_line(engine, y, xl, xr, color_fill);
        }
    }
}

void kek_2d_triangle_border(KEK_engine* engine, KEK_IVec2 vertices[3], uint8_t color_fill, uint8_t color_border) {
    kek_2d_triangle(engine, vertices, color_fill);
    int i;
    for (i = 0; i < 3; ++i) {
        kek_2d_line(engine, vertices[i], vertices[(i + 1) % 3], color_border);
    }
}

void kek_2d_text_5x8(KEK_engine* engine, const KEK_font_5x8 *font, KEK_IVec2 p, const char *text, uint8_t color) {
    static const int glyph_width = 5;
    static const int glyph_height = 8;
    static const int glyph_advance = 6;
    int cursor_x;
    int cursor_y;

    if (!font || !text) {
        return;
    }

    cursor_x = p.x;
    cursor_y = p.y;

    while (*text) {
        const KEK_glyph_5x8 *glyph;
        uint8_t row;

        if (*text == '\n') {
            cursor_x = p.x;
            cursor_y += glyph_height;
            ++text;
            continue;
        }

        glyph = kek_font_5x8_get_glyph(font, (uint8_t)*text);

        for (row = 0; row < glyph_height; ++row) {
            uint8_t bits = glyph->data[row];
            uint8_t col;

            for (col = 0; col < glyph_width; ++col) {
                int dst_x;
                int dst_y;

                if ((bits & (1u << (glyph_width - 1 - col))) == 0) {
                    continue;
                }

                dst_x = cursor_x + (int)col;
                dst_y = cursor_y + (int)row;

                if (dst_x < 0 || dst_x >= engine->w || dst_y < 0 || dst_y >= engine->h) {
                    continue;
                }

                kek_blit(engine, (uint16_t)dst_x, (uint16_t)dst_y, color);
            }
        }

        cursor_x += glyph_advance;
        ++text;
    }
}
