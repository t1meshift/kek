/* How long the rasterisers take, scene by scene, at 320x200.

   Plain C99 over the library and nothing else: timing is clock(), so the same
   file builds for the target and runs under an emulator or on the machine.
   Under DJGPP it is uclock() instead, which reads the timer chip to the
   microsecond: DOS's clock() ticks every 55 ms, which is a tenth of what a
   change to the span loop moves.
   Each scene draws the same frames every round, clearing the frame first, and
   the fastest of the rounds is reported: on a desktop the slower ones are
   other processes, not the code.

   Each scene also prints a checksum of its last frame and how many pixels it
   painted, so a change meant to be invisible can be seen to be, and one that
   is not can be measured. --dump writes those last frames, one after another,
   raw, for comparing two builds byte by byte.

       kek_bench_raster [frames] [--dump FILE] [--assets DIR]

   DIR holds the demo's cat.kmf and Dingus.kif; without it the scenes that
   draw the cat are skipped.

   frames per round, default 100. */

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include "kek.h"
#include "kek_2d.h"
#include "kek_3d.h"
#include "kek_asset_memory.h"
#include "kek_file_model.h"
#include "kek_model.h"
#include "kek_texture.h"

#define BENCH_W 320
#define BENCH_H 200
#define BENCH_ROUNDS 5

#ifdef __DJGPP__
/* <time.h> declares these only outside strict C99, which is how this builds. */
typedef long long uclock_t;
uclock_t uclock(void);
#define BENCH_SECONDS() ((double)uclock() / 1193180.)
#else
#define BENCH_SECONDS() ((double)clock() / (double)CLOCKS_PER_SEC)
#endif

/* Room for the cat, its 256x256 texture, and the staging to load them. */
static unsigned char memory[KEK_MEMORY_SIZE(BENCH_W, BENCH_H) + (size_t)256u * 1024u];
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

/* The demo's cat, when --assets names the directory it is in, drawn as the
   demo draws it and then taken apart: behind the camera, where every face is
   transformed, lit and clipped away, so what is left is the per-vertex and
   per-face work; and far off, a few hundred pixels, where it is the setup of
   each triangle and row. The files are read with stdio into memory and
   handed to the engine through the in-memory provider, so the library still
   sees no file system. */
#define BENCH_CAT_FILE_MAX ((size_t)32u * 1024u)
#define BENCH_TEXTURE_FILE_MAX ((size_t)80u * 1024u)

static unsigned char cat_file[BENCH_CAT_FILE_MAX];
static unsigned char texture_file[BENCH_TEXTURE_FILE_MAX];
static KEK_MemoryAsset cat_assets[2];
static KEK_MemoryAssetProvider cat_provider;
static KEK_model* cat;

/* dir/name into buffer, or 0 if it cannot be read or does not fit. */
static size_t read_file(const char* dir, const char* name, unsigned char* buffer, size_t capacity) {
    char path[512];
    FILE* file;
    size_t size;

    if ((size_t)snprintf(path, sizeof(path), "%s/%s", dir, name) >= sizeof(path)) {
        return 0;
    }
    file = fopen(path, "rb");
    if (!file) {
        return 0;
    }
    size = fread(buffer, 1, capacity, file);
    if (size == capacity || ferror(file)) {
        size = 0;
    }
    (void)fclose(file);
    return size;
}

/* The texture's name is the one cat.kmf gives. */
static void load_cat(const char* dir) {
    cat_assets[0].path = "cat.kmf";
    cat_assets[0].bytes = cat_file;
    cat_assets[0].size = read_file(dir, "cat.kmf", cat_file, sizeof(cat_file));
    cat_assets[1].path = "Dingus.kif";
    cat_assets[1].bytes = texture_file;
    cat_assets[1].size = read_file(dir, "Dingus.kif", texture_file, sizeof(texture_file));
    if (cat_assets[0].size == 0 || cat_assets[1].size == 0) {
        return;
    }
    kek_asset_memory_init(&cat_provider, cat_assets, 2);
    e.assets = &cat_provider.base;
    cat = kek_model_get(&e, kek_file_model_load(&e, "cat.kmf"));
}

/* The demo's view: the default camera, the cat turning in front of it, fog
   from 6 to 25. */
static void draw_cat(int frame, float z) {
    KEK_camera camera = KEK_DEFAULT_CAMERA;

    kek_3d_set_fog(&e, 6.f, 25.f);
    kek_3d_draw_model(&e, cat, &camera, (KEK_FVec3){ 0.f, -1.f, z },
                      (KEK_FVec3){ 0.f, (float)frame * 0.05f, 0.f });
    kek_3d_set_fog(&e, 0.f, 0.f);
}

