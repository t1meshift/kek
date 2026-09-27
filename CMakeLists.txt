cmake_minimum_required(VERSION 3.20)

project(kek VERSION 0.1.0 LANGUAGES C)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
include(GNUInstallDirs)

# The library is what this project ships. The demo and the editor are projects
# of their own that consume it the way a game would (demo/CMakeLists.txt,
# tools/kek_editor/CMakeLists.txt); in this tree they are added at the end as
# subdirectories, and they are off when kek is itself someone's subdirectory.
# PROJECT_IS_TOP_LEVEL would say the same, from CMake 3.21.
if(CMAKE_SOURCE_DIR STREQUAL PROJECT_SOURCE_DIR)
  set(KEK_IS_TOP_LEVEL ON)
else()
  set(KEK_IS_TOP_LEVEL OFF)
endif()

# Dependencies are resolved by CMake so a fresh clone builds without setup;
# the library itself has none, the tests need Unity, and demo/ and the editor
# resolve SDL3 and Dear ImGui under the same switch. Turn this OFF to use local
# copies under ext/ instead (see KEK_UNITY_LOCAL_DIR below, and
# KEK_SDL3_LOCAL_DIR and KEK_IMGUI_LOCAL_DIR in those projects).
option(KEK_FETCH_DEPS "Resolve dependencies automatically (system package or FetchContent)" ON)
set(KEK_UNITY_LOCAL_DIR "${CMAKE_SOURCE_DIR}/ext/Unity-2.6.1"
    CACHE PATH "Unity sources used when KEK_FETCH_DEPS=OFF")
set(KEK_UNITY_TAG "v2.6.1" CACHE STRING "Unity (ThrowTheSwitch) git tag to fetch")

# The unit tests need nothing but a C99 compiler and Unity, so they build for
# any target the engine does. Off in the browser build by default: there is
# nothing there to run them. The editor is a desktop tool; a browser build has
# no use for it.
if(KEK_IS_TOP_LEVEL AND NOT EMSCRIPTEN)
  set(KEK_DESKTOP_DEFAULT ON)
else()
  set(KEK_DESKTOP_DEFAULT OFF)
endif()
option(KEK_BUILD_TESTS "Build the engine's unit tests (tests/) and register them with CTest" ${KEK_DESKTOP_DEFAULT})
option(KEK_BUILD_DEMO "Build the demo (demo/) against this tree's kek" ${KEK_IS_TOP_LEVEL})
option(KEK_BUILD_EDITOR "Build the editor (tools/kek_editor/) against this tree's kek" ${KEK_DESKTOP_DEFAULT})
option(KEK_INSTALL "Generate install rules for kek and its CMake package" ${KEK_IS_TOP_LEVEL})

if(KEK_BUILD_TESTS)
  if(KEK_FETCH_DEPS)
    include(FetchContent)
    # Unity does ship a CMakeLists, but it builds as C11 with -Werror and a
    # compiler-specific warning list, and adds install rules — none of which
    # should decide whether our tests build on a toolchain Unity has never
    # seen. Pointing SOURCE_SUBDIR at src/, which has no CMakeLists, makes
    # FetchContent_MakeAvailable only download it; tests/CMakeLists.txt builds
    # the one source file itself.
    FetchContent_Declare(unity
      GIT_REPOSITORY https://github.com/ThrowTheSwitch/Unity.git
      GIT_TAG        ${KEK_UNITY_TAG}
      GIT_SHALLOW    TRUE
      SOURCE_SUBDIR  src
    )
    FetchContent_MakeAvailable(unity)
    set(KEK_UNITY_DIR "${unity_SOURCE_DIR}")
  else()
    set(KEK_UNITY_DIR "${KEK_UNITY_LOCAL_DIR}")
  endif()
endif()

set(KEK_TARGET_FPS 30 CACHE STRING "Frames per second the platform layer paces to")
set(KEK_MODEL_POOL_CAPACITY 16 CACHE STRING "Max pooled models")
set(KEK_TEXTURE_POOL_CAPACITY 16 CACHE STRING "Max pooled textures")
set(KEK_POOL_MODEL_VERTS_MAX 1024 CACHE STRING "Max vertices per pooled model")
set(KEK_POOL_MODEL_FACES_MAX 1024 CACHE STRING "Max faces per pooled model")
set(KEK_POOL_MODEL_COLORS_MAX 1024 CACHE STRING "Max face colors per pooled model")
set(KEK_POOL_MODEL_UVS_MAX 1024 CACHE STRING "Max UVs a KMF may declare, and max faces per pooled textured model")
set(KEK_POOL_TEXTURE_PIXELS_MAX 65536 CACHE STRING "Max pixels per pooled texture")
set(KEK_PALETTE_SHADING_LEVELS 4 CACHE STRING "Max shading levels")

# The output directories below are per-configuration, and with a single-config
# generator (Ninja, Make) $<CONFIG> is empty unless a build type is set — which
# would drop the binaries straight into ${CMAKE_BINARY_DIR}. Only for a build of
# this tree: as someone's subdirectory, where binaries go is theirs to say.
if(KEK_IS_TOP_LEVEL)
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
endif()

# At the top level rather than inside tests/, so `ctest --test-dir build` finds
# the suite from the build root — which is what CI runs. The tests themselves
# are added further down, once the kek target exists.
enable_testing()

