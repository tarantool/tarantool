add_library(tzcode STATIC strftime.c strptime.c timelocal.c)
target_link_libraries(tzcode)
