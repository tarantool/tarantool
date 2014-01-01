add_custom_target(www-data-site ALL
    COMMAND ${PROJECT_SOURCE_DIR}/extra/static.py --target site
    --input  ${PROJECT_SOURCE_DIR}/doc/www-data.in
    --output ${PROJECT_BINARY_DIR}/doc/www-data
    )
add_custom_target(www-data-doc ALL
    COMMAND ${PROJECT_SOURCE_DIR}/extra/static.py --target docs
    --input  ${PROJECT_SOURCE_DIR}/doc/www-data.in
    --output ${PROJECT_BINARY_DIR}/doc/www-data
    )
