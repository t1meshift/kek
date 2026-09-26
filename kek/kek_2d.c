#include <stdlib.h>
#include <math.h>
#include "include/kek_math.h"
#include "kek_macro.h"
#include "kek_2d.h"
#include "kek_font.h"
#include "kek.h"
#include "kek_internal.h"


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
     * The trivial tests above do not settle every case. Two endpoints in
     * different outside regions — one left, one above — can still pass
     * clear of the rectangle beyond its corner, and then the intersections
     * computed above land outside it, on the extension of an edge. Clamping
     * those would draw a line that is not there: a segment missing the
     * top-left corner came back as the whole left edge.
     *
     * A real intersection is on an edge to within float error, so anything
     * further out than that is a miss. The slack is a pixel: a segment that
     * misses by less rounds onto the corner, which is as good an answer as
     * rejecting it.
     */
    if (nx0 < xmin - 1.f || nx0 > xmax + 1.f || ny0 < ymin - 1.f || ny0 > ymax + 1.f ||
        nx1 < xmin - 1.f || nx1 > xmax + 1.f || ny1 < ymin - 1.f || ny1 > ymax + 1.f)
        return 0;

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
     * After the check above the points lie within a pixel of the
     * rectangle, but float rounding near the boundary and that pixel of
     * slack can still produce values like xmax+1 or ymin-1, so we clamp.
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
        /* Not error << 1: error is routinely negative here, and shifting a
           negative value left is undefined. The multiply is the same code. */
        e2 = error * 2;
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

/* kek_blit and kek_line do not clip — that is what makes them fast paths, and
   why they live in kek_internal.h. These two are the clipping counterparts,
   for the primitives below that generate spans and points in unbounded
   coordinates. Everything here that touches the framebuffer goes through one
   of them. */

static void kek_2d_span(KEK_engine* engine, int y, int x0, int x1, uint8_t color) {
    if (y < 0 || y >= engine->h) {
        return;
    }
    if (x0 > x1) {
        KEK_SWAP(int, x0, x1);
    }
    /* Reject rather than clamp: clamping a fully off-screen span would glue a
       one-pixel sliver to the edge it fell off. */
    if (x1 < 0 || x0 >= engine->w) {
        return;
    }
    x0 = KEK_MAX(x0, 0);
    x1 = KEK_MIN(x1, engine->w - 1);
    kek_line(engine, (uint16_t)y, (uint16_t)x0, (uint16_t)x1, color);
}

static void kek_2d_point(KEK_engine* engine, int x, int y, uint8_t color) {
    if (x < 0 || x >= engine->w || y < 0 || y >= engine->h) {
        return;
    }
    kek_blit(engine, (uint16_t)x, (uint16_t)y, color);
}

/* Half-open in y and closed in x. Lopsided, but it is the existing convention:
   the debug bars in the game tile as {0, i*8} to {8, (i+1)*8} and rely on the
   bottom row being exclusive. */
void kek_2d_rect(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color_fill) {
    int y0 = p0.y;
    int y1 = p1.y;
    int y;

    if (y0 > y1) {
        KEK_SWAP(int, y0, y1);
    }

    for (y = y0; y < y1; ++y) {
        kek_2d_span(engine, y, p0.x, p1.x, color_fill);
    }
}

void kek_2d_rect_border(KEK_engine* engine, KEK_IVec2 p0, KEK_IVec2 p1, uint8_t color_fill, uint8_t color_border) {
    KEK_IVec2 tl = { p0.x, p0.y };
    KEK_IVec2 tr = { p1.x, p0.y };
    KEK_IVec2 br = { p1.x, p1.y };
    KEK_IVec2 bl = { p0.x, p1.y };

    kek_2d_rect(engine, p0, p1, color_fill);
    kek_2d_line(engine, tl, tr, color_border);
    kek_2d_line(engine, tr, br, color_border);
    kek_2d_line(engine, br, bl, color_border);
    kek_2d_line(engine, bl, tl, color_border);
}

void kek_2d_circle(KEK_engine* engine, KEK_IVec2 p, uint16_t radius, uint8_t color_fill) {
    // Jesko's method, idk if this even works correct
    int t1, t2, mx, my;
    t1 = radius / 16;
    mx = radius;
    my = 0;
    while (mx >= my) {
        kek_2d_span(engine, p.y + my, p.x - mx, p.x + mx, color_fill);
        kek_2d_span(engine, p.y - my, p.x - mx, p.x + mx, color_fill);
        kek_2d_span(engine, p.y + mx, p.x - my, p.x + my, color_fill);
        kek_2d_span(engine, p.y - mx, p.x - my, p.x + my, color_fill);
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
    mx = radius;
    my = 0;
    while (mx >= my) {
        kek_2d_point(engine, p.x + mx, p.y + my, color_border);
        kek_2d_point(engine, p.x + mx, p.y - my, color_border);
        kek_2d_point(engine, p.x - mx, p.y + my, color_border);
        kek_2d_point(engine, p.x - mx, p.y - my, color_border);
        kek_2d_point(engine, p.x + my, p.y + mx, color_border);
        kek_2d_point(engine, p.x + my, p.y - mx, color_border);
        kek_2d_point(engine, p.x - my, p.y + mx, color_border);
        kek_2d_point(engine, p.x - my, p.y - mx, color_border);
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

    /* Both loops intersect the segment's y range with the viewport rather than
       clamping its ends into it: clamping turns a triangle entirely above or
       below the screen into a single row drawn at the edge it fell off. An
       empty intersection leaves y0 > y1 and the loop does not run. The spans go
       through kek_2d_span, which applies the same rule in x. */
    if (a.y != b.y) {
        segment_height = b.y - a.y;
        int y0 = KEK_MAX(a.y, 0);
        int y1 = KEK_MIN(b.y, engine->h - 1);
        for (y = y0; y <= y1; ++y) {
            x1 = a.x + ((c.x - a.x)*(y - a.y)) / total_height;
            x2 = a.x + ((b.x - a.x)*(y - a.y)) / segment_height;
            kek_2d_span(engine, y, x1, x2, color_fill);
        }
    }

    if (b.y != c.y) {
        segment_height = c.y - b.y;
        int y0 = KEK_MAX(b.y, 0);
        int y1 = KEK_MIN(c.y, engine->h - 1);
        for (y = y0; y <= y1; ++y) {
            x1 = a.x + ((c.x - a.x)*(y - a.y)) / total_height;
            x2 = b.x + ((c.x - b.x)*(y - b.y)) / segment_height;
            kek_2d_span(engine, y, x1, x2, color_fill);
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
