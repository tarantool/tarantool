build_module(loaderslib loaderslib.c)
build_module(on_shutdownlib on_shutdownlib.c)
build_module(xtmlib xtmlib.c)
target_link_libraries(xtmlib xtm_api_static)
