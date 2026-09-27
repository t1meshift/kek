# The demo: one scene, the SDL3 platform layer that runs it on the desktop and
# in the browser, and the assets it loads. It consumes kek the way a game would,
# and nothing here is part of the library — platform/ is meant to be copied into
# a game and changed there, not linked against.
#
# Built from the top-level CMakeLists.txt it gets this tree's kek; on its own it
# finds an installed one:
#   cmake -S demo -B build-demo -DCMAKE_PREFIX_PATH=<where kek is installed>
cmake_minimum_required(VERSION 3.20)

project(kek_demo VERSION 0.1.0 LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT TARGET kek::kek)
  find_package(kek REQUIRED)
endif()

# DJGPP has no use for SDL3 — the DOS platform layer talks to VGA and IRQ9
# directly (platform/dos/) — and no cross-built SDL3 exists to find or fetch.
if(NOT DJGPP)
option(KEK_FETCH_DEPS "Resolve dependencies automatically (system package or FetchContent)" ON)
set(KEK_SDL3_LOCAL_DIR "${CMAKE_SOURCE_DIR}/ext/SDL3-3.4.2"
    CACHE PATH "Prebuilt SDL3 used when KEK_FETCH_DEPS=OFF")
set(KEK_SDL3_TAG "release-3.4.2" CACHE STRING "SDL3 git tag to fetch")

# The editor resolves SDL3 the same way; whichever of the two comes first in a
# build of the whole tree does it for both.
if(NOT TARGET SDL3::SDL3)
  if(KEK_FETCH_DEPS)
    # An SDL3 installed system-wide is just as good and saves building it from
    # source; fall back to fetching only when there is none. A host SDL3 is no
    # use to Emscripten, so that build always goes through the source fetch.
    if(NOT EMSCRIPTEN)
      find_package(SDL3 QUIET)
    endif()
    if(NOT SDL3_FOUND)
      if(EMSCRIPTEN)
        # The page needs video and keyboard and nothing else; every other
        # subsystem is dead weight in the wasm.
        set(SDL_AUDIO OFF CACHE BOOL "" FORCE)
        set(SDL_CAMERA OFF CACHE BOOL "" FORCE)
        set(SDL_DIALOG OFF CACHE BOOL "" FORCE)
        set(SDL_HAPTIC OFF CACHE BOOL "" FORCE)
        set(SDL_JOYSTICK OFF CACHE BOOL "" FORCE)
        set(SDL_POWER OFF CACHE BOOL "" FORCE)
        set(SDL_SENSOR OFF CACHE BOOL "" FORCE)
      endif()

      include(FetchContent)
      FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        ${KEK_SDL3_TAG}
        GIT_SHALLOW    TRUE
      )
      FetchContent_MakeAvailable(SDL3)
    endif()
  else()
    list(APPEND CMAKE_PREFIX_PATH "${KEK_SDL3_LOCAL_DIR}")
    find_package(SDL3 REQUIRED)
  endif()
endif()
endif()

# The same set kek builds with, for the same reasons; see the top-level
# CMakeLists.txt. A copy rather than a shared file, so this directory stands
# on its own.
option(KEK_WERROR "Treat compiler warnings as errors" OFF)

