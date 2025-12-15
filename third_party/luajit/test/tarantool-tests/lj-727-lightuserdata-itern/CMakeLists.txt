get_filename_component(test_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)
BuildTestCLib(lightuserdata lightuserdata.c ${test_name}.test.lua)