static void scene_cat(int frame) {
    draw_cat(frame, 4.5f);
}

static void scene_cat_behind(int frame) {
    draw_cat(frame, -4.5f);
}

static void scene_cat_far(int frame) {
    draw_cat(frame, 30.f);
}

/* Close enough to fill most of the frame: big triangles, the worst the demo
   shows. */
static void scene_cat_close(int frame) {
    draw_cat(frame, 2.4f);
}

/* The demo's 2D over the 3D: a palette strip with its numbers and the
   camera's position. */
static void scene_overlay(int frame) {
    char text[128];
    int i;

    for (i = 0; i < 16; ++i) {
        (void)snprintf(text, sizeof(text), "%d", i);
        kek_2d_rect(&e, (KEK_IVec2){ 0, i * 8 }, (KEK_IVec2){ 8, (i + 1) * 8 }, (uint8_t)i);
        kek_2d_text_5x8(&e, &KEK_FONT_DEFAULT_5X8, (KEK_IVec2){ 10, i * 8 }, text, 15);
    }
    (void)snprintf(text, sizeof(text), "x: %.02f\ny: %.02f\nz: %.02f", (double)frame, 0., 0.);
    kek_2d_text_5x8(&e, &KEK_FONT_DEFAULT_5X8, (KEK_IVec2){ 30, 8 }, text, 9);
}

/* A room to stand in, the way a level's first room is drawn until something
   smarter decides what to skip: the inside of a box, 12 x 5 x 16 units with
   the camera at its centre, every wall a grid of quads and every quad two
   triangles textured with the default texture. The grid is 5 or 15 quads a
   side, 300 or 2,700 triangles in all, which is what tells what a triangle
   costs from what a pixel does. The second draws a copy of the room behind
   its far wall too: nothing the camera can see, but with no visibility test
   it is transformed, culled, set up and rejected by depth all the same. */
#define ROOM_CELL_MAX 15
#define ROOM_WALLS 6
#define ROOM_VERTS_MAX (ROOM_WALLS * (ROOM_CELL_MAX + 1) * (ROOM_CELL_MAX + 1))
#define ROOM_FACES_MAX (ROOM_WALLS * ROOM_CELL_MAX * ROOM_CELL_MAX * 2)

typedef struct Room {
    KEK_model model;
    KEK_model_vertex verts[ROOM_VERTS_MAX];
    KEK_model_face faces[ROOM_FACES_MAX];
    KEK_model_face_uv face_uvs[ROOM_FACES_MAX];
} Room;

static Room room_small, room_big;
static KEK_FVec2 room_uvs[4] = { {0.f, 0.f}, {1.f, 0.f}, {0.f, 1.f}, {1.f, 1.f} };

/* Each wall has an inward normal n and two axes u, v with u x v = n, so that
   the triangles (p00, p10, p01) and (p10, p11, p01) wind the way the
   back-face cull keeps them from inside. */
static void build_room(Room* r, int cells, KEK_TextureHandle texture) {
    /* Per wall: the axis held fixed and its side (0 or 255), then u and v. */
    static const int WALL[ROOM_WALLS][4] = {
        { 0, 0, 1, 2 }, { 0, 255, 2, 1 }, { 1, 0, 2, 0 },
        { 1, 255, 0, 2 }, { 2, 0, 0, 1 }, { 2, 255, 1, 0 }
    };
    int wall, i, j, nv = 0, nf = 0;
    int side = cells + 1;

    for (wall = 0; wall < ROOM_WALLS; ++wall) {
        int base = nv;

        for (j = 0; j <= cells; ++j) {
            for (i = 0; i <= cells; ++i) {
                int c[3];
                c[WALL[wall][0]] = WALL[wall][1];
                c[WALL[wall][2]] = i * 255 / cells;
                c[WALL[wall][3]] = j * 255 / cells;
                r->verts[nv].x = (uint8_t)c[0];
                r->verts[nv].y = (uint8_t)c[1];
                r->verts[nv].z = (uint8_t)c[2];
                ++nv;
            }
        }
        for (j = 0; j < cells; ++j) {
            for (i = 0; i < cells; ++i) {
                uint16_t p00 = (uint16_t)(base + j * side + i), p10 = (uint16_t)(p00 + 1);
                uint16_t p01 = (uint16_t)(p00 + side), p11 = (uint16_t)(p01 + 1);
                r->faces[nf] = (KEK_model_face){ p00, p10, p01 };
                r->face_uvs[nf++] = (KEK_model_face_uv){ 0, 1, 2 };
                r->faces[nf] = (KEK_model_face){ p10, p11, p01 };
                r->face_uvs[nf++] = (KEK_model_face_uv){ 1, 3, 2 };
            }
        }
    }
    r->model.verts = r->verts;
    r->model.scale = (KEK_FVec3){ 12.f / 255.f, 5.f / 255.f, 16.f / 255.f };
    r->model.offset = (KEK_FVec3){ -6.f, -2.5f, -8.f };
    r->model.faces = r->faces;
    r->model.face_colors = 0;
    r->model.uvs = room_uvs;
    r->model.face_uvs = r->face_uvs;
    r->model.texture = texture;
    r->model.owns_texture = 0;
    r->model.verts_count = (uint16_t)nv;
    r->model.faces_count = (uint16_t)nf;
    r->model.colors_count = 0;
    r->model.uvs_count = 4;
    r->model.face_uvs_count = (uint16_t)nf;
}

