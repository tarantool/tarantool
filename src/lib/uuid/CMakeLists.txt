add_library(uuid STATIC tt_uuid.c mp_uuid.c)
target_link_libraries(uuid core bit)
