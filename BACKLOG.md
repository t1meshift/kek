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
| Licence | All rights reserved. The game lives in its own private repository; this one keeps the engine and a demo |
| Target machine | Pentium as the primary target, able to run on a 486DX. An FPU is required: no 486SX. 3D on FPU-less machines was all-integer engines with cut-down frames or rates (Ultima Underworld, Doom, Descent), and a 486SX-33 would be ~5 fps even so; building every stage without float for that is not worth it |
| Arithmetic | Quake's split: float for geometry and triangle setup, integers in the span loop, `lrintf` at the boundary. Measured on 86Box (Tier 2): 16.16 geometry is slower than float on a Pentium and level with it on a 486DX; only a 486SX, emulating its FPU at ~200,000 cycles a vertex, would need it. Was fixed point throughout, for the 486SX |
| Tests | Unity (ThrowTheSwitch), C99, fetched like SDL3 and Dear ImGui, driven by CTest. The harness has to build for foreign targets too, which rules out anything needing `fork` or a prebuilt library |
| Frame comparison | No golden images while rendering is float — see the transcendentals item in Tier 3 |
| Level format | `.klf`, magic `KLVL`, by analogy with `.kmf`/`KMDL` and `.kif`/`KIMG` |
| Audio | Own format, most likely sound banks |
| 2D coordinates | Screen pixels. `kek_2d_triangle` overflows `int` past ~46,000 px on both axes and stays that way: no screen-space caller gets near it. Noted in [kek_2d.h](kek/include/kek_2d.h) |
| Library boundary | The library is the product: `kek/` and nothing that needs an OS. Platform layers and asset providers over stdio belong to consumers — the demo has its own, and a game copies them rather than linking against them |
| Memory | The application owns all of it. The library keeps no storage of its own; every buffer it uses is handed to it, so each target decides where memory comes from |
| Distribution | A CMake package: `find_package(kek)`, `kek::kek`. The demo and the editor are separate projects that consume it; the editor ships as binaries through GitHub Releases. DJGPP gets a build file of its own when it comes to that |
| Handle generations | 16 bits per slot, so a stale handle comes back to life after 65,536 reuses of one slot. That is 256 times what San Andreas allowed itself, for far smaller pools; not worth widening |

## Tier 0 — Foundation

Nothing open. Tier 2 is next.

## Tier 1 — The engine as a library

Nothing open. The library builds alone, installs as a package the demo builds against in CI, and
keeps no storage of its own: everything is in the block the application hands `kek_init`.

## Tier 2 — Target machine: Pentium, running on a 486DX

Budget: fit comfortably into ~4 MB including assets. The worst-case pools are gone (`16ffe9d`) and
depth is 16 bits (`6da6808`): a 320×200 engine is a block of `KEK_MEMORY_SIZE(320, 200)` = 202,527
bytes on a 64-bit build and 199,711 on a 32-bit one, plus whatever the application gives its arena.
Nearly all of it is the frame:

| | Bytes |
| --- | --- |
| Depth buffer, `uint16_t` | 128,000 |
| Framebuffer | 64,000 |
| Handle tables, 64 + 64 slots | 8,192 (64-bit), 5,376 (32-bit) |
| Palette + shading palette | 1,792 |
| Default cube and texture | 528 |

Models are a byte an axis per vertex and share their UVs as the file does (`fb92a44`): the demo's cat
is 11,776 bytes in the arena, not counting its texture.

Speed: 20–30 fps at 320×200 on a Pentium 100 is within reach of plain C, and the target for the pixel
loop is ~40 cycles. Measured on 86Box with a standalone span loop (a full-screen wall, z = 1 to 6,
64×64 texture), cycles per pixel:

