add_custom_target(www-data ALL
    COMMAND ${PROJECT_SOURCE_DIR}/scripts/static.py
            --source-path ${PROJECT_SOURCE_DIR}/doc/www-data.in
            --output-path ${PROJECT_BINARY_DIR}/doc/www-data)
