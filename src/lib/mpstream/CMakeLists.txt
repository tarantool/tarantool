add_library(mpstream STATIC mpstream.c)
target_link_libraries(mpstream core uuid ${MSGPUCK_LIBRARIES})
