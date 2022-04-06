add_library(tzcode STATIC strftime.c strptime.c timelocal.c timezone.c)
target_link_libraries(tzcode)