| Span loop | Pentium 100 | 486DX2-66 |
| --- | --- | --- |
| Flat fill, 16-bit depth | 11 | 19 |
| Affine texture, u and v in 16.16, 16-bit depth | 28 | 52 |
| Perspective every 16 pixels, 16-bit depth | 34 (+2 for 86Box's cheap `fdiv`) | 77 |
| The same without a depth buffer | 24 | 55 |
| The same with the 4×4 dither | 38 | 84 |
| Every pixel rejected by the depth test | 23 | 51 |
| The engine at `0945961`, the benchmark's full-screen wall (16×16 texture) | ~590 | ~1,290 |
| The engine at `035fd37`, the same wall | ~57 | ~130 |
| The engine now (`f50ba29`), the same wall | ~49 | not measured |
| The engine now, the same wall flat | ~17 | ~34 |

A frame of that loop is ~26 ms of painted pixels on the Pentium, ~7 ms more for 1.5× overdraw that the
depth test rejects, 2 ms of clear, 1–2 ms to copy to VGA over PCI: ~25 fps with ~5 ms left for geometry
and the game, ~30 with no overdraw. The 486 lands at 7–10 fps, as Quake did there. The loop is an ideal
case — long spans, one texture in cache, no triangle setup — so the real budget is a few hundred
visible triangles, not thousands. The source is kept outside the repository for now; the DOS build
itself that runs it is `cba7a07`, in [Done](#done).

The demo's own frame, measured: the bench draws its cat and overlay (`cc11002`, `--assets demo/assets`)
and the demo shows its frame rate (`ffdbd4a`). On the emulated Pentium 100, default view, not paced
(`KEK_TARGET_FPS=1000`), it runs at 39.8 fps, ~25 ms a frame:

| | ms |
| --- | --- |
| The cat, as the bench draws it | ~22 |
| Its vertices and faces alone, drawn behind the camera | ~3.6 |
| Its pixels, ~6,700 through the loop for 4,900 painted, at the loop's ~40 cycles | ~2.5 |
| Copying the frame to VGA, S3 Trio64 on PCI | 2.4 |
| The clear | 1.9 |
| The 2D overlay | ~0.5 |

The parts come to ~27 ms against the demo's ~25: the bench turns its cat by frame, the demo by time,
so they do not draw quite the same views. The copy was 62 ms with the VM's first card, an IBM VGA on 8-bit ISA; a VLB Trio64 on the 486 is 2.4
too. With the camera moved up so that the cat fills most of the frame, the paced build reads 20 fps.

- **Small triangles cost their rows, not their pixels.** Counted, the cat is 168 triangles in 1,614
  rows of about four pixels, nearly all one span, and a row costs ~850 cycles on the Pentium: the
  perspective divide at each end of the span, the planes evaluated where it starts, the conversions
  and compares around them, for four pixels of ~40. Quake drew its models affine, with every
  attribute stepped down the edges in fixed point and no float or divide in a row at all
  (`d_polyse.c`), because on a model's small triangles the perspective error does not show. The same
  here, chosen per triangle by its width, took the cat from 25.8 to 21.6 ms (`62981cb`), not the
  ~8–10 the estimate said: the row's divides were a small part of it. Large or deep triangles,
  walls and floors, keep the perspective path. What was measured, on the emulated Pentium 100 with
  the change in and parts of the frame cut out in turn (bench, `--assets`, 50 frames), the cat's
  21.6 ms:

  | Part of the cat's frame | ms |
  | --- | --- |
  | The clear | 1.9 |
  | Vertices: transform, projection, fog (316 of them) | 2.0 |
  | Faces: on-screen test, normal, back-face cull (608) | 2.1 |
  | Faces: light and UVs | 1.0 |
  | Faces: the vertices handed to the rasteriser | 0.4 |
  | Triangle setup, and the walk over the rows with nothing done in them | 5.4 |
  | A row's start: depth, shade, texture ends, steps | 5.8 |
  | The pixel loops | 3.1 |

  The first try, a row's ends from int64 planes, saved 0.8%: 64-bit multiplies are a libgcc call on
  DJGPP, as costly as the divides they replaced. In 32 bits, modulo 2^32 for REPEAT and read signed
  for CLAMP, it was 2.8 ms, and depth and shade as fixed-point planes instead of `kek_3d_row` 1.4
  more. In the demo itself on the Pentium, uncapped: 31–40 fps before, 39–50 after. Frames differ
  from the perspective ones by 5–12 pixels of 64,000. Width 16 rather than 48 costs 0.9 ms. What
  is left is mostly the rasteriser's cost per triangle and per row, not the pixels: 11 ms of the 21.6
  against 3 for the pixel loops.

  Close enough to fill the frame (`cat, close up` in the bench, 20,600 pixels), the demo's worst
  case: 46.3 ms without the affine path, 40.0 at width 48, 36.9 at any width. That is pixel-bound
  at ~130 cycles a pixel where the loop is put at ~40; why is not known yet, overdraw and short
  spans being the candidates.

  A room to stand in, the way a level's first room would be drawn with nothing to skip yet (bench,
  the inside of a 12 × 5 × 16 box, the camera at its centre turning, every wall a grid of quads
  textured with the default texture), on the Pentium 100 at width 48:

  | | ms | fps |
  | --- | --- | --- |
  | 300 triangles | 49.9 | 20 |
  | 2,700 triangles | 90.1 | 11 |
  | 300, and a copy of the room behind the far wall | 60.1 | 17 |

  Every scene fills the frame, so what the 2,400 extra triangles cost, 40 ms, is not fill. Of the
  2,700 only ~640 are drawn at any view; the rest are transformed, culled and dropped, and that
  model side costs ~11 µs a submitted face (vertices, projection, culling, light): about 30 of the
  94 ms. With no visibility test a level costs what all of it costs, not what is seen, which the
  copy behind the wall shows too: nothing visible, 10 ms, a fifth more. The rest, taken apart on
  the 2,700 room with the stages cut out in turn: a drawn triangle's setup ~1,700 cycles (three
  divides for its UVs 210, `kek_3d_setup` 210, the u and v planes 390, `kek_3d_common` 490, the
  fixed-point depth and shade 370), the walk ~100 cycles a row, then ~47 ms of row starts and
  pixels, ~66 cycles a pixel against the ~50 of the ideal loop above. So the setup is ~10% of
  such a frame and the fill half of it. Width at any value takes the 300-triangle room to 42.8
  ms, but on a wall that is a texture swimming, so the perspective path stays past 48.

  The fill taken apart on the textured quad, 64,000 pixels in 33.5 ms on the Pentium: the clear 1.9,
  the setup and walk 0.2, the pixel loop 20.9 (33 cycles a pixel, 54 with a shade that varies), and
  10.5 for the spans' own setup, ~260 cycles for each of 4,000 spans, a third of the surface. It is
  not the 64-bit conversions: taking them to 32 bits (`6eb5175`) saved 2–4%. The guess was the
  chain of dependent x87 operations the loop cannot start before, the divide, three multiplies, the
  magic-number rounding stored and read back, which Quake hid by starting the next divide before the
  current pixels. The emulated Pentium prices a dependent `fdiv` at ~4 cycles instead of 39, so this
  was measured on the 486DX2-66, where the divide is honest: a 32-pixel span's setup is ~860 cycles
  there, 26 of the quad's 89 ms, and the divide is 73 of them. Hiding it would save 2–3% at most,
  which is not worth assembler. Taking the setup apart there, the quad's 26 ms: the three float adds
  that move a span's 1/z, u/z and v/z along ~280 cycles, `kek_3d_perspective` ~200, the two
  conversions ~185, the two steps ~30, the sum ~700 of 860: it is float work end to end, and the
  parts overlap rather than add. The pixel loop is 61 cycles a pixel there, ~35 instructions a
  pixel with about ten of them stack spills, since x86-32 has six registers for the loop's dozen
  values. Unrolling it by four, the shading row for each pixel picked once, took 3.5% off a long
  span and put 3.4% on the cat's short ones: not taken. Compiler flags, on the 486: `-fomit-frame-pointer`
  2%, `-funroll-loops` 5% on the quad and 3% on the room, `-O3` nothing on those and 9% on the cat.
  On that machine the 300-triangle room is 151 ms (6.6 fps), 55 of them span setups and 46 the pixel
  loop. What does work is fewer spans: `KEK_3D_SPAN`, the pixels between divides, 32 by
  default (16 was Quake's), which makes the quad 15% faster than 16, the 300-triangle room 9%; 64
  makes them 22% and 13%. The pictures differ at 32 by 4,600 pixels of the quad and 700 of the
  room, at 64 by 13,800 and 2,800, and at 64 the lines of a near wall wobble. The cat, whose
  triangles are small, does not change.

  Other builds and machines, the bench at the defaults. `-O3`, which is what the CMake Release
  build is, against the `-O2` the DOS scripts use, on the Pentium 100: the cat 7% and the 2,700
  room 6% faster, the long-span quad 3% slower. On a Pentium II 300 (86Box, Deschutes on a
  P2B-LS; `-march=pentium2` against `-march=pentium` is a few per cent): the cat 4.8 ms, the
  300-triangle room 11.1, the 2,700 room 19.3, the textured quad 7.6; about 3.5 times the Pentium
  100, for three times the clock. The Pentium II budget is not the hard one.

Geometry is small beside that, and float suits it. A second standalone loop — rotate, translate and
project a vertex; set up a triangle's area and three attribute gradients — in cycles:

| | Pentium 100 | 486DX2-66 | 486SX-33, FPU emulated |
| --- | --- | --- | --- |
| Vertex, float, `(int)` casts | 185 | 535 | 199,577 |
| Vertex, float, `lrintf` | 157 | 539 | 200,131 |
| Vertex, 16.16 | 243 | 543 | 568 |
| Triangle, float | 375 (~515 with a true `fdiv`) | 1,351 | 526,084 |
| Triangle, 16.16 | 633 | 1,079 | 1,102 |

For 500 vertices and 300 triangles that is ~2.3 ms in float against ~3.1 in 16.16 on the Pentium, ~10
against ~9 on the 486DX: a millisecond either way, in frames of 40 and 130. The Pentium's pipelined FPU
beats its 10-cycle unpipelined `imul`, which is why Quake kept geometry in float. Only the 486SX
separates them, at ~8 seconds a frame in float, and it is not a target (see Settled decisions).

- **What is left between the textured span and the table.** ~49 cycles a pixel on the Pentium against
  38 for the standalone loop with the same dither. `f50ba29` took the span ends' conversions off x87's
  `fldcw` (a double's mantissa rounds them instead), the variable shift out of the texel index and the
  reloads out of the loop: from ~57. Measured on the Pentium only; timings there are `uclock()` now
  (`50fc42e`). What might take the rest:
  - *The span loop itself* is still ~35 instructions a pixel with most of its variables on the stack:
    x86-32 has seven registers, DJGPP's GCC keeps one for the frame pointer, and the loop wants more
    than a dozen. The standalone loop has its texture's size as a constant; the engine's is a mask in
    memory. Quake's was assembly. `-fomit-frame-pointer` changed nothing visible in the loop's code.
    u and v packed into one register, as some engines did for a fixed texture size, would take one
    back.
  - *The FPU in single precision* while rasterising, as Quake did: the Pentium's `fdiv` from 39 cycles
    to 19, once a span. C99 cannot say it, so it is a few lines of platform code, and optional.
  - *`lrintf` rather than the casts left*, per row and per vertex: measured, about 1% on the quads and
    3–4% on the cubes, not taken. It needs `-fno-math-errno` in every build of the library, or
    `lrintf` is a libm call slower than the cast; and it rounds vertices to the nearest pixel, which
    moves edges. The double-mantissa rounding the span ends use would do the same without a flag.
- **Order of work.** Done: the arena for Tier 1, the per-pixel divides (`0945961`), 16-bit depth
  (`6da6808`), the scanline rasteriser (`035fd37`), the models' storage (`fb92a44`) and the per-face
  and per-triangle waste (`5269d9e`) and the affine path for small triangles (`62981cb`). Next the
  vertices and faces, which are now the biggest part of the demo's frame. What is left of the span matters for large surfaces: a full-screen textured wall
  is 33 ms on the Pentium, where the table's loop would be ~26 ms. Fixed point across the engine is no
  longer on the list (see Settled decisions).

## Tier 3 — Engine: prerequisites for levels

- **Render to texture.** The arena it waited for is in. A `KEK_texture` is palette indices, a byte per pixel, the
  same as the frame, so binding a texture's pixels as the frame for a while and drawing into it with
  the ordinary 2D and 3D calls gives security-camera monitors (the Build engine's `setviewtotile`) and
  model thumbnails in the editor. The projection already takes its aspect from `e->w`/`e->h`. Two
  things to get right: 3D needs a depth buffer the size of the texture, which should be a temporary
  from the top of the arena (`kek_arena_temp`, internal today) rather than the frame's own, since a scene half way through its main view has that
  one half full; and textures are drawn before the view that shows them, or they show the last frame.
  Nothing about it needs a `KEK_frame` type up front: `fb`, `db`, `w` and `h` are already the bound
  frame, and a bind/restore pair of functions over them is the whole API.

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
- **The camera is set up again for every model.** `kek_3d_draw_model` builds the camera's matrix
  (6 `sinf`/`cosf`) and its focal length (`tanf`) on every call, though they change once a frame. With
  DJGPP's libm, which is fdlibm in software, that is ~1,100 cycles a model on a Pentium 100 and ~3,500
  on a 486DX2-66, measured on 86Box. A view set once — `kek_3d_begin_view(e, camera)` or the camera
  cached in the engine — takes it off the per-model path. It changes the same signature as the
  transform item above, so the two go together. The view is also where the depth buffer's scale
  belongs: `KEK_3D_DEPTH_SCALE` is a constant that puts 65535 at the default camera's near plane, 0.1,
  so a camera with its near plane at 1 uses a tenth of the range. Taken from the view's near plane
  instead, every camera would get all 16 bits.
- **Own transcendentals.** libm calls across four files: `sinf`/`cosf` (18), `roundf` (4), `tanf` (2),
  `sqrtf` (2), `floorf`, `fabsf` (2). Table-driven replacements drop the libm dependency and, more
  importantly, make rendering bit-reproducible across toolchains — libm accuracy is not specified, unlike
  `+ - * /` and `sqrt`. What they cost with DJGPP's libm, which is fdlibm and never uses the x87's own
  instructions, in cycles per call on 86Box:

  | | Pentium 100 | 486DX2-66 |
  | --- | --- | --- |
  | `sinf`, `cosf` | 145 | 450–465 |
  | `tanf` | 232 | 767 |
  | `sqrtf` | 282 | 547 |
  | `floorf` | 26 | 32 |
  | `roundf` | 34 | 88 |
  | `lrintf` | 65 | 147 |
  | `(int)` cast | 26 | 58 |

  Per frame that is small next to the pixels: trigonometry is per model (and mostly the camera's,
  above), `sqrtf` per face in the light, ~1 ms and ~2.5 ms for 300 faces. `floorf` was twice per
  texel under `REPEAT`, ~33 ms of a full-screen wall on the Pentium, until the scanline rasteriser
  wrapped with a mask; it is left only where a span end lands past ±32,768 texels, and in the
  per-pixel sampler that textures whose sides are not powers of two still go through. `sqrtf` has a
  cheap fix: `sqrtf` stays a library call even with
  `-fno-math-errno`, but `(float)__builtin_sqrtl(x)` under it is one `fsqrt`, ~70 and ~85 cycles, and
  rounds to the same float (a 64-bit intermediate is more than the 2 × 24 + 2 bits double rounding of
  a square root needs to be harmless). The builtin is GCC and Clang only, so it wants a small helper
  with `sqrtf` as the fallback. `-fno-math-errno` for the library is safe regardless — nothing reads
  `errno` — and the span loop wants it for `lrintf`, which it makes a single `fistp`.
  `-funsafe-math-optimizations` would also give `fsqrt`, but it licenses reassociation, which golden
  frames cannot have. Together with `-ffp-contract=off` and no fast-math this puts golden-frame tests back on the
  table — per instruction set, not across them: with geometry staying float (Settled decisions), x87
  and SSE builds differ in intermediate precision, and the benchmark's checksums already do. The x87
  frames matched bit for bit between a desktop `-m32 -mfpmath=387` build and DJGPP on 86Box, so one
  set of golden frames for x87 and one for SSE would cover every target and the CI.

## Tier 4 — Levels (`.klf`)

There is no world as a concept. Scenes are compiled-in C structs, `DEMO_init_scene()`
([demo_scene_loader.c](demo/demo_scene_loader.c)) is a switch with one case, `DEMO_SCENETAG_MENU` and
`DEMO_SCENETAG_SETTINGS` are declared in [demo_tag.h](demo/include/demo_tag.h) and never implemented, and
rendering is a single hardcoded `kek_3d_draw_model` call.

- An entity/instance concept in the engine: a pooled array of `{model handle, texture handle, transform,
  flags}` and a draw pass over it, following the existing `KEK_ModelPool` pattern.
- The `.klf` format, magic `KLVL`: header, model table (paths), instance table (transform plus model
  index), camera spawn, and the light: per-vertex shade for level geometry, a light per zone, fog. Same
  discipline as KMF — fixed-size records, bounds-checked, no allocation. Level geometry wants more
  than a model's byte an axis (`fb92a44`): across a room 64 units wide a step is a quarter of a unit,
  and seams between pieces would open. Float, or 16 bits over the level's box.
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
- Split the camera/player controller out of the scene; `DEMO_EntryScene_update` currently does both.

## Tier 5 — Editor

- **`SceneEditor` and `ModelEditor` are stubs** — labels only, see
  [scene_editor.h](tools/kek_editor/tools/scene_editor.h) and
  [model_editor.h](tools/kek_editor/tools/model_editor.h). The shell, the tool-plugin architecture and
  `DemoTool` with its palette editor all work.
- **The keystone is missing: nothing renders the engine framebuffer into ImGui.** Both tools need it and
  it is written once, as `components/engine_viewport.h`. `app_build_palette()` at
  [main_sdl.c#L38](demo/platform/main_sdl.c#L38) is the same palette expansion. It needs no OS, so it
  can move into the library as `kek_palette_to_rgbx8888` and be shared.
- **No file open/save** — no KMF/KIF import, no `.klf` output. SDL3 has `SDL_ShowOpenFileDialog`.
- **Releases.** Editor binaries through GitHub Releases, built against the installed package the way
  CI builds the demo. Nothing publishes them yet.

## Tier 6 — Demo

The game's own backlog lives with the game. The demo is one scene: a rotating cat, debug palette bars
and camera coordinates ([demo_scene_entry.c](demo/demo_scene_entry.c)).

- **Cleanup once entities exist**: the dead `if (!e->assets)` at
  [demo_scene_entry.c#L69](demo/demo_scene_entry.c#L69) and the commented-out multi-model draw.

## Tier 7 — Later

- **No audio at all**; `SDL_AUDIO` is explicitly disabled in the web build
  ([CMakeLists.txt](CMakeLists.txt)). Own format by analogy with KMF/KIF, most likely sound banks — a set
  of short samples in one file behind a shared table, to save space.
- **Scripts**: no `requirements.txt` (Pillow is needed), no round-trip tests for `obj_to_kmf` or
  `bmp_to_kif`. The default palette has three copies: the engine's table in
  [kek_palette.c](kek/kek_palette.c), a string in [palette_to_bmp.py](scripts/palette_to_bmp.py) parsed
  with a regex, and `ENGINE_PALETTE_888` in [bmp_to_kif.py](scripts/bmp_to_kif.py). All three agree
  today — the first two were checked byte for byte when the item shrank to three channels — but nothing
  keeps them agreeing. Generate the Python copies from the C table, or the C table from a data file.
- **Gouraud as a model option.** `424f69e` shades flat, one shade per face. That suits the look, costs one table
  lookup per pixel and let the normals go (`01673ea`). Smooth models would want a shade per corner, and the
  interpolation is already there for fog. The normals can come back small: Quake stored a vertex normal
  as a byte indexing a table of 162 directions. KMF still carries them, so the loader has them to read.
- **How much of libc the library needs** — `needs a decision`. Today: the freestanding headers
  (`stdint.h`, `stddef.h`, `limits.h`), `memcpy`/`memset` and one `strcmp` in the in-memory asset
  provider, plus libm, which Tier 3's transcendentals item removes. GCC and Clang emit calls to
  `memcpy`/`memset`/`memmove`/`memcmp` even under `-ffreestanding`, so "no libc" means those four come
  from the platform or from kek. A CI build of the library with `-ffreestanding -nostdlib` would keep it
  honest either way.
- **The DJGPP toolchain file needs the compiler's directory on `PATH`.** `cmake/toolchain-djgpp.cmake`
  names the compiler by its full path, but the gcc driver runs `stubify` to finish a DOS executable
  and looks for it on `PATH`; without `KEK_DJGPP_ROOT/bin` there, configuring fails at CMake's compiler
  check. The toolchain file could prepend the directory to `PATH` for the build.
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

### Tier 1

| | Was | Commit |
| --- | --- | --- |
| Game-named project | `GAME_NAME` was the CMake project name and the prefix of every target. The project is `kek`; the scene became the demo, with targets `kek_demo` and `kek_demo_sdl` | `c4bf800` |
| A sample in the public repository | The only consumer of the engine was the game, which was to leave. It stays as the demo, and the web demo builds it | `c4bf800` |
| Mixed assets | `assets/` was to be split into engine samples and game content. Everything in it is the demo's; it is `demo/assets` now | `c4bf800` |
| Platform layer wired to the game | `main_sdl.c` called the game's init from `<game.h>`, and the plan was a callback so a game could reuse it. The platform layer was never library code: it is the demo's, in `demo/platform`, and a game copies it. The stdio asset provider went with it rather than into `kek/io` | `8c104d4` |
| Everything in one CMake project | The top-level CMakeLists built engine, game, platform layer and editor as one. It builds the library and its tests; the demo and the editor are projects of their own over `kek::kek`, and as someone's subdirectory kek builds the library alone | `8c104d4` |
| Not installable | No install rules, no package. `cmake --install` and `find_package(kek)`; Native CI builds the demo against the install | `3b65957` |
| Frame, depth and palettes in library statics | Sized by `KEK_BUFFER_WIDTH`/`HEIGHT`, which were baked into the package. `kek_init` takes a `KEK_desc` and one block from the application and lays all four out in it; `KEK_MEMORY_SIZE` sizes a static array for it | `747952e` |
| Pools and scratch in library statics | Both pools, the KMF loader's staging and `kek_3d_draw_model`'s view-space vertices, sized by `KEK_POOL_*` limits baked into the package and shared by every engine in the process: a second `kek_init` reset the first one's pools. All of it is in the application's block now, as the arena and the handle tables; no `KEK_POOL_*` limit is left | `16ffe9d` |

### Tier 2

| | Was | Commit |
| --- | --- | --- |
| Worst-case slots | A model slot was 87,136 bytes whatever it held and a texture slot 65,560, ×90 and ×256 the defaults. A two-ended arena in the rest of the block: assets sized exactly from the low end, temporaries from the high end, a footer per block so that a destroy in any order gives the memory back once what is above it has gone, and `kek_arena_mark`/`kek_arena_release` for a level's lifetime. `kek_model_create` and `kek_texture_create` take sizes. Generations are unchanged | `16ffe9d` |
| Normals nobody read | `KEK_model_face_normal` was 36 bytes per face, 432 of the default cube's 1,264, and neither drawing nor lighting read it. Gone from the model; the KMF loader checks normal indices and reads past the normals without staging them. The builtin reserve went from 2,048 to 1,024 with it | `01673ea` |
| Bounding-box rasterisers | Both walked every pixel of a triangle's box with float edge functions, stepped float attributes through memory on x87 and sampled through a call per pixel: ~15× the standalone span loop. Edges walked row by row in exact integers, a span loop in 16.15 depth and 16.16 texels with masked wrap, perspective every 16 pixels. The benchmark's textured wall from ~1,290 cycles a pixel to ~130 on the 486, ~590 to ~57 on the Pentium; coverage the same, pixel for pixel | `035fd37` |
| UVs the largest per-face array | UVs expanded to a float pair per corner of every face, 24 bytes a face, vertices a float triple, faces three `uint32_t`. UVs indexed as the file has them, vertices a byte an axis with a per-model scale and offset as in Quake's MDL, 16-bit indices: the demo's cat from 26,304 bytes to 11,776. Vertices move by up to half a step as a model turns, which is accepted | `fb92a44` |
| Per-face and per-triangle waste | Back faces were lit, square root and all, clipped and projected before a screen-space test dropped them; every face projected its three corners though a vertex is shared by about six; an edge's setup was 64-bit divides, eight calls into libgcc a triangle; a row took two evaluations of each plane and a divide for its depth. Back faces dropped in view space first, each vertex projected once, 32-bit edge steps, a row's depth and shade stepped by the triangle's gradient. The demo's cat from 32.5 ms to ~22 on the Pentium | `5269d9e` |

### Tier 3

| | Was | Commit |
| --- | --- | --- |
| Resolution hardcoded | `KEK_BUFFER_WIDTH`/`KEK_BUFFER_HEIGHT`/`KEK_TARGET_FPS` were `#define`s at the top of `kek.c`. In `kek_config.h` with the other knobs now, with CMake cache entries | `f99074d` |
| No lighting | The shading palette was computed at init and `kek_3d_draw_model` never used it. Flat shading from a world-fixed directional light plus ambient, with the normal derived from the face; fog with view depth; Bayer 4×4 dither between rows | `424f69e` |
| Divides in the per-pixel loop | Three per pixel in `kek_3d_triangle` (`w0 / area`), five in `kek_3d_triangle_textured` (and `u_over_z / inv_z`), and no way to measure them. A benchmark in `bench/`; one divide per triangle, attributes as planes stepped by adds, and perspective divided out every 16 pixels with affine spans between, as in Quake. On an emulated 486DX2-66 (86Box, DJGPP): flat quad 791 → 445 ms, textured quad 1,681 → 1,242 ms, textured cube 478 → 346 ms | `954cb4c`, `0945961` |
| Depth buffer the largest allocation | A `float` per pixel, 256,000 of a 320×200 block's 329,487 bytes. 1/z in a `uint16_t`, scaled so 65535 is the default near plane, saturating at both ends; the block is 201,487 | `6da6808` |
| The sampler per textured pixel | `kek_texture_sample` was a call per pixel with a branch on the warp mode, `floorf` twice under `REPEAT` and two conversions. The span loop steps u and v in 16.16 and masks for wrap; only textures whose sides are not powers of two still go through it | `035fd37` |
| Stepped floats through memory on x87 | Strict C99 stored and reloaded every `float` the pixel loops stepped. Nothing in the span loops is float now | `035fd37` |

### Tier 7

| | Was | Commit |
| --- | --- | --- |
| No DOS platform layer | Only SDL3 (`demo/platform/main_sdl.c`); the frame was mode-13h-shaped from the start but nothing ran it there. `demo/platform/dos/`: mode 13h via a real-mode `int 0x10` and DJGPP's near-pointer window, the palette straight to ports `0x3C8`/`0x3C9` with no widening, an IRQ9 handler for real held/pressed/released keys, `uclock()` for frame pacing. `cmake/toolchain-djgpp.cmake` reaches the existing CMake tree with the installed cross compiler rather than a build file of its own — `kek`, `tests/` and `bench/` had no OS dependency and cross-build unchanged. Verified in 86Box's 486DX2-66 VM from a FreeDOS floppy: the cat renders, rotates and moves with the keyboard | `cba7a07` |

### Found on the way, not from a backlog item

`88f1b93`: camera orientation now uses the transpose of the world rotation,
shared by point projection and model drawing. Projection checks its float coordinates before
integer conversion; triangles beyond the ±2^20-pixel guard band take a bounded near/side
clipping path with double intersections and interpolated UVs. Direct rasterizer calls reject
coordinates outside that band. Public signatures and resource formats are unchanged.

Verified locally: 218 tests in 14 suites with GCC and Clang under ASan/UBSan (including
float-cast-overflow), clang-tidy, and DJGPP cross-builds of the library, tests, benchmark and
DOS demo. A separate Linux sanitizer job is now in native CI. The external DOS measurement
setup stays outside this repository.

Native performance against `a62c07a`: GCC 16.2.1, Release (`-O3`), x86-64, CPU 2;
three alternating before/after runs, 3,000 frames per scene, best of five rounds,
with `--assets demo/assets`. The benchmark's temporary output copy printed six decimal
places instead of three; its workloads were unchanged. All 11 frame checksums matched
in every run. The largest median slowdown was 1.9%; the cat behind the camera improved
by 31.9% and the distant cat by 4.6%. Pixel loops are unchanged: public boundary checks
wrap the internal rasterizers, native GCC/Clang align the hot functions to 64 bytes
(DJGPP retains its default alignment), and vertex transforms stay inside their loop.

Emulated, against `a62c07a` (86Box, DJGPP `-O2`, bench with `--assets`, best of five rounds,
frame checksums identical). Pentium 100, 100 frames: the cat as in the demo 24.68 → 25.74 ms
(+4.3%, the same at 10 frames), behind the camera 5.48 → 3.71 (−32.3%), far off 8.41 → 7.79
(−7.4%), the cubes +0.2% to +0.5%, quads and the 2D overlay within 0.03%. So the 1.9% above
is native only: on DOS the demo's cat is 4.3% slower. Two of the added checks were taken out
one at a time (per-vertex finite test, the normal's overflow guard) and explain about 0.15 ms of
the 1.09 ms; taking out the screen-bound checks crashes the rasteriser, so those were not
measured. 486DX2-66, 10 frames, without the cat: everything within 0.25%. The affine path
for small triangles, below, is worth far more than this.

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
