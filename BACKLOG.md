# Backlog

What is missing, why it matters and in what order to do it. Tiers are priorities,
not schedules: Tier 0 is the current focus, everything below it is ordered by what
unblocks what. Items marked `needs a decision` are waiting on a call, not on work.
Finished items move to [Done](#done) at the bottom, one line each plus the commit
that did it — the reasoning lives in the commit message rather than twice here.

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
| 2D coordinates | Screen pixels. `kek_2d_triangle` overflows `int` past ~46,000 px on both axes and stays that way: no screen-space caller gets near it. Noted in [kek_2d.h](kek/include/kek_2d.h) |
| Handle generations | 16 bits per slot, so a stale handle comes back to life after 65,536 reuses of one slot. That is 256 times what San Andreas allowed itself, for far smaller pools; not worth widening |

## Tier 0 — Foundation

Nothing open. Tier 1 is next.

## Tier 1 — Splitting the engine from the game

The game moves to a private repository and the engine stays open. The game is 300 lines today and the
separation only gets more expensive. Preparation matters more than the move itself.

- **The platform layer is wired to the game.** [main_sdl.c#L90](platform/main_sdl.c#L90) calls
  `GAME_init_ctx()` from `<game.h>` directly. Nothing leaves the repository until that is a callback or a
  config field instead of a known symbol.
- **`GAME_NAME` is the CMake project name** and the prefix of every target. The project is the engine; the
  game is a consumer.
- **Reorganise the sources.** `kek/io/kek_asset_memory.c` is already there (pure C, no file I/O —
  engine material). What remains is `kek/io/kek_asset_stdio.c` behind `KEK_WITH_STDIO_ASSETS` (stdio is
  not present on every target the engine aims at — this is today's
  [platform_assets_fs.c](platform/platform_assets_fs.c)), and `platform/sdl3/` for the OS backend.
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
  `kek_model_destroy` has no meaning in a bump allocator — there are 17 calls to the destroy functions,
  6 in [kek_file_model.c](kek/kek_file_model.c), 6 in [kek_file_image.c](kek/kek_file_image.c), 2 in the
  game, and 3 in [kek_pool.c](kek/kek_pool.c) itself, where `kek_model_destroy` now releases a texture
  the model owns. What rescues it: almost all of them are LIFO by construction, since the loaders free exactly what
  they just built on an error path. So a two-ended arena with marks works — permanent data (default cube
  and texture, fonts) from the low end, per-level data from the high end, released wholesale on level
  change, plus `kek_arena_mark()`/`kek_arena_release(mark)`. `destroy` genuinely frees when the block is
  the most recent allocation, which covers every loader error path without touching their logic, and
  otherwise just invalidates the handle until the next reset. Handles with generation counters stay, and
  matter *more* under an arena, not less: they are the only thing that turns a use-after-release into a
  detectable error instead of a corrupted triangle three weeks later.

  The pool tests from Tier 0 are in place, in [test_pool.c](tests/test_pool.c): public API only, no
  slot, capacity or handle bit read, so the arena has to pass them unchanged. The parser suites check
  after every failed load that both pools have the free capacity they had before, which is the same
  property the loaders' LIFO error paths will rely on under marks.
- **Normals are the largest array in the engine and nothing reads them.** `KEK_model_face_normal` is
  3 × `KEK_FVec3` = 36 bytes per face, 36,864 bytes per slot — 42% of a model — and `kek_3d_draw_model`
  never touches them. Lighting does not read them either: it derives the face normal from the vertices at
  draw time (`424f69e`). So the array can go outright rather than shrink, and the loader can stop
  expanding the indexed normals KMF stores (`KEK_FileModel_FaceVertex.normal`). If Gouraud ever comes
  (Tier 7), per-corner normals come back as a byte each, not 12. UVs have the same shape: indexed on
  disk, 24 bytes per face in memory.
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

- **Fog never reaches black.** The shading palette's last row is 1/`KEK_PALETTE_SHADING_LEVELS` of the
  colour, not black, so a face past `fog_end` is dim but visible: at the default four levels, a quarter.
  That is right for a light that only darkens, but wrong for fog into darkness. A black row after the
  last one (or a fog colour, which costs one nearest-colour row per level) would fix it. It is a
  palette-table change, not a rasteriser one.
- **Nothing draws an image in 2D.** [kek_2d.h](kek/include/kek_2d.h) has primitives and 5×8 text but no way
  to put a `KEK_texture` into the framebuffer, which blocks HUD, menus, backgrounds, sprites and
  billboards. Needs `kek_2d_blit_texture()` and a transparent colour index.
- **No scale, no transform type.** `kek_3d_draw_model(e, mdl, camera, pos, rotation)`
  ([kek_3d.h](kek/include/kek_3d.h)) — required before anything can be placed in a world.
- **Divides in the per-pixel loop** in `kek_3d_triangle` and `kek_3d_triangle_textured` (`w0 / area`,
  `u_over_z / inv_z`). Precompute `1/area` and move to affine spans with subdivision — the technique Quake
  used to hide one divide behind sixteen pixels. Measure first; there is no profiling harness.
- **The depth buffer is the single largest allocation.** 320×200×4 = 250 KB against 62.5 KB for the frame
  itself. Quantised `1/z` in `uint16` halves it and cuts memory traffic in the hot loop. Quake used a
  16-bit z-buffer at this resolution, and only for alias models.
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
  index), camera spawn, and the light: per-vertex shade for level geometry, a light per zone, fog. Same
  discipline as KMF — fixed-size records, bounds-checked, no allocation.
  Specified in [docs/formats.md](docs/formats.md), which has a stub section waiting for it.
- **Lighting a level.** The light from `424f69e` is one sun plus ambient, which suits models and
  outdoor scenes, not rooms. Indoors:
  - *The world gets light baked per vertex.* The exporter traces point lights and shadows offline and
    writes one shade byte per vertex into `.klf`. At run time that is the `KEK_3D_ProjectedVertex.shade`
    channel fog already interpolates, with the base taken from the vertex instead of from Lambert. Any
    number of lights and shadows cost nothing at run time. The price is a byte per vertex, and large
    walls have to be split for the gradients to show, as on the PS1.
  - *Models take the light of their zone.* A zone or room in `.klf` carries a direction and ambient,
    and the entity pass calls `kek_3d_set_light` before each model from the zone it stands in. Quake lit
    alias models from the lightmap under them, to the same end.
  - *Dynamic light later*: muzzle flashes and explosions as extra shade for vertices within a radius.
  - *No lightmaps.* They need a surface cache, a unique UV unwrap and a level compiler, and four shading
    levels in 256 colours would throw away most of their resolution. Light brighter than the base
    colour is a separate question: overbright rows in the shading table, as Quake's colormap had.
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
  [game_scene_entry.c#L81](game/game_scene_entry.c#L81) and the commented-out multi-model draw.

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
  `bmp_to_kif`. The default palette has three copies: the engine's table in
  [kek_palette.c](kek/kek_palette.c), a string in [palette_to_bmp.py](scripts/palette_to_bmp.py) parsed
  with a regex, and `ENGINE_PALETTE_888` in [bmp_to_kif.py](scripts/bmp_to_kif.py). All three agree
  today — the first two were checked byte for byte when the item shrank to three channels — but nothing
  keeps them agreeing. Generate the Python copies from the C table, or the C table from a data file.
- **Gouraud as a model option.** `424f69e` shades flat, one shade per face. That suits the look, costs one table
  lookup per pixel and lets the normals go (Tier 2). Smooth models would want a shade per corner, and the
  interpolation is already there for fog. The normals can come back small: Quake stored a vertex normal
  as a byte indexing a table of 162 directions.
- **Docs**: CONTRIBUTING.

## Done

Ordered oldest first. Each line is what the item was, not what replaced it; `git show`
on the hash has the reasoning, the measurements and what was verified.

### Tier 0

| | Was | Commit |
| --- | --- | --- |
| Licence | A public repository with a live demo and no statement of terms | `e1302c4` |
| Palette layout | `KEK_palette_item` was a union whose `sizeof` was 8, not 4, with a lossy `color:18` member; a 256-entry palette cost 2 KB and its layout was implementation-defined. Now a plain three-byte struct, 768 bytes for 256 entries — the VGA palette block — held by static asserts | `9bfb38b` |
| Shading palette overflow | `kek_set_shading_palette()` copied `256 * LEVELS * sizeof(KEK_palette_item)` into a `uint8_t[256 * LEVELS]`, overflowing it eightfold. The parameter is `const uint8_t*` now: a shading palette is a table of indices | `9bfb38b` |
| Leaked texture slot | `kek_file_model_load()` dropped the handle from `kek_file_image_load()` and kept the raw `KEK_texture*`, so the slot could never be released — the 16-slot pool ran out on the 15th load. `KEK_model` holds a handle and an `owns_texture` flag | `c8a20bf` |
| `kek_blit`/`kek_line` in the public header | Both are unclipped fast paths and had no business in `kek.h`. Moved to `kek/kek_internal.h` as `static inline` | `cad397a` |
| Compiler warnings | `CMakeLists.txt` had no `target_compile_options` at all. `-Wall -Wextra -Wpedantic` (`/W4` on MSVC) everywhere but vendored ImGui, behind `KEK_WERROR`; all 370 warnings cleared | `c2f5679` |
| `.clang-tidy` | The file was zero bytes. A narrow set with a reason recorded for every subtraction; the tree is clean against it | `ab7deb3` |
| Native CI | Only the web demo was built. Linux GCC, Linux Clang and Windows MSVC, plus a non-blocking clang-tidy job | `b16f62b` |
| Fixed simulation step | `kek_update()` passed `1000/target_fps` whatever had happened, so a frame that took 80 ms still advanced by 33 and the game ran slow in proportion to how far behind it was. Real dt from the platform layer, clamped to `KEK_MAX_FRAME_MS` and NaN-proof; `SDL_AppIterate` sleeps the remainder instead of waking every millisecond. The game accumulates seconds rather than frames | `18b72f7` |
| `kek_2d_triangle` clamped where it should reject | Off-screen spans collapsed onto the edge they fell off. Worse in y than recorded here: clamping `a.y`/`b.y` into range gave `ay == by == 0`, so a triangle at y = -300 drew a full 320-pixel row along the top. Both axes intersect now, spans go through `kek_2d_span` | `349beae` |
| ~50 KB of stack in the model loader | A worst-case KMF was staged in locals: 51,712 bytes of frame against 672 for the next largest in the engine. Static scratch, 240 bytes of frame, and the browser build's `-sSTACK_SIZE=1048576` is gone | `acbcbb7` |
| On-disk layout rested on compiler padding | The KMF and KIF headers matched their Python writers by coincidence. Size and every field offset asserted, plus the record types; `docs/formats.md` written | `3520df1` |
| `kek_palette.c` not self-contained | It read `KEK_PALETTE_SHADING_LEVELS` without including `kek_config.h`, so only the CMake `-D` made it compile. It includes it now, and the default became a signed `4` like the `-D`: as `4u` it would have clamped a negative shade to the top | `25d41a2` |
| No tests | Unity, fetched like SDL3 and ImGui, driven by CTest, one executable per suite, nothing that needs `fork`, SDL3 or a prebuilt library. Unit suites for the math, palette, line clipper and pools (public API only); KMF and KIF parsers against every truncation, bad magic and version, counts one past each limit and out-of-range indices, with a leak check after every rejection | `f2232be`, `1eefa93`, `3ace570` |
| No in-memory `KEK_AssetProvider` | `kek/io/kek_asset_memory.c`: a table of `{path, bytes, size}` the caller owns, no stdio, no allocation | `9b8a5fa` |
| Rendering untested | A guarded frame the engine renders into; the 2D and triangle hand passes as tests at two frame sizes, the depth test in both draw orders, `kek_texture_sample` at the edges under `CLAMP` and `REPEAT`, `kek_3d_clip_near` at 0, 3 and 4 plus a 5000-triangle sweep, and whole cubes through the near plane | `db60ba8` |
| clang-tidy did not block | The job was `continue-on-error` because the runner's clang-tidy drifted. Pinned to `ubuntu-24.04` and clang-tidy 22 from apt.llvm.org, `WarningsAsErrors: '*'`; the step also reported `tee`'s exit status, so it could never have failed | `4b48250` |
| No input state API | `kek_key_held`/`kek_key_pressed`/`kek_key_released` over three bitsets in `KEK_engine`, 192 bytes rather than the two sketched here: with only current and previous, a tap inside one frame is lost. Edges are cleared after the scene's update and on every scene switch, so each is seen by exactly one update. The entry scene dropped its own bitmask | `ffd9d95`, `9333d8e` |
| Reserved identifiers | `_kek_*`, `_GAME_*` and `_fs_asset_*` claimed names the standard reserves. 27 names in 7 files, not every translation unit; the marker moved to the end (`kek_apply_scene_switch_`) and `bugprone-reserved-identifier` is back on | `583fc89` |
| `KEK_POOL_MODEL_UVS_MAX` meant two things | It sized the per-face `face_textures`, but the loader checked it only against the distinct-UV count; with `UVS_MAX` below `FACES_MAX` a textured model wrote past its slot. `faces_count` is checked too, and a second engine build with small limits proves it under ASan | `89c8fb3` |
| Backface cull overflowed `int` | `kek_area_triangle_signed` multiplied screen coordinates the near clip had put tens of thousands of pixels out. Wraparound hid it until twice the area passed `INT_MAX`: a wall 35 units off and ~80 tall, not the 18 estimated here, and the back of it drew 2761 pixels. Edge form in `double` | `b51c188` |
| `kek_texture_sample` cast NaN | The clamp let NaN through and `REPEAT` made NaN of infinity; both reached a `(uint16_t)` cast. NaN now resolves to 0 in both modes | `470d233` |

### Tier 3

| | Was | Commit |
| --- | --- | --- |
| Resolution hardcoded | `KEK_BUFFER_WIDTH`/`KEK_BUFFER_HEIGHT`/`KEK_TARGET_FPS` were `#define`s at the top of `kek.c`. In `kek_config.h` with the other knobs now, with CMake cache entries | `f99074d` |
| No lighting | The shading palette was computed at init and `kek_3d_draw_model` never used it. Flat shading from a world-fixed directional light plus ambient, with the normal derived from the face; fog with view depth; Bayer 4×4 dither between rows | `424f69e` |

### Found on the way, not from a backlog item

| | Was | Commit |
| --- | --- | --- |
| Null scene dereference | `_kek_apply_scene_switch` reached `e->scene->exit` unguarded, so a `kek_request_scene()` before the first `kek_set_scene()` was a segfault on the next `kek_update()` | `a8e6020` |
| Near-plane clip bound | `kek_3d_clip_near` relied on an unenforced four-vertex bound while writing into a four-element array on the caller's stack. Four is provable; it is checked now | `443b7a4` |
| Zero-size read reported success | `kek_file_model_read_exact` returned true for a zero-size read on a stream with no vtable | `443b7a4` |
| `kek_2d_rect_border` | Drew the same diagonal four times instead of the four edges | `d4776d4` |
| Unclipped 2D primitives | `kek_2d_circle`, `kek_2d_circle_border` and `kek_2d_rect` handed unclipped coordinates to `kek_blit`/`kek_line`. Under ASan: a `negative-size-param` memset of -65455 bytes, a global-buffer-overflow and a SEGV. `kek_2d_span`/`kek_2d_point` are the clipping counterparts | `d4776d4` |
| Undefined shift | `error << 1` in the Bresenham loop of `kek_2d_line`, where `error` is routinely negative | `d4776d4` |
| `KEK_STATIC_ASSERT_DECL` collided with itself | It named its typedef after `__LINE__`, so two headers asserting on the same line number in one translation unit were a duplicate typedef — an error in C99, not a tolerated redeclaration. It would have broken on the second header that used it. Takes an explicit tag now | `3520df1` |
| `kek_palette_shade` read past its table | It clamped the shade to `[0, LEVELS]` and read row `LEVELS` — one row past the end, a global-buffer-overflow under ASan. Nothing calls it yet; lighting would have been first | `eb2b0b6` |
| `kek_2d_clip_line` accepted segments that miss | Ends in two different outside regions got past the trivial reject even when the segment passed clear of a corner, and the final clamp pulled the stray intersections onto the edge: a segment missing the top-left corner drew the whole left column. 2298 of 20000 random segments | `fd4b3b2` |
| `kek_2d_line` dropped its last pixel | The Bresenham loop tested after stepping, so every non-horizontal line stopped one short, and a line drawn the other way drew a different set. Closed borders hid it | `3a66b54` |
