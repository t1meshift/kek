/* How long the rasterisers take, scene by scene, at 320x200.

   Plain C99 over the library and nothing else: timing is clock(), so the same
   file builds for the target and runs under an emulator or on the machine.
   Each scene draws the same frames every round, clearing the frame first, and
   the fastest of the rounds is reported: on a desktop the slower ones are
   other processes, not the code.

   Each scene also prints a checksum of its last frame and how many pixels it
   painted, so a change meant to be invisible can be seen to be, and one that
   is not can be measured. --dump writes those last frames, one after another,
   raw, for comparing two builds byte by byte.

       kek_bench_raster [frames] [--dump FILE]

   frames per round, default 100. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kek.h"
#include "kek_3d.h"
#include "kek_model.h"
#include "kek_texture.h"

#define BENCH_W 320
#define BENCH_H 200
#define BENCH_ROUNDS 5

static unsigned char memory[KEK_MEMORY_SIZE(BENCH_W, BENCH_H) + (size_t)64u * 1024u];
static KEK_engine e;
static KEK_model flat_cube;

static KEK_3D_ProjectedVertex vertex(int x, int y, float z, float u, float v, float shade) {
    KEK_3D_ProjectedVertex p;
    p.screen.x = x;
    p.screen.y = y;
    p.depth = z;
    p.inv_z = 1.f / z;
    p.u_over_z = u / z;
    p.v_over_z = v / z;
    p.shade = shade;
    return p;
}

/* The screen as two triangles receding to the right, near on the left and far
   on the right, so the perspective divide has something to do; the frame
   number shifts the depth so no two frames are the same. */
static void screen_quad(int frame, int textured, float shade_far) {
    float near_z = 1.f + (float)(frame % 8) * 0.125f;
    float far_z = near_z * 6.f;
    KEK_3D_ProjectedVertex a[3], b[3];

    a[0] = vertex(0, 0, near_z, 0.f, 0.f, 0.f);
    a[1] = vertex(BENCH_W - 1, 0, far_z, 4.f, 0.f, shade_far);
    a[2] = vertex(BENCH_W - 1, BENCH_H - 1, far_z, 4.f, 4.f, shade_far);
    b[0] = a[0];
    b[1] = a[2];
    b[2] = vertex(0, BENCH_H - 1, near_z, 0.f, 4.f, 0.f);
    if (textured) {
        const KEK_texture* texture = kek_texture_get(&e, kek_default_texture_handle(&e));
        /* Four times across, as a wall would be. */
        kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_REPEAT);
        kek_3d_triangle_textured(&e, a, texture);
        kek_3d_triangle_textured(&e, b, texture);
        kek_texture_set_warp_mode(&e, KEK_TEXTURE_WARP_CLAMP);
    } else {
        kek_3d_triangle(&e, a, 20);
        kek_3d_triangle(&e, b, 20);
    }
}

/* Nothing but the clear every scene starts with: what to subtract. */
static void scene_clear(int frame) {
    (void)frame;
}

static void scene_flat_quad(int frame) {
    screen_quad(frame, 0, 0.f);
}

static void scene_textured_quad(int frame) {
    screen_quad(frame, 1, 0.f);
}

/* A shade that varies across the quad, so the per-pixel shade path runs. */
static void scene_textured_quad_shaded(int frame) {
    screen_quad(frame, 1, 2.5f);
}

/* The default cube close up and turning, most of the frame: the whole path
   from model to pixels, with the default light. */
static void draw_cube(int frame, KEK_model* model) {
    KEK_camera camera = KEK_DEFAULT_CAMERA;
    float turn = (float)frame * 0.05f;

    kek_3d_draw_model(&e, model, &camera, (KEK_FVec3){ 0.f, 0.f, 1.4f },
                      (KEK_FVec3){ turn * 0.7f, turn, turn * 0.3f });
}

static void scene_cube_textured(int frame) {
    draw_cube(frame, kek_model_get(&e, kek_default_cube_model_handle(&e)));
}

static void scene_cube_flat(int frame) {
    draw_cube(frame, &flat_cube);
}

