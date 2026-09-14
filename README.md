# KEK engine

## Overview
KEK is a 2D/3D software renderer/engine written in pure C99 with no dynamic allocations. It draws into a 320×200 framebuffer with a 256-color palette and targets 30 FPS. The goal is to run on as many platforms as possible with almost non-existent CPU and memory requirements.

## Structure
Right now, it has 4 components:
- `kek/` — the engine itself: rasterization (2D and 3D), math, pool allocator, model/image/palette/font handling
- `game/` — game/business logic
- `platform/` — platform implementation files (SDL3 for now; builds on Windows and Linux)
- `tools/kek_editor/` — an editor built on Dear ImGui (C++20) that links against the engine. The shell and the tool-plugin architecture work; the scene and model tools are still stubs.

`scripts/` holds the asset converters: `obj_to_kmf.py` (models), `bmp_to_kif.py` (images), `bdf_to_c.py` (fonts), `palette_to_bmp.py`.

## Building

Requires CMake 3.20+ and a C99 / C++20 toolchain. CMake uses a system-wide SDL3 if you have one and builds it from source otherwise; it always fetches Dear ImGui. A fresh clone needs no setup:

```sh
cmake -B build
cmake --build build
```

Targets: `kek` (engine), `SnusShooter` (game logic), `kek_editor` (editor), and `SnusShooter_sdl` (the playable executable).

The game resolves assets against the working directory, so run it from its output directory:

```sh
cd build/Debug && ./SnusShooter_sdl
```

To build against local copies of the dependencies instead of fetching them:

```sh
cmake -B build -DKEK_FETCH_DEPS=OFF \
  -DKEK_SDL3_LOCAL_DIR=ext/SDL3-3.4.2 \
  -DKEK_IMGUI_LOCAL_DIR=ext/imgui-1.92.6-docking
```
