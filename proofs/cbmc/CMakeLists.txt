find_program(CBMC cbmc)
if (NOT EXISTS ${CBMC})
  message(FATAL_ERROR "CBMC is required for running proofs")
endif()

get_filename_component(GOTO_CC ${CMAKE_C_COMPILER} NAME)
if (NOT ${GOTO_CC} MATCHES "goto-cc")
  message(FATAL_ERROR "goto-cc must be used as a C compiler")
endif()

set(TEST_SUITE_NAME "tarantool-proofs")
set(PROOF_TARGETS "")
set(TEST_SUITE_PREFIX "proofs")

# XXX: The call produces both test and target
# <${TEST_SUITE_NAME}.test-deps> as a side effect.
_add_test_suite_target(${TEST_SUITE_NAME}
  LABELS ${TEST_SUITE_NAME}
  DEPENDS ${PROOF_TARGETS}
)

list(APPEND COMMON_CBMC_OPTIONS
  # Enable array bounds checks.
  --bounds-check
  # Check whether values can be represented after type cast.
  --conversion-check
  # Enable division by zero checks.
  --div-by-zero-check
  # Checks that all enum type expressions have values in the enum
  # range.
  --enum-range-check
  # Check floating-point for +/-Inf.
  --float-overflow-check
  # Enable memory leak checks.
  --memory-leak-check
  # Enable memory cleanup checks.
  --memory-cleanup-check
  # Check floating-point for NaN.
  --nan-check
  # Checks that all pointers in pointer primitives are valid or
  # NULL.
  --pointer-primitive-check
  # Enable pointer checks.
  --pointer-check
  # Enable pointer arithmetic over- and underflow.
  --pointer-overflow-check
  # Include checks that are trivially true.
  --retain-trivial-checks
  # Enable signed arithmetic over- and underflow.
  --signed-overflow-check
  # Enable arithmetic over- and underflow checks.
  --unsigned-overflow-check
  # Check shift greater than bit-width.
  --undefined-shift-check

  # The option checks whether `--unwind` is large enough to cover
  # all program paths. If the argument is too small, CBMC will
  # detect that not enough unwinding is done reports that an
  # unwinding assertion has failed.
  --unwinding-assertions
  # Drop functions trivially unreachable from the `main` function.
  --drop-unused-functions
  --flush

  --verbosity 9
)

include(CMakeParseArguments)
message(STATUS "Add CBMC proofs (${TEST_SUITE_NAME})")

function(create_proof)
  cmake_parse_arguments(
    PROOF
    ""
    "PREFIX"
    "SOURCES;LIBRARIES;CBMC_OPTS"
    ${ARGN}
  )
  add_executable(${PROOF_PREFIX}.proof ${PROOF_SOURCES})
  target_link_libraries(${PROOF_PREFIX}.proof ${PROOF_LIBRARIES})
  set(PROOF_TARGETS ${PROOF_TARGETS} ${PROOF_PREFIX}.proof PARENT_SCOPE)
  set(test_title ${TEST_SUITE_PREFIX}/cbmc/${PROOF_PREFIX}.proof)
  add_test(NAME ${test_title}
    COMMAND ${CBMC} ${PROOF_CBMC_OPTS}
            ${CMAKE_CURRENT_BINARY_DIR}/${PROOF_PREFIX}.proof
  )
  set_tests_properties(${test_title} PROPERTIES
    LABELS ${TEST_SUITE_NAME}
    DEPENDS ${TEST_SUITE_NAME}-deps
  )
endfunction()

list(APPEND STAILQ_FUNCTIONS
  stailq_add
  stailq_add_tail
  stailq_concat
  stailq_create
  stailq_cut_tail
  stailq_empty
  stailq_first
  stailq_insert
  stailq_insert_entry
  stailq_last
  stailq_next
  stailq_reverse
  stailq_shift
)
set(STAILQ_MAX_ITEMS 5)
foreach(proof_name ${STAILQ_FUNCTIONS})
  create_proof(PREFIX ${proof_name}
    SOURCES stailq.c
    CBMC_OPTS ${COMMON_CBMC_OPTIONS}
  )
  string(TOUPPER ${proof_name} proof_macro)
  target_compile_definitions(${proof_name}.proof PRIVATE
    ${proof_macro}=1
    MAX_ITEMS=${STAILQ_MAX_ITEMS}
  )
endforeach()

list(APPEND HEAP_FUNCTIONS
  heap_create
  heap_delete
  heap_destroy
  heap_insert
  heap_iterator_init
  heap_iterator_next
  heap_pop
  heap_top
  heap_update_all
  heap_update
)

# "Invariant check failed: src/util/lower_byte_operators.cpp:73 function: adjust_width",
# https://github.com/diffblue/cbmc/issues/8475
set(HEAP_SIFT_UP_UNWIND 5)
set(HEAP_SIFT_DOWN_UNWIND 5)
set(HEAP_CHECK_UNWIND 50)
foreach(proof_name ${HEAP_FUNCTIONS})
  create_proof(PREFIX ${proof_name}
    SOURCES heap.c
    CBMC_OPTS ${COMMON_CBMC_OPTIONS}
              --unwindset test_heap_sift_up.0:${HEAP_SIFT_UP_UNWIND}
              --unwindset test_heap_sift_down.0:${HEAP_SIFT_UP_UNWIND}
              --unwindset test_heap_update_all.0:10
              --unwindset test_heap_check.0:${HEAP_CHECK_UNWIND}
  )
  string(TOUPPER ${proof_name} proof_macro)
  target_compile_definitions(${proof_name}.proof PRIVATE
    ${proof_macro}=1
  )
endforeach()

add_custom_target(proofs
  DEPENDS ${PROOF_TARGETS}
  COMMENT "Build proofs"
)
