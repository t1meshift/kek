# The editor: Dear ImGui (C++20) on SDL3, linking against kek. A desktop tool,
# shipped on its own; a browser build has no use for it.
#
# Built from the top-level CMakeLists.txt it gets this tree's kek; on its own it
# finds an installed one:
#   cmake -S tools/kek_editor -B build-editor -DCMAKE_PREFIX_PATH=<where kek is installed>
cmake_minimum_required(VERSION 3.20)

project(kek_editor VERSION 0.1.0 LANGUAGES C CXX)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(NOT TARGET kek::kek)
  find_package(kek REQUIRED)
endif()

option(KEK_FETCH_DEPS "Resolve dependencies automatically (system package or FetchContent)" ON)
set(KEK_SDL3_LOCAL_DIR "${CMAKE_SOURCE_DIR}/ext/SDL3-3.4.2"
    CACHE PATH "Prebuilt SDL3 used when KEK_FETCH_DEPS=OFF")
set(KEK_IMGUI_LOCAL_DIR "${CMAKE_SOURCE_DIR}/ext/imgui-1.92.6-docking"
    CACHE PATH "Dear ImGui sources used when KEK_FETCH_DEPS=OFF")
set(KEK_SDL3_TAG "release-3.4.2" CACHE STRING "SDL3 git tag to fetch")
set(KEK_IMGUI_TAG "v1.92.6-docking" CACHE STRING "Dear ImGui git tag to fetch")

if(KEK_FETCH_DEPS)
  include(FetchContent)

  # The demo resolves SDL3 the same way; whichever of the two comes first in a
  # build of the whole tree does it for both. A system-wide SDL3 is just as good
  # and saves building it from source.
  if(NOT TARGET SDL3::SDL3)
    find_package(SDL3 QUIET)
    if(NOT SDL3_FOUND)
      FetchContent_Declare(SDL3
        GIT_REPOSITORY https://github.com/libsdl-org/SDL.git
        GIT_TAG        ${KEK_SDL3_TAG}
        GIT_SHALLOW    TRUE
      )
      FetchContent_MakeAvailable(SDL3)
    endif()
  endif()

  # Dear ImGui ships no CMakeLists; FetchContent_MakeAvailable only downloads it
  # and the imgui target below is built from ${imgui_SOURCE_DIR}.
  FetchContent_Declare(imgui
    GIT_REPOSITORY https://github.com/ocornut/imgui.git
    GIT_TAG        ${KEK_IMGUI_TAG}
    GIT_SHALLOW    TRUE
  )
  FetchContent_MakeAvailable(imgui)
  set(KEK_IMGUI_DIR "${imgui_SOURCE_DIR}")
else()
  if(NOT TARGET SDL3::SDL3)
    list(APPEND CMAKE_PREFIX_PATH "${KEK_SDL3_LOCAL_DIR}")
    find_package(SDL3 REQUIRED)
  endif()
  set(KEK_IMGUI_DIR "${KEK_IMGUI_LOCAL_DIR}")
endif()

# The same set kek builds with, for the same reasons; see the top-level
# CMakeLists.txt. Not applied to the vendored Dear ImGui, whose warnings are not
# ours to fix.
option(KEK_WERROR "Treat compiler warnings as errors" OFF)

function(kek_editor_warnings target)
  if(MSVC)
    target_compile_options(${target} PRIVATE /W4)
  else()
    target_compile_options(${target} PRIVATE -Wall -Wextra -Wpedantic)
    if(KEK_WERROR)
      target_compile_options(${target} PRIVATE -Werror)
    endif()
  endif()
endfunction()

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
  main.cpp
)
set_target_properties(kek_editor PROPERTIES
  CXX_STANDARD 20
  CXX_STANDARD_REQUIRED ON
  CXX_EXTENSIONS OFF
)
kek_editor_warnings(kek_editor)
target_link_libraries(kek_editor PRIVATE kek::kek SDL3::SDL3 imgui)

if(WIN32)
  add_custom_command(
    TARGET kek_editor POST_BUILD
    COMMAND "${CMAKE_COMMAND}" -E copy "$<TARGET_FILE:SDL3::SDL3>" "$<TARGET_FILE_DIR:kek_editor>"
    VERBATIM
  )
endif()
