get_filename_component(test_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)
BuildTestCLib(libtestxor testxor.c ${test_name}.test.lua)
