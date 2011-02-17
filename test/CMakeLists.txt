add_custom_target(test
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py --bindir=${PROJECT_BINARY_DIR}/mod/silverbox --vardir=${PROJECT_BINARY_DIR}/test/var
    )
