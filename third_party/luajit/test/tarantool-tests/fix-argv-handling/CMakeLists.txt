get_filename_component(test_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)
BuildTestCLib(mynewstate mynewstate.c ${test_name}.test.lua)
BuildTestCLib(execlib execlib.c ${test_name}.test.lua)
