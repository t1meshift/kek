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
# executable of the same name and registers it with CTest.
function(kek_add_test name)
  add_executable(${name} ${name}.c ${ARGN})
  set_target_properties(${name} PROPERTIES
    C_STANDARD 99
    C_STANDARD_REQUIRED ON
    C_EXTENSIONS OFF
  )
  kek_target_warnings(${name})
  target_link_libraries(${name} PRIVATE kek unity)
  add_test(NAME ${name} COMMAND ${name})
endfunction()

kek_add_test(test_asset_memory)