static void scene_cube_fog(int frame) {
    kek_3d_set_fog(&e, 0.8f, 2.2f);
    draw_cube(frame, kek_model_get(&e, kek_default_cube_model_handle(&e)));
    kek_3d_set_fog(&e, 0.f, 0.f);
}

typedef struct Scene {
    const char* name;
    void (*draw)(int frame);
} Scene;

static const Scene SCENES[] = {
    { "clear only", scene_clear },
    { "flat quad", scene_flat_quad },
    { "textured quad", scene_textured_quad },
    { "textured quad, shaded", scene_textured_quad_shaded },
    { "cube, flat", scene_cube_flat },
    { "cube, textured", scene_cube_textured },
    { "cube, textured, fog", scene_cube_fog }
};

static uint32_t checksum(void) {
    uint32_t hash = 2166136261u;
    size_t i;

    for (i = 0; i < (size_t)BENCH_W * BENCH_H; ++i) {
        hash = (hash ^ e.fb[i]) * 16777619u;
    }
    return hash;
}

static long painted(void) {
    long count = 0;
    size_t i;

    for (i = 0; i < (size_t)BENCH_W * BENCH_H; ++i) {
        count += e.fb[i] != 0;
    }
    return count;
}

/* A whole positive number and nothing after it, or 0. */
static int parse_frames(const char* text) {
    char* end;
    long value = strtol(text, &end, 10);

    if (end == text || *end != '\0' || value <= 0 || value > 1000000L) {
        return 0;
    }
    return (int)value;
}

/* Output is the point of the program, but a closed stdout is no reason to
   stop measuring, so what printf returns is not looked at. */
int main(int argc, char** argv) {
    const KEK_desc desc = { .width = BENCH_W, .height = BENCH_H };
    const char* dump_path = 0;
    FILE* dump = 0;
    int frames = 100;
    size_t s;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
            dump_path = argv[++i];
        } else if (parse_frames(argv[i]) > 0) {
            frames = parse_frames(argv[i]);
        } else {
            (void)fprintf(stderr, "usage: %s [frames] [--dump FILE]\n", argv[0]);
            return 2;
        }
    }

    if (!kek_init(&e, &desc, memory, sizeof(memory))) {
        (void)fprintf(stderr, "kek_init failed\n");
        return 1;
    }
    flat_cube = KEK_CUBE_MODEL;
    flat_cube.uvs = 0;
    flat_cube.face_uvs = 0;
    flat_cube.uvs_count = 0;
    flat_cube.face_uvs_count = 0;
    if (dump_path) {
        dump = fopen(dump_path, "wb");
        if (!dump) {
            (void)fprintf(stderr, "cannot write %s\n", dump_path);
            return 1;
        }
    }

    (void)printf("%d frames of %dx%d, best of %d rounds\n\n", frames, BENCH_W, BENCH_H, BENCH_ROUNDS);
    (void)printf("%-24s %10s %10s %10s\n", "scene", "ms/frame", "painted", "checksum");
    for (s = 0; s < sizeof(SCENES) / sizeof(SCENES[0]); ++s) {
        double best = -1.;
        int round;

        for (round = 0; round < BENCH_ROUNDS; ++round) {
            clock_t start = clock();
            double ms;
            int frame;

            for (frame = 0; frame < frames; ++frame) {
                kek_flush_buffers(&e);
                SCENES[s].draw(frame);
            }
            ms = (double)(clock() - start) * 1000. / (double)CLOCKS_PER_SEC / (double)frames;
            if (best < 0. || ms < best) {
                best = ms;
            }
        }
        (void)printf("%-24s %10.3f %10ld   %08lx\n", SCENES[s].name, best, painted(), (unsigned long)checksum());
        if (dump && fwrite(e.fb, 1, (size_t)BENCH_W * BENCH_H, dump) != (size_t)BENCH_W * BENCH_H) {
            (void)fprintf(stderr, "cannot write %s\n", dump_path);
            (void)fclose(dump);
            return 1;
        }
    }

    /* A write stream: its close is where a full disk shows up. */
    if (dump && fclose(dump) != 0) {
        (void)fprintf(stderr, "cannot write %s\n", dump_path);
        return 1;
    }
    return 0;
}
