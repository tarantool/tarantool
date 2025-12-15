get_filename_component(test_name ${CMAKE_CURRENT_SOURCE_DIR} NAME)
BuildTestCLib(libcur_L_coroutine libcur_L_coroutine.c ${test_name}.test.lua)
