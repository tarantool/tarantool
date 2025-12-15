set(PERF_SUITE_NAME LuaJIT-benches)
set(LUA_BENCH_SUFFIX .lua)

AddBenchTarget(${PERF_SUITE_NAME})

# Input for the k-nucleotide and revcomp benchmarks.
set(FASTA_NAME ${CMAKE_CURRENT_BINARY_DIR}/FASTA_5000000)
add_custom_target(FASTA_5000000
  COMMAND ${LUAJIT_BINARY}
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/fasta.lua 5000000 > ${FASTA_NAME}
  OUTPUT ${FASTA_NAME}
  DEPENDS luajit-main
  COMMENT "Generate ${FASTA_NAME}."
)

make_lua_path(LUA_PATH
  PATHS
    ${LUA_PATH_BENCH_BASE}
    ${CMAKE_CURRENT_SOURCE_DIR}/libs/?.lua
)

# Input for the <sum-file.lua> benchmark.
set(SUM_NAME ${CMAKE_CURRENT_BINARY_DIR}/SUMCOL_5000.txt)
# Remove possibly existing file.
file(REMOVE ${SUM_NAME})

set(SUMCOL_FILE ${CMAKE_CURRENT_SOURCE_DIR}/SUMCOL_1.txt)
file(READ ${SUMCOL_FILE} SUMCOL_CONTENT)
foreach(_unused RANGE 4999)
  file(APPEND ${SUM_NAME} "${SUMCOL_CONTENT}")
endforeach()

file(GLOB benches "${CMAKE_CURRENT_SOURCE_DIR}/*${LUA_BENCH_SUFFIX}")
foreach(bench_path ${benches})
  file(RELATIVE_PATH bench_name ${CMAKE_CURRENT_SOURCE_DIR} ${bench_path})
  set(bench_title "perf/${PERF_SUITE_NAME}/${bench_name}")
  if(bench_name MATCHES "k-nucleotide" OR bench_name MATCHES "revcomp")
    AddBench(${bench_name}
      ${bench_path} ${PERF_SUITE_NAME} "${LUA_PATH}" ${FASTA_NAME}
    )
    add_dependencies(${bench_name} FASTA_5000000)
  elseif(bench_name MATCHES "sum-file")
    AddBench(${bench_name}
      ${bench_path} ${PERF_SUITE_NAME} "${LUA_PATH}" ${SUM_NAME}
    )
  else()
    AddBench(${bench_name} ${bench_path} ${PERF_SUITE_NAME} "${LUA_PATH}")
  endif()
endforeach()

# We need to generate the file before we run tests.
add_dependencies(${PERF_SUITE_NAME}-console FASTA_5000000)
