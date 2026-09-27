# Benchmarks. Not tests: they pass or fail nothing, they print timings. Plain
# C99 over the library like the tests, so they build for the target too.
add_executable(kek_bench_raster bench_raster.c)
set_target_properties(kek_bench_raster PROPERTIES
  C_STANDARD 99
  C_STANDARD_REQUIRED ON
  C_EXTENSIONS OFF
)
kek_target_warnings(kek_bench_raster)
target_link_libraries(kek_bench_raster PRIVATE kek)
