add_library(coll STATIC coll.c coll_def.c)
target_link_libraries(coll core ${ICU_LIBRARIES})

if (ENABLE_BUNDLED_ICU)
    add_dependencies(coll bundled-icu)
endif()
