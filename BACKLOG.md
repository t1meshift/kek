# Backlog

What is missing, why it matters and in what order to do it. Tiers are priorities,
not schedules: Tier 0 is the current focus, everything below it is ordered by what
unblocks what. Items marked `needs a decision` are waiting on a call, not on work.

## Settled decisions

These shape several items below, so they are recorded once here.

| Question | Decision |
| --- | --- |
| Licence | All rights reserved. The game moves to a private repository; the engine stays open |
| Target machine | Pentium as the primary target, able to run on a 486 |
| Arithmetic | Fixed point. A 486 has no FPU/integer overlap, and a 486SX has no FPU at all |
| Tests | Unity (ThrowTheSwitch), C99, fetched like SDL3 and Dear ImGui, driven by CTest. The harness has to build for foreign targets too, which rules out anything needing `fork` or a prebuilt library |
| Frame comparison | No golden images while rendering is float — see the transcendentals item in Tier 3 |
| Level format | `.klf`, magic `KLVL`, by analogy with `.kmf`/`KMDL` and `.kif`/`KIMG` |
| Audio | Own format, most likely sound banks |

## Tier 0 — Foundation

- **No LICENSE file.** A public repository with a live demo and no statement of terms. All rights reserved.

- **`kek_set_shading_palette()` overflows its destination by 8×.** [kek.c#L50](kek/kek.c#L50) copies
  `256 * KEK_PALETTE_SHADING_LEVELS * sizeof(KEK_palette_item)` — 8192 bytes — into `KEK_SHADING_PALETTE`,
  which is `uint8_t[256 * KEK_PALETTE_SHADING_LEVELS]`, 1024 bytes. Latent only because nothing calls the
  function; it is declared in the public header. The root cause is a type error: a shading palette is a
  table of palette *indices*, so the parameter should be `const uint8_t*` and the size should carry no
  `sizeof(KEK_palette_item)` at all. Exactly the class of defect the tests below exist to catch.

- **`KEK_palette_item` does not lay out the way the code reads.** `sizeof` is 8, not 4: each 6-bit channel
  gets its own byte and five bytes are padding, so a 256-entry palette costs 2 KB. The `color:18` union
  member is lossy — writing `b = 63` reads back as `0x30000`, because only the low two bits of `b` fall
  inside the 18-bit window. Nothing reads `.color` today, so this is a trap rather than a live bug.

  The fix is a plain three-byte struct:

  ```c
  typedef struct KEK_palette_item {
      uint8_t r, g, b;   /* 0..63 — VGA DAC range */
  } KEK_palette_item;
  ```

  `sizeof` is then 3 *by the standard* rather than by observation — every member is char-aligned, so no
  padding is possible. That leaves the category of implementation-defined layout entirely, which the
  bitfield version never could. 256 entries become exactly 768 bytes: the VGA palette block, so a DOS port
  writes it with `rep outsb` straight to port `0x3C9` with no repacking. Scope: 25 `.channels` sites
  (6 in [kek_palette.c](kek/kek_palette.c), 1 in [main_sdl.c](platform/main_sdl.c), 18 in
  [palette_grid.h](tools/kek_editor/components/palette_grid.h)), the 64-line default table whose fourth
  initializer is always a literal `0`, and static asserts on `sizeof == 3` and `sizeof(item[256]) == 768`.
  `needs a decision`: keep the `.channels.r` spelling by nesting the struct, or flatten to `palette[i].r`
  and touch those 25 sites. Worth doing in the same commit as the overflow above — same file, same
  confusion between colours and indices.

- **No compiler warnings are enabled.** [CMakeLists.txt](CMakeLists.txt) has no `target_compile_options` at
  all. Add `-Wall -Wextra -Wpedantic` (`/W4` on MSVC) to the engine, the game and the platform layer, then
  spend a pass clearing what falls out.

- **`.clang-tidy` is empty** (0 bytes). Fill it with a narrow, strict set — `bugprone-*`,
  `clang-analyzer-*`, `portability-*`, `cert-*` — and give `tools/kek_editor/` its own config with a
  subset of `modernize-*`. Non-blocking in CI first, blocking once clean.

- **No tests.** The engine is full of pure, trivially testable functions:
  [kek_math.c](kek/kek_math.c) (`kek_mat3_from_euler`, `kek_normalize_fvec3`, `kek_area_triangle_signed`),
  [kek_palette.c](kek/kek_palette.c) (`kek_palette_nearest_color`, `kek_palette_calculate_shading`, and the
  layout questions above), `kek_2d_clip_line` at [kek_2d.c#L10](kek/kek_2d.c#L10),
  [kek_pool.c](kek/kek_pool.c) (handle encoding, generation reuse, refusal past capacity), and the
  [KMF](kek/kek_file_model.c) and [KIF](kek/kek_file_image.c) parsers against truncated and malformed input.

- **Rendering is testable without golden images.** `KEK_engine.fb` and `.db` are pointers, so a test can
  supply its own buffer with guard bytes around it and assert the things that do not depend on float:
  the rasterizer never writes outside the frame, the depth test rejects what is further away,
  `kek_texture_sample` is correct at the edges under both `CLAMP` and `REPEAT`, and near-plane clipping
  yields 0, 3 or 4 vertices.

- **No in-memory `KEK_AssetProvider`.** Needed to exercise the parsers without touching the filesystem.
  The vtable is already defined in [kek_asset.h](kek/include/kek_asset.h); this belongs in the engine
  rather than in `platform/` — see Tier 1.

- **CI builds the web demo only** ([web-demo.yml](.github/workflows/web-demo.yml)). Add a native workflow:
  Linux GCC and Clang, Windows MSVC, warnings as errors, CTest.

- **The simulation step ignores real time.** `kek_update()` always passes `1000.f / target_fps`
  ([kek.c#L122](kek/kek.c#L122)) and `SDL_AppIterate` spins on `SDL_Delay(1)`
  ([main_sdl.c](platform/main_sdl.c)). Move to real dt with an upper clamp. Knock-on effect:
  `GAME_EntryScene_render` derives its rotation from `ticks / target_fps`, which stops being time — it
  needs accumulated seconds instead of a frame counter.

- **No input state API.** The engine only delivers `key_down`/`key_up` events, so every scene keeps its own
  bitmask — see `_GAME_EntryScene_Movement` at
  [game_scene_entry.c#L15](game/game_scene_entry.c#L15). Two bitsets over `KEK_SCANCODE_SIZE = 512`
  (current and previous frame, for edge detection) cost 128 bytes against the ~2.6 MB already in BSS, so
  static memory is not an argument against it. Mouse and gamepad later.

- **Leaked texture slot.** [kek_file_model.c#L242](kek/kek_file_model.c#L242) drops the handle returned by
  `kek_file_image_load()` and stores only the raw `KEK_texture*`, so the pool slot can never be released.
  `KEK_model` should hold a `KEK_TextureHandle` and `kek_model_destroy` should free it.

- **~50 KB of stack in the model loader.** The locals at
  [kek_file_model.c#L63](kek/kek_file_model.c#L63) are why the web build needs `-sSTACK_SIZE=1048576`.
  Move them to a static scratch buffer, in keeping with the no-dynamic-allocation rule.

- **On-disk layout rests on compiler padding.** `KEK_FileModel_Header` is read with a single
  `read(sizeof(hdr))` and has to match `KMDL_HEADER_STRUCT = "<4sHHHHHHHBx"` in
  [obj_to_kmf.py](scripts/obj_to_kmf.py). It does — both are 20 bytes — but nothing enforces it. Add
  `KEK_STATIC_ASSERT` on both header sizes and on `KEK_palette_item`, and write `docs/formats.md`.

- **`kek_blit()` and `kek_line()` do not clip** ([kek.c#L81](kek/kek.c#L81)). They are fast paths and should
  stay that way, but they do not belong in the public [kek.h](kek/include/kek.h). Move them to an internal
  header: the only callers are `kek_2d.c` and `kek_3d.c`, and both clip already.

## Tier 1 — Splitting the engine from the game

The game moves to a private repository and the engine stays open. The game is 300 lines today and the
separation only gets more expensive. Preparation matters more than the move itself.

- **The platform layer is wired to the game.** [main_sdl.c#L90](platform/main_sdl.c#L90) calls
  `GAME_init_ctx()` from `<game.h>` directly. Nothing leaves the repository until that is a callback or a
  config field instead of a known symbol.
- **`GAME_NAME` is the CMake project name** and the prefix of every target. The project is the engine; the
  game is a consumer.
- **Reorganise the sources.** `kek/io/kek_asset_memory.c` (pure C, no file I/O — engine material),
  `kek/io/kek_asset_stdio.c` behind `KEK_WITH_STDIO_ASSETS` (stdio is not present on every target the
  engine aims at — this is today's [platform_assets_fs.c](platform/platform_assets_fs.c)), and
  `platform/sdl3/` for the OS backend.
- **Make the engine installable** — install rules and an export set, so an out-of-tree game can consume it
  through `find_package(kek)` or FetchContent.
- **A sample in the public repository.** [web-demo.yml](.github/workflows/web-demo.yml) builds the game
  today; the demo has to survive the move, so `samples/` needs a minimal target that also exercises the
  platform layer.
- **Split the assets.** [assets/](assets) currently mixes engine samples and game content.

## Tier 2 — Target machine: Pentium, running on a 486

Budget: fit comfortably into ~4 MB including assets. The engine occupies ~2.6 MB today while holding
roughly 20 KB of actual data.

| | Bytes |
| --- | --- |
| Model pool, 16 slots | 1,394,176 |
| Texture pool, 16 slots | 1,048,960 |
| Depth buffer, `float` | 256,000 |
| Framebuffer | 64,000 |
| Palette + shading palette | 3,072 |

- **Worst-case slots are the whole story.** A `KEK_ModelPoolSlot` is 87,136 bytes whatever it holds: 1024
  vertices, 1024 faces, 1024 normals, 1024 colours, 1024 UVs. The built-in cube — 8 vertices, 12 faces,
  [kek_model.c#L149](kek/kek_model.c#L149) — is 972 bytes of real data, an overhead of **×90**. A texture
  slot is 65,560 bytes against 256 bytes for the default 16×16 texture, **×256**. Replace N worst-case
  slots with one static arena allocated sequentially with marks — the Quake `Hunk_Alloc` model, reset on
  level change. This is still "no dynamic allocation" in the sense the project means: a bump allocator
  over a static array, deterministic, no free list, no fragmentation.

  The expensive part is the API, not the allocator. `kek_model_create(e)` hands back a worst-case slot;
  an arena has to know the size up front, so it becomes `kek_model_create(e, verts, faces, flags)`. And
  `kek_model_destroy` has no meaning in a bump allocator — there are 14 calls to the destroy functions,
  6 in [kek_file_model.c](kek/kek_file_model.c), 6 in [kek_file_image.c](kek/kek_file_image.c), 2 in the
  game. What rescues it: almost all of them are LIFO by construction, since the loaders free exactly what
  they just built on an error path. So a two-ended arena with marks works — permanent data (default cube
  and texture, fonts) from the low end, per-level data from the high end, released wholesale on level
  change, plus `kek_arena_mark()`/`kek_arena_release(mark)`. `destroy` genuinely frees when the block is
  the most recent allocation, which covers every loader error path without touching their logic, and
  otherwise just invalidates the handle until the next reset. Handles with generation counters stay, and
  matter *more* under an arena, not less: they are the only thing that turns a use-after-release into a
  detectable error instead of a corrupted triangle three weeks later.

  Sequence: the pool tests from Tier 0 come first. Swapping the allocator underneath this without them is
  how that corrupted triangle happens.
- **Normals are the largest array in the engine and nothing reads them.** `KEK_model_face_normal` is
  3 × `KEK_FVec3` = 36 bytes per face, 36,864 bytes per slot — 42% of a model — and `kek_3d_draw_model`
  never touches them. KMF already stores them indexed (`KEK_FileModel_FaceVertex.normal`) and the loader
  expands them. Store one normal per face, or derive them at load;
  `kek_file_model_calculate_face_normal` already exists. UVs have the same shape: indexed on disk,
  24 bytes per face in memory.
- **Quantise vertices.** Quake's MDL format stored positions as `uint8` with a per-model scale and offset —
  four times smaller than float, and a natural step toward fixed point.
- **Fixed point.** Go through a `kek_scalar` typedef and a small operation set, keeping the float build as
  a reference to diff against during the transition. It reaches `KEK_FVec2`/`KEK_FVec3` and therefore
  nearly every public struct, plus [kek_3d.c](kek/kek_3d.c), [kek_2d.c](kek/kek_2d.c),
  [kek_math.c](kek/kek_math.c), the converters and the editor. On-disk formats can stay float and convert
  at load — the loader already visits every vertex.
- **Order of work.** Remove the per-pixel divides and move depth to `uint16` first: both are worth doing
  regardless of arithmetic. Then the arena and the data structures. Fixed point last — it is the most
  cross-cutting change and far easier on top of structures that have already shrunk.

## Tier 3 — Engine: prerequisites for levels

- **There is no lighting.** The shading palette is computed at init
  (`kek_invalidate_shading_palette`) and face normals are loaded or generated
  ([kek_file_model.c#L39](kek/kek_file_model.c#L39)) — and `kek_3d_draw_model` uses neither. Everything
  renders as flat colour or unlit texture. Flat or Gouraud shading through the existing
  `kek_palette_shade()` is the largest visual return for the least code in the whole backlog.
- **Nothing draws an image in 2D.** [kek_2d.h](kek/include/kek_2d.h) has primitives and 5×8 text but no way
  to put a `KEK_texture` into the framebuffer, which blocks HUD, menus, backgrounds, sprites and
  billboards. Needs `kek_2d_blit_texture()` and a transparent colour index.
- **No scale, no transform type.** `kek_3d_draw_model(e, mdl, camera, pos, rotation)`
  ([kek_3d.h](kek/include/kek_3d.h)) — required before anything can be placed in a world.
- **Divides in the per-pixel loop** in `kek_3d_triangle` and `kek_3d_triangle_textured` (`w0 / area`,
  `u_over_z / inv_z`). Precompute `1/area` and move to affine spans with subdivision — the technique Quake
  used to hide one divide behind sixteen pixels. Measure first; there is no profiling harness either.
- **The depth buffer is the single largest allocation.** 320×200×4 = 250 KB against 62.5 KB for the frame
  itself. Quantised `1/z` in `uint16` halves it and cuts memory traffic in the hot loop. Quake used a
  16-bit z-buffer at this resolution, and only for alias models.
- **Resolution is hardcoded.** `KEK_BUFFER_WIDTH`/`KEK_BUFFER_HEIGHT` are `#define`s at
  [kek.c#L9](kek/kek.c#L9) even though `e->w`/`e->h` are already struct fields. Move them to
  `kek_config.h` with the other knobs — a prerequisite for any small target.
- **Own transcendentals.** 28 libm calls across four files: `sinf`/`cosf` (15), `roundf` (5), `tanf` (2),
  `sqrtf`, `floorf`, `fabsf`. Table-driven replacements drop the libm dependency and, more importantly,
  make rendering bit-reproducible across toolchains — libm accuracy is not specified, unlike `+ - * /`
  and `sqrt`. Together with `-ffp-contract=off` and no fast-math this puts golden-frame tests back on the
  table.

## Tier 4 — Levels (`.klf`)

There is no world as a concept. Scenes are compiled-in C structs, `GAME_init_scene()`
([game_scene_loader.c](game/game_scene_loader.c)) is a switch with one case, `GAME_SCENETAG_MENU` and
`GAME_SCENETAG_SETTINGS` are declared in [game_tag.h](game/include/game_tag.h) and never implemented, and
rendering is a single hardcoded `kek_3d_draw_model` call.

- An entity/instance concept in the engine: a pooled array of `{model handle, texture handle, transform,
  flags}` and a draw pass over it, following the existing `KEK_ModelPool` pattern.
- The `.klf` format, magic `KLVL`: header, model table (paths), instance table (transform plus model
  index), camera spawn. Same discipline as KMF — fixed-size records, bounds-checked, no allocation.
  Specified in the same `docs/formats.md`.
- `kek_file_level_load()`, tested through the in-memory provider from Tier 0.
- An exporter in `scripts/`, from a level source or from the editor.
- Collision and spatial queries — nothing makes a level walkable. AABBs and a ray cast against model
  bounds are enough to start.
- Split the camera/player controller out of the scene; `GAME_EntryScene_update` currently does both.

## Tier 5 — Editor

- **`SceneEditor` and `ModelEditor` are stubs** — labels only, see
  [scene_editor.h](tools/kek_editor/tools/scene_editor.h) and
  [model_editor.h](tools/kek_editor/tools/model_editor.h). The shell, the tool-plugin architecture and
  `DemoTool` with its palette editor all work.
- **The keystone is missing: nothing renders the engine framebuffer into ImGui.** Both tools need it and
  it is written once, as `components/engine_viewport.h`. `app_build_palette()` at
  [main_sdl.c#L35](platform/main_sdl.c#L35) is the same palette expansion, so it should move into the
  engine as `kek_palette_to_rgbx8888` and be shared.
- **No file open/save** — no KMF/KIF import, no `.klf` output. SDL3 has `SDL_ShowOpenFileDialog`.
- **The editor is not built in CI** (it is deliberately absent from the web build).

## Tier 6 — Game (private repository)

- **There is no shooter in SnusShooter.** One scene: a rotating cat, debug palette bars and camera
  coordinates ([game_scene_entry.c](game/game_scene_entry.c)). No menu, no HUD, no weapons, no enemies,
  no game loop, no win or lose.
- **Cleanup once entities exist**: the dead `if (!e->assets)` at
  [game_scene_entry.c#L79](game/game_scene_entry.c#L79) and the commented-out multi-model draw.

## Tier 7 — Later

- **No audio at all**; `SDL_AUDIO` is explicitly disabled in the web build
  ([CMakeLists.txt](CMakeLists.txt)). Own format by analogy with KMF/KIF, most likely sound banks — a set
  of short samples in one file behind a shared table, to save space.
- **A DOS platform layer.** The target is named and the frame format already suits it: VGA mode 13h is
  exactly 320×200 at 256 colours, so presenting is a copy to `0xA0000`, and the 0–63 channel range of
  `KEK_palette_item` is the VGA DAC range (ports `0x3C8`/`0x3C9`) — though see Tier 0 on its in-memory
  layout. Build with DJGPP. Other platform layers per the README's "as many platforms as possible"; SDL3
  is the only one so far.
- **Scripts**: no `requirements.txt` (Pillow is needed), no round-trip tests for `obj_to_kmf` or
  `bmp_to_kif`. [palette_to_bmp.py](scripts/palette_to_bmp.py) also embeds its own copy of the default
  palette as a string and parses it with a regex, so the table has two sources of truth already —
  independent of the layout change in Tier 0, but it will need the same edit.
- **Docs**: CONTRIBUTING.
