add_library(ann STATIC ann_numeric.c ann_flat.c ann_vector.c)
target_link_libraries(ann m mp_embedding)

add_library(ann_usearch STATIC ann_usearch.cc)
target_compile_features(ann_usearch PRIVATE cxx_std_20)
target_include_directories(ann_usearch SYSTEM PRIVATE
                           ${PROJECT_SOURCE_DIR}/third_party/USearch/include
                           ${PROJECT_SOURCE_DIR}/third_party/USearch/c)
target_link_libraries(ann_usearch ann)

add_library(ann_backends STATIC ann_registry.c)
target_link_libraries(ann_backends ann_usearch ann)
