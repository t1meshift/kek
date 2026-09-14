cmake_minimum_required(VERSION 3.20)

# Project settings
set(GAME_NAME "SnusShooter")

project(${GAME_NAME} VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Dependencies are resolved by CMake so a fresh clone builds without setup:
# a system-wide SDL3 is used when present, anything missing is fetched.
# Turn this OFF to use local copies under ext/ instead (see KEK_SDL3_LOCAL_DIR
# and KEK_IMGUI_LOCAL_DIR below).
option(KEK_FETCH_DEPS "Resolve SDL3 and Dear ImGui automatically (system package or FetchContent)" ON)
set(KEK_SDL3_LOCAL_DIR "${CMAKE_SOURCE_DIR}/ext/SDL3-3.4.2"
    CACHE PATH "Prebuilt SDL3 used when KEK_FETCH_DEPS=OFF")
set(KEK_IMGUI_LOCAL_DIR "${CMAKE_SOURCE_DIR}/ext/imgui-1.92.6-docking"
    CACHE PATH "Dear ImGui sources used when KEK_FETCH_DEPS=OFF")

set(KEK_SDL3_TAG "release-3.4.2" CACHE STRING "SDL3 git tag to fetch")
set(KEK_IMGUI_TAG "v1.92.6-docking" CACHE STRING "Dear ImGui git tag to fetch")

if(KEK_FETCH_DEPS)
  include(FetchContent)

  # An SDL3 installed system-wide is just as good and saves building it from
  # source; fall back to fetching only when there is none. A host SDL3 is no use
  # to Emscripten, so that build always goes through the source fetch.
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

    FetchContent_Declare(SDL3
      GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
      GIT_TAG        ${KEK_SDL3_TAG}
      GIT_SHALLOW    TRUE
    )
    FetchContent_MakeAvailable(SDL3)
  endif()

  # Only the editor needs Dear ImGui, and the editor is not part of a browser
  # build (see the kek_editor section).
  if(NOT EMSCRIPTEN)
    FetchContent_Declare(imgui
      GIT_REPOSITORY https://github.com/ocornut/imgui.git
      GIT_TAG        ${KEK_IMGUI_TAG}
      GIT_SHALLOW    TRUE
    )

    # Dear ImGui ships no CMakeLists; FetchContent_MakeAvailable only downloads it
    # and the imgui target below is built from ${imgui_SOURCE_DIR}.
    FetchContent_MakeAvailable(imgui)
    set(KEK_IMGUI_DIR "${imgui_SOURCE_DIR}")
  endif()
else()
  list(APPEND CMAKE_PREFIX_PATH "${KEK_SDL3_LOCAL_DIR}")
  find_package(SDL3 REQUIRED)
  set(KEK_IMGUI_DIR "${KEK_IMGUI_LOCAL_DIR}")
endif()

set(KEK_MODEL_POOL_CAPACITY 16 CACHE STRING "Max pooled models")
set(KEK_TEXTURE_POOL_CAPACITY 16 CACHE STRING "Max pooled textures")
set(KEK_POOL_MODEL_VERTS_MAX 1024 CACHE STRING "Max vertices per pooled model")
set(KEK_POOL_MODEL_FACES_MAX 1024 CACHE STRING "Max faces per pooled model")
set(KEK_POOL_MODEL_COLORS_MAX 1024 CACHE STRING "Max face colors per pooled model")
set(KEK_POOL_MODEL_UVS_MAX 1024 CACHE STRING "Max textured faces per pooled model")
set(KEK_POOL_TEXTURE_PIXELS_MAX 65536 CACHE STRING "Max pixels per pooled texture")
set(KEK_PALETTE_SHADING_LEVELS 4 CACHE STRING "Max shading levels")

# The output directories below are per-configuration, and with a single-config
# generator (Ninja, Make) $<CONFIG> is empty unless a build type is set — which
# would drop the binaries straight into ${CMAKE_BINARY_DIR}.
get_property(KEK_MULTI_CONFIG GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(NOT KEK_MULTI_CONFIG AND NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Debug" CACHE STRING "Build type" FORCE)
endif()

set(
  CMAKE_RUNTIME_OUTPUT_DIRECTORY
  "${CMAKE_BINARY_DIR}/$<CONFIGURATION>"
)

set(
  CMAKE_LIBRARY_OUTPUT_DIRECTORY
  "${CMAKE_BINARY_DIR}/$<CONFIGURATION>"
)

######## KEK engine ########
add_library(kek 
  kek/include/kek_asset.h
  kek/include/kek_keyboard.h
  kek/include/kek_macro.h
  kek/include/kek_math.h
  kek/include/kek_config.h
  kek/include/kek.h
  kek/include/kek_palette.h
  kek/include/kek_2d.h
  kek/include/kek_font.h
  kek/include/kek_model.h
  kek/include/kek_3d.h
  kek/include/kek_texture.h
  kek/include/kek_pool.h
  kek/include/kek_file_image.h
  kek/include/kek_file_model.h
  kek/kek_default_font.c
  kek/kek_model.c
  kek/kek_palette.c
  kek/kek_math.c
  kek/kek_font.c
  kek/kek_2d.c
  kek/kek_3d.c
  kek/kek_texture.c
  kek/kek_pool.c
  kek/kek_file_image.c
  kek/kek_file_model.c
  kek/kek.c
)
set_target_properties(kek PROPERTIES 
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)
target_include_directories(kek PUBLIC
  kek/include/
)
target_compile_definitions(kek PUBLIC
  KEK_MODEL_POOL_CAPACITY=${KEK_MODEL_POOL_CAPACITY}
  KEK_TEXTURE_POOL_CAPACITY=${KEK_TEXTURE_POOL_CAPACITY}
  KEK_POOL_MODEL_VERTS_MAX=${KEK_POOL_MODEL_VERTS_MAX}
  KEK_POOL_MODEL_FACES_MAX=${KEK_POOL_MODEL_FACES_MAX}
  KEK_POOL_MODEL_COLORS_MAX=${KEK_POOL_MODEL_COLORS_MAX}
  KEK_POOL_MODEL_UVS_MAX=${KEK_POOL_MODEL_UVS_MAX}
  KEK_POOL_TEXTURE_PIXELS_MAX=${KEK_POOL_TEXTURE_PIXELS_MAX}
  KEK_PALETTE_SHADING_LEVELS=${KEK_PALETTE_SHADING_LEVELS}
)
if(UNIX)
  # sqrtf/sinf/cosf/tanf/floorf/roundf/fabsf. Recent glibc folds libm into libc,
  # but older ones and musl still need the explicit link.
  target_link_libraries(kek PUBLIC m)
endif()


######## The game ########
add_library(${GAME_NAME}
  game/include/game_scenes/game_scene_entry.h
  game/include/game_scene_loader.h
  game/include/game_tag.h
  game/include/game.h
  game/game_scene_entry.c
  game/game_scene_loader.c
  game/game.c
)
set_target_properties(${GAME_NAME} PROPERTIES 
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)
target_include_directories(${GAME_NAME} PUBLIC
  game/include/
)
target_link_libraries(${GAME_NAME} PRIVATE kek)


######## Platform implementations ########
# SDL3 executable — platform-agnostic, the only platform layer implemented so far.
add_executable(${GAME_NAME}_sdl
  platform/main_sdl.c
  platform/platform_assets_fs.h
  platform/platform_assets_fs.c
)
set_target_properties(${GAME_NAME}_sdl PROPERTIES
  C_STANDARD 11 # Required by SDL3
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)
target_link_libraries(${GAME_NAME}_sdl PRIVATE kek ${GAME_NAME} SDL3::SDL3)

if(WIN32)
  add_custom_command(
    TARGET ${GAME_NAME}_sdl POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:${GAME_NAME}_sdl>"
    VERBATIM
  )
endif()

if(EMSCRIPTEN)
  # The browser build is one self-contained page: index.html + .js + .wasm, with
  # the assets baked into the module. Nothing is fetched at runtime and
  # platform_assets_fs.c keeps working unchanged — MEMFS mounts the embedded
  # files at /assets and the working directory is /, so "./assets/" resolves.
  set(KEK_WEB_SHELL "${CMAKE_SOURCE_DIR}/platform/web/shell.html")
  file(GLOB_RECURSE KEK_WEB_ASSET_FILES "${CMAKE_SOURCE_DIR}/assets/*")

  set_target_properties(${GAME_NAME}_sdl PROPERTIES
    OUTPUT_NAME "index"
    SUFFIX ".html"
    LINK_DEPENDS "${KEK_WEB_SHELL};${KEK_WEB_ASSET_FILES}"
  )
  target_link_options(${GAME_NAME}_sdl PRIVATE
    "--embed-file" "${CMAKE_SOURCE_DIR}/assets@/assets"
    "--shell-file" "${KEK_WEB_SHELL}"
    # kek_file_model_load() keeps a whole KMF's worth of vertices, normals, UVs
    # and faces in locals — well past Emscripten's 64 KB default stack.
    "-sSTACK_SIZE=1048576"
    # No allocation past startup, so the heap can be fixed instead of growable.
    "-sINITIAL_MEMORY=33554432"
    "-sALLOW_MEMORY_GROWTH=0"
    # SDL_AppIterate returns every frame, so the runtime has to stay alive.
    "-sEXIT_RUNTIME=0"
  )
else()
  # The game resolves assets against the working directory (see "./assets/" in
  # platform/main_sdl.c), so they have to sit next to the executable.
  add_custom_command(
    TARGET ${GAME_NAME}_sdl POST_BUILD
    COMMAND ${CMAKE_COMMAND} -E remove_directory
            "$<TARGET_FILE_DIR:${GAME_NAME}_sdl>/assets"
    COMMAND ${CMAKE_COMMAND} -E copy_directory
            "${CMAKE_SOURCE_DIR}/assets"
            "$<TARGET_FILE_DIR:${GAME_NAME}_sdl>/assets"
    COMMENT "Replacing assets directory next to ${GAME_NAME}_sdl"
  )
endif()

# The editor is a desktop tool; a browser build has no use for it and Dear ImGui
# would only be dead weight in the page.
if(NOT EMSCRIPTEN)

######## ImGui ########
add_library(imgui
  ${KEK_IMGUI_DIR}/imgui.cpp
  ${KEK_IMGUI_DIR}/imgui_draw.cpp
  ${KEK_IMGUI_DIR}/imgui_tables.cpp
  ${KEK_IMGUI_DIR}/imgui_widgets.cpp
  ${KEK_IMGUI_DIR}/backends/imgui_impl_sdl3.cpp
  ${KEK_IMGUI_DIR}/backends/imgui_impl_sdlrenderer3.cpp
)
set_target_properties(imgui PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
target_include_directories(imgui PUBLIC
  ${KEK_IMGUI_DIR}
  ${KEK_IMGUI_DIR}/backends
)
target_link_libraries(imgui PUBLIC SDL3::SDL3)

######## kek_editor ########
add_executable(kek_editor
  tools/kek_editor/main.cpp
)
set_target_properties(kek_editor PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
target_link_libraries(kek_editor PRIVATE kek SDL3::SDL3 imgui)

if(WIN32)
  add_custom_command(
    TARGET kek_editor POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:kek_editor>"
    VERBATIM
  )
endif()

endif() # NOT EMSCRIPTEN

# if(APPLE)
#   set_target_properties(MyGame PROPERTIES
#     INSTALL_RPATH "@executable_path;@loader_path"
#     BUILD_WITH_INSTALL_RPATH TRUE
#     MACOSX_RPATH TRUE
#   )
# elseif(UNIX)
#   set_target_properties(MyGame PROPERTIES
#     INSTALL_RPATH "$ORIGIN"
#     BUILD_WITH_INSTALL_RPATH TRUE
#   )
# endif()
