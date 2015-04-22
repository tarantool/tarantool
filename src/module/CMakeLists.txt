set(api_headers ../say.h ../coeio.h ../lua/utils.h)
apigen(${api_headers})

add_subdirectory(sql)
add_subdirectory(pg)
add_subdirectory(mysql)