function(kek_demo_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(KEK_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

######## The scene ########
add_library(kek_demo
  include/demo_scenes/demo_scene_entry.h
  include/demo_scene_loader.h
  include/demo_tag.h
  include/demo.h
  demo_scene_entry.c
  demo_scene_loader.c
  demo.c
)
set_target_properties(kek_demo PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)
kek_demo_warnings(kek_demo)
target_include_directories(kek_demo PUBLIC
  include/
)
target_link_libraries(kek_demo PRIVATE kek::kek)

if(NOT DJGPP)
######## SDL3 platform layer ########
# Platform-agnostic as far as SDL3 goes: Windows, Linux and, through
# Emscripten, the browser.
add_executable(kek_demo_sdl
  platform/main_sdl.c
  platform/platform_assets_fs.h
  platform/platform_assets_fs.c
)
set_target_properties(kek_demo_sdl PROPERTIES
  C_STANDARD 11 # Required by SDL3
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)
kek_demo_warnings(kek_demo_sdl)
target_link_libraries(kek_demo_sdl PRIVATE kek::kek kek_demo SDL3::SDL3)

if(WIN32)
  add_custom_command(
    TARGET kek_demo_sdl POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:kek_demo_sdl>"
    VERBATIM
  )
endif()

if(EMSCRIPTEN)
  # The browser build is one self-contained page: index.html + .js + .wasm, with
  # the assets baked into the module. Nothing is fetched at runtime and
  # platform_assets_fs.c keeps working unchanged — MEMFS mounts the embedded
  # files at /assets and the working directory is /, so "./assets/" resolves.
  set(KEK_WEB_SHELL "${CMAKE_CURRENT_SOURCE_DIR}/platform/web/shell.html")
  file(GLOB_RECURSE KEK_WEB_ASSET_FILES "${CMAKE_CURRENT_SOURCE_DIR}/assets/*")

  set_target_properties(kek_demo_sdl PROPERTIES
    OUTPUT_NAME "index"
    SUFFIX ".html"
    LINK_DEPENDS "${KEK_WEB_SHELL};${KEK_WEB_ASSET_FILES}"
  )
  target_link_options(kek_demo_sdl PRIVATE
    "--embed-file" "${CMAKE_CURRENT_SOURCE_DIR}/assets@/assets"
    "--shell-file" "${KEK_WEB_SHELL}"
    # The 1 MB -sSTACK_SIZE that used to be here was for kek_file_model_load(),
    # which staged a whole KMF in ~51 KB of locals. That is a static scratch
    # buffer now and the engine's largest frame is 672 bytes, so Emscripten's
    # 64 KB default is plenty.
    # No allocation past startup, so the heap can be fixed instead of growable.
    "-sINITIAL_MEMORY=33554432"
    "-sALLOW_MEMORY_GROWTH=0"
    # SDL_AppIterate returns every frame, so the runtime has to stay alive.
    "-sEXIT_RUNTIME=0"
  )
else()
  # The demo resolves assets against the working directory (see "./assets/" in
  # platform/main_sdl.c), so they have to sit next to the executable.
  add_custom_command(
    TARGET kek_demo_sdl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:kek_demo_sdl>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_CURRENT_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:kek_demo_sdl>/assets"
    COMMENT "Replacing assets directory next to kek_demo_sdl"
  )
endif()

else() # DJGPP
######## DOS platform layer ########
# VGA mode 13h and an IRQ9 keyboard handler, straight to the hardware — see
# platform/dos/main_dos.c. No SDL3, no dynamic loading of anything.
add_executable(kek_demo_dos
  platform/platform_assets_fs.h
  platform/platform_assets_fs.c
  platform/dos/platform_dos_keyboard.h
  platform/dos/platform_dos_keyboard.c
  platform/dos/main_dos.c
)
set_target_properties(kek_demo_dos PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  # DJGPP's <dpmi.h>/<go32.h> hide the _go32_dpmi_* declarations this file
  # needs behind "#ifndef __STRICT_ANSI__", which plain -std=c99 sets; only
  # the GNU dialect (-std=gnu99) leaves it unset. kek itself needs no such
  # thing and stays strict.
  C_EXTENSIONS ON
)
kek_demo_warnings(kek_demo_dos)
target_link_libraries(kek_demo_dos PRIVATE kek::kek kek_demo)

# The demo resolves assets against the working directory (see "./assets/" in
# main_dos.c), so they have to sit next to the executable — the same rule as
# kek_demo_sdl, just without Emscripten's embedded-file alternative.
add_custom_command(
  TARGET kek_demo_dos POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E remove_directory
          "$<TARGET_FILE_DIR:kek_demo_dos>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_CURRENT_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:kek_demo_dos>/assets"
  COMMENT "Replacing assets directory next to kek_demo_dos"
)
endif()