######## Warnings ########
# Applied to the engine and its tests. PRIVATE so the flags do not leak into
# anything linking against kek; the demo and the editor have their own copy.
#
# MSVC gets /W4 but not /WX yet: nobody has looked at what /W4 has to say about
# this code, and a red master on a compiler none of us runs is not worth it.
option(KEK_WERROR "Treat compiler warnings as errors" OFF)

function(kek_target_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(KEK_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

######## KEK engine ########
# A variable rather than inline in add_library(), so tests/ can build a second
# copy of the engine under different KEK_POOL_* limits (see
# KEK_BUILD_TESTS below) without a second list to keep in sync.
set(KEK_ENGINE_SOURCES
  kek/include/kek_asset.h
  kek/include/kek_asset_memory.h
  kek/include/kek_keyboard.h
  kek/include/kek_light.h
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
  kek/kek_internal.h
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
  kek/io/kek_asset_memory.c
)

add_library(kek ${KEK_ENGINE_SOURCES})
set_target_properties(kek PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)
kek_target_warnings(kek)
# Installed under include/kek/ so the headers do not land loose in a system
# include directory, and put on the include path from there, so a consumer
# writes <kek.h> whichever way it got kek.
target_include_directories(kek PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/kek/include>
  $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}/kek>
)
target_compile_definitions(kek PUBLIC
  KEK_TARGET_FPS=${KEK_TARGET_FPS}u
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

# The name a consumer uses whichever way it got kek, add_subdirectory or
# find_package, so demo/ and the editor are written once for both.
add_library(kek::kek ALIAS kek)

######## Install ########
# find_package(kek) after `cmake --install`. The package is kek::kek and
# nothing else; the KEK_* limits above are baked into it.
if(KEK_INSTALL)
  include(CMakePackageConfigHelpers)

  install(TARGETS kek
    EXPORT kekTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
  )
  install(DIRECTORY kek/include/
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/kek
    FILES_MATCHING PATTERN "*.h"
  )
  install(EXPORT kekTargets
    NAMESPACE kek::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kek
  )

  configure_package_config_file(cmake/kekConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/kekConfig.cmake"
    INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kek
  )
  # 0.x: a minor version is allowed to break the API, so only the same one
  # is compatible.
  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/kekConfigVersion.cmake"
    COMPATIBILITY SameMinorVersion
  )
  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/kekConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/kekConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/kek
  )
endif()


######## Tests ########
if(KEK_BUILD_TESTS)
  # test_file_model_uv_overflow needs KEK_POOL_MODEL_UVS_MAX below
  # KEK_POOL_MODEL_FACES_MAX, to get a faces_count that clears FACES_MAX but
  # not UVS_MAX — the case BACKLOG.md's "KEK_POOL_MODEL_UVS_MAX means two
  # things" describes, which the default config (every KEK_POOL_* limit the
  # same 1024) cannot produce. Those limits are PUBLIC compile definitions on
  # `kek`, so a test executable cannot override them per-TU while linking the
  # shared library; a second, small static build of the whole engine from the
  # same KEK_ENGINE_SOURCES is the least invasive way to get one binary under
  # different limits without duplicating the source list. KEK_MODEL_POOL_CAPACITY
  # is cut to 2 as well — one slot for the default cube kek_init() always
  # creates, one for the test's model — so the test's model lands in the last
  # slot of the pool's static storage and an overflow of its face_textures
  # runs past the end of that whole array, where ASan's global-buffer-overflow
  # check is guaranteed to catch it rather than just corrupting whatever pool
  # slot happens to sit next door.
  add_library(kek_test_small_uvs STATIC ${KEK_ENGINE_SOURCES})
  set_target_properties(kek_test_small_uvs PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
  )
  kek_target_warnings(kek_test_small_uvs)
  target_include_directories(kek_test_small_uvs PUBLIC
    kek/include/
  )
  target_compile_definitions(kek_test_small_uvs PUBLIC
    KEK_TARGET_FPS=${KEK_TARGET_FPS}u
    KEK_MODEL_POOL_CAPACITY=2
    KEK_TEXTURE_POOL_CAPACITY=${KEK_TEXTURE_POOL_CAPACITY}
    KEK_POOL_MODEL_VERTS_MAX=${KEK_POOL_MODEL_VERTS_MAX}
    KEK_POOL_MODEL_FACES_MAX=${KEK_POOL_MODEL_FACES_MAX}
    KEK_POOL_MODEL_COLORS_MAX=${KEK_POOL_MODEL_COLORS_MAX}
    # Above KEK_CUBE_MODEL.textures_count (12, kek_model.c) or kek_pool_init's
    # clone of the default cube fails outright; still comfortably below
    # KEK_POOL_MODEL_FACES_MAX.
    KEK_POOL_MODEL_UVS_MAX=16
    KEK_POOL_TEXTURE_PIXELS_MAX=${KEK_POOL_TEXTURE_PIXELS_MAX}
    KEK_PALETTE_SHADING_LEVELS=${KEK_PALETTE_SHADING_LEVELS}
  )
  if(UNIX)
    target_link_libraries(kek_test_small_uvs PUBLIC m)
  endif()

  add_subdirectory(tests)
endif()

######## Consumers ########
if(KEK_BUILD_DEMO)
  add_subdirectory(demo)
endif()
if(KEK_BUILD_EDITOR)
  add_subdirectory(tools/kek_editor)
endif()
