# Engine unit tests. Unity plus CTest, and nothing else: no SDL3, no fork, no
# prebuilt library, because the same suite has to build for foreign targets
# (DJGPP, for the 486) and run there under an emulator. Each file is its own
# executable, so a crash in one suite cannot take the others with it.

######## Unity ########
# Built here from its one source file rather than through Unity's own
# CMakeLists — see the FetchContent_Declare in the top-level CMakeLists.txt.
# Vendored, so it does not get kek_target_warnings, the same as imgui.
add_library(unity STATIC
  ${KEK_UNITY_DIR}/src/unity.c
)
set_target_properties(unity PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)
target_include_directories(unity PUBLIC
  ${KEK_UNITY_DIR}/src
)

# kek_add_test(<name> [extra sources...]) builds tests/<name>.c into an
# executable of the same name and registers it with CTest, against the
# engine built with the project's own KEK_POOL_* limits.
function(kek_add_test name)
  kek_add_test_against(${name} kek ${ARGN})
endfunction()

# kek_add_test_against(<name> <lib> [extra sources...]) is the same, but
# against a named engine library instead of `kek` — for test_file_model_uv_overflow,
# which needs the smaller-limit kek_test_small_uvs from the top-level
# CMakeLists.txt to reproduce a bug the default limits cannot.
function(kek_add_test_against name lib)
  add_executable(${name} ${name}.c ${ARGN})
  set_target_properties(${name} PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
  )
  kek_target_warnings(${name})
  target_link_libraries(${name} PRIVATE ${lib} unity)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

kek_add_test(test_asset_memory)
kek_add_test(test_memory)
kek_add_test(test_math)
kek_add_test(test_palette)
kek_add_test(test_2d_clip_line)
kek_add_test(test_pool test_support.c)
kek_add_test(test_file_image test_support.c)
kek_add_test(test_file_model test_support.c)
kek_add_test_against(test_file_model_uv_overflow kek_test_small_uvs test_support.c)
kek_add_test(test_render_2d test_support.c)
kek_add_test(test_render_3d test_support.c)
kek_add_test(test_texture_sample test_support.c)
kek_add_test(test_clip_near)
kek_add_test(test_input test_support.c)
