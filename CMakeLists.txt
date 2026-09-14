cmake_minimum_required(VERSION 3.10.0)

# Project settings
set(GAME_NAME "SnusShooter")
set(USE_SDL3_SOURCE OFF)

project(${GAME_NAME} VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(KEK_MODEL_POOL_CAPACITY 16 CACHE STRING "Max pooled models")
set(KEK_TEXTURE_POOL_CAPACITY 16 CACHE STRING "Max pooled textures")
set(KEK_POOL_MODEL_VERTS_MAX 1024 CACHE STRING "Max vertices per pooled model")
set(KEK_POOL_MODEL_FACES_MAX 1024 CACHE STRING "Max faces per pooled model")
set(KEK_POOL_MODEL_COLORS_MAX 1024 CACHE STRING "Max face colors per pooled model")
set(KEK_POOL_MODEL_UVS_MAX 1024 CACHE STRING "Max textured faces per pooled model")
set(KEK_POOL_TEXTURE_PIXELS_MAX 65536 CACHE STRING "Max pixels per pooled texture")
set(KEK_PALETTE_SHADING_LEVELS 4 CACHE STRING "Max shading levels")

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
# Win32 executable
if(USE_SDL3_SOURCE)
  add_subdirectory(ext/SDL-release-3.4.2 EXCLUDE_FROM_ALL)
else()
  list(APPEND CMAKE_PREFIX_PATH ext/SDL3-3.4.2)
  find_package(SDL3 REQUIRED)
endif()
add_executable(${GAME_NAME}_win32
  platform/main_win32.c
  platform/platform_assets_fs.h
  platform/platform_assets_fs.c
)
set_target_properties(${GAME_NAME}_win32 PROPERTIES
  C_STANDARD 11 # Required by SDL3
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)
target_link_libraries(${GAME_NAME}_win32 PRIVATE kek ${GAME_NAME} SDL3::SDL3)

if(NOT USE_SDL3_SOURCE)
  add_custom_command(
    TARGET ${GAME_NAME}_win32 POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:${GAME_NAME}_win32>"
    VERBATIM
)
endif()

add_custom_command(
  TARGET ${GAME_NAME}_win32 POST_BUILD
  COMMAND ${CMAKE_COMMAND} -E remove_directory
          "$<TARGET_FILE_DIR:${GAME_NAME}_win32>/assets"
  COMMAND ${CMAKE_COMMAND} -E copy_directory
          "${CMAKE_SOURCE_DIR}/assets"
          "$<TARGET_FILE_DIR:${GAME_NAME}_win32>/assets"
  COMMENT "Replacing assets directory next to ${GAME_NAME}_win32"
)

######## ImGui ########
add_library(imgui
  ext/imgui-1.92.6-docking/imgui.cpp
  ext/imgui-1.92.6-docking/imgui_draw.cpp
  ext/imgui-1.92.6-docking/imgui_tables.cpp
  ext/imgui-1.92.6-docking/imgui_widgets.cpp
  ext/imgui-1.92.6-docking/backends/imgui_impl_sdl3.cpp
  ext/imgui-1.92.6-docking/backends/imgui_impl_sdlrenderer3.cpp
)
set_target_properties(imgui PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
target_include_directories(imgui PUBLIC
  ext/imgui-1.92.6-docking
  ext/imgui-1.92.6-docking/backends
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

if(NOT USE_SDL3_SOURCE)
  add_custom_command(
    TARGET kek_editor POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:kek_editor>"
    VERBATIM
  )
endif()

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
