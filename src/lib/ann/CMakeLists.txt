add_library(ann STATIC ann_numeric.c ann_flat.c ann_vector.c ann_registry.c)
target_link_libraries(ann m mp_embedding)
