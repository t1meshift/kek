# KEK engine

## [Live Demo](https://t1meshift.github.io/kek/)

## Overview
KEK is a 2D/3D software renderer/engine written in pure C99 with no dynamic allocations. It draws into a 320×200 framebuffer with a 256-color palette and targets 30 FPS. The goal is to run on as many platforms as possible with almost non-existent CPU and memory requirements.

## Structure
The library is the product; everything else consumes it.
- `kek/` — the library: rasterization (2D and 3D), math, an arena for assets, model/image/palette/font handling. No platform code and no dependencies
- `demo/` — a demo scene, its assets and the SDL3 platform layer that runs it on Windows, Linux and, through Emscripten, the browser. None of it is part of the library: a game starts from a copy of `demo/platform/`. The game itself lives in a separate repository
- `tools/kek_editor/` — an editor built on Dear ImGui (C++20) that links against the library. The shell and the tool-plugin architecture work; the scene and model tools are still stubs.

`scripts/` holds the asset converters: `obj_to_kmf.py` (models), `bmp_to_kif.py` (images), `bdf_to_c.py` (fonts), `palette_to_bmp.py`.

## Roadmap

The engine targets a Pentium and should still run on a 486, which settles a few things — fixed-point arithmetic among them. The current focus is the foundation rather than features. Warnings, clang-tidy, a native CI build and a unit-test suite are in place, and the defects they turned up are fixed. Models are lit: flat shading through the palette, with fog and a dither. Levels and a working editor come after the rest of the foundation.

[BACKLOG.md](BACKLOG.md) has the whole list, ordered by what unblocks what.

## Building

Requires CMake 3.20+ and a C99 / C++20 toolchain. The top-level `CMakeLists.txt` builds the library and its tests, and adds the demo and the editor, which are projects of their own (`demo/CMakeLists.txt`, `tools/kek_editor/CMakeLists.txt`); `-DKEK_BUILD_DEMO=OFF`, `-DKEK_BUILD_EDITOR=OFF` and `-DKEK_BUILD_TESTS=OFF` leave any of them out. Added to another project with `add_subdirectory`, it builds the library alone, as `kek::kek`. The demo and the editor use a system-wide SDL3 if you have one and build it from source otherwise; the editor always fetches Dear ImGui. A fresh clone needs no setup:

```sh
cmake -B build
cmake --build build
```

Targets: `kek` (engine), `kek_demo` (the demo scene), `kek_editor` (editor), and `kek_demo_sdl` (the runnable demo).

`-Wall -Wextra -Wpedantic` (`/W4` on MSVC) are on for everything but the vendored Dear ImGui. `-DKEK_WERROR=ON` turns them into errors; `.github/workflows/native.yml` builds that way on Linux GCC, Linux Clang and Windows MSVC, and runs `clang-tidy` as a separate job, pinned to one LLVM release, where any finding fails the build.

The engine's unit tests live in `tests/`, on [Unity](https://github.com/ThrowTheSwitch/Unity) (fetched like the other dependencies) and CTest. They build by default everywhere but the browser; `-DKEK_BUILD_TESTS=OFF` skips them.

```sh
ctest --test-dir build --output-on-failure
```

`bench/` times the rasterisers scene by scene and prints a checksum of each scene's last frame; it builds wherever the tests do, `-DKEK_BUILD_BENCH=OFF` skips it. Build it in Release for numbers that mean anything, and pin it to one core:

```sh
taskset -c 2 ./build/Release/kek_bench_raster 300
```

The demo resolves assets against the working directory, so run it from its output directory:

```sh
cd build/Debug && ./kek_demo_sdl
```

### Using kek from another project

Install it and find it as a package:

```sh
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
cmake --install build --prefix <prefix>
```

```cmake
find_package(kek REQUIRED)
target_link_libraries(my_game PRIVATE kek::kek)
```

with `-DCMAKE_PREFIX_PATH=<prefix>` when configuring the consumer. `add_subdirectory` gives the same `kek::kek`.

The application owns all of the engine's memory; the library keeps none of its own. It hands `kek_init` one block. The engine lays out its frame, depth buffer, palettes and handle tables inside it, and whatever the application adds on top of `KEK_MEMORY_SIZE` is the arena that models and textures come from:

```c
static unsigned char memory[KEK_MEMORY_SIZE(320, 200) + 256 * 1024];
KEK_engine engine;

kek_init(&engine, &(KEK_desc){ .width = 320, .height = 200 }, memory, sizeof memory);
```

There are no per-model or per-texture limits: a model or a texture is as big as the arena has room for. `kek_arena_mark` and `kek_arena_release` free everything loaded since a mark at once, which is how a level's assets go away. `demo/` is a complete consumer and builds that way on its own:

```sh
cmake -S demo -B build-demo -DCMAKE_PREFIX_PATH=<prefix>
```

### Browser build

The SDL3 platform layer also builds for the web through Emscripten, so the same
`demo/platform/main_sdl.c` produces the desktop executable and the demo page. With
`emcc` on PATH:

```sh
emcmake cmake -B build-web -DCMAKE_BUILD_TYPE=Release
cmake --build build-web
```

That writes `build-web/Release/index.{html,js,wasm}` — the assets are embedded in
the module, so the three files are the whole page. Serve them over HTTP (`file://`
will not load the wasm):

```sh
python3 -m http.server -d build-web/Release
```

The page is built and published to GitHub Pages by `.github/workflows/web-demo.yml`
on every push to `master`; the repository's Pages source has to be set to
"GitHub Actions" once for the deploy step to work. The editor is not part of the
browser build.

To build against local copies of the dependencies instead of fetching them:

```sh
cmake -B build -DKEK_FETCH_DEPS=OFF \
  -DKEK_SDL3_LOCAL_DIR=ext/SDL3-3.4.2 \
  -DKEK_IMGUI_LOCAL_DIR=ext/imgui-1.92.6-docking \
  -DKEK_UNITY_LOCAL_DIR=ext/Unity-2.6.1
```

## License

All rights reserved — see [LICENSE](LICENSE). The source is public to read and
build locally; it is not open source.
