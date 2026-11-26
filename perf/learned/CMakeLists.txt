# Learned index research stand. It does not use Google Benchmark and is not
# run by ctest, because it needs generated datasets.
add_executable(learned_gen gen.cc gen_keys.cc gen_ops.cc)
add_executable(learned_bench bench.cc)
target_include_directories(learned_bench SYSTEM PRIVATE
                           ${PROJECT_SOURCE_DIR}/third_party/alex)
target_link_libraries(learned_bench small)