/* The camera stays at the room's centre and turns, as a player looking round;
   the room itself stays put, so a copy z units along is still behind a wall. */
static void draw_room(int frame, KEK_model* model, float z) {
    KEK_camera camera = KEK_DEFAULT_CAMERA;

    camera.rotation.y = (float)frame * 0.05f;
    kek_3d_draw_model(&e, model, &camera, (KEK_FVec3){ 0.f, 0.f, z }, (KEK_FVec3){ 0.f, 0.f, 0.f });
}

static void scene_room_small(int frame) {
    draw_room(frame, &room_small.model, 0.f);
}

static void scene_room_big(int frame) {
    draw_room(frame, &room_big.model, 0.f);
}

static void scene_room_hidden(int frame) {
    draw_room(frame, &room_small.model, 0.f);
    draw_room(frame, &room_small.model, 16.f);
}

typedef struct Scene {
    const char* name;
    void (*draw)(int frame);
    int needs_cat;
} Scene;

static const Scene SCENES[] = {
    { "clear only", scene_clear, 0 },
    { "flat quad", scene_flat_quad, 0 },
    { "textured quad", scene_textured_quad, 0 },
    { "textured quad, shaded", scene_textured_quad_shaded, 0 },
    { "cube, flat", scene_cube_flat, 0 },
    { "cube, textured", scene_cube_textured, 0 },
    { "cube, textured, fog", scene_cube_fog, 0 },
    { "demo's 2D overlay", scene_overlay, 0 },
    { "cat, as in the demo", scene_cat, 1 },
    { "cat, behind the camera", scene_cat_behind, 1 },
    { "cat, far off", scene_cat_far, 1 },
    { "cat, close up", scene_cat_close, 1 },
    { "room, 300 triangles", scene_room_small, 0 },
    { "room, 2700 triangles", scene_room_big, 0 },
    { "room and a hidden one", scene_room_hidden, 0 }
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
    const char* assets_dir = 0;
    FILE* dump = 0;
    int frames = 100;
    size_t s;
    int i;

    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--dump") == 0 && i + 1 < argc) {
            dump_path = argv[++i];
        } else if (strcmp(argv[i], "--assets") == 0 && i + 1 < argc) {
            assets_dir = argv[++i];
        } else if (parse_frames(argv[i]) > 0) {
            frames = parse_frames(argv[i]);
        } else {
            (void)fprintf(stderr, "usage: %s [frames] [--dump FILE] [--assets DIR]\n", argv[0]);
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
    build_room(&room_small, 5, kek_default_texture_handle(&e));
    build_room(&room_big, 15, kek_default_texture_handle(&e));
    if (assets_dir) {
        load_cat(assets_dir);
        if (!cat) {
            (void)fprintf(stderr, "cannot load the cat from %s\n", assets_dir);
            return 1;
        }
    }
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

        if (SCENES[s].needs_cat && !cat) {
            (void)printf("%-24s %10s\n", SCENES[s].name, "no --assets");
            continue;
        }

        for (round = 0; round < BENCH_ROUNDS; ++round) {
            double start = BENCH_SECONDS();
            double ms;
            int frame;

            for (frame = 0; frame < frames; ++frame) {
                kek_flush_buffers(&e);
                SCENES[s].draw(frame);
            }
            ms = (BENCH_SECONDS() - start) * 1000. / (double)frames;
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
