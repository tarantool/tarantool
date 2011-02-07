add_custom_target(test
    COMMAND ${PROJECT_SOURCE_DIR}/test/test-run.py --bindir=${PROJECT_BINARY_DIR}/core --vardir=${PROJECT_BINARY_DIR}/test/var
    )
