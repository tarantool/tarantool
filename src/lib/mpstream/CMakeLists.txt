add_library(mpstream STATIC mpstream.c)
target_link_libraries(mpstream core ${MSGPUCK_LIBRARIES})
