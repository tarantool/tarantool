add_subdirectory(user)
add_subdirectory(developer)

add_custom_target(www-data ALL
    COMMAND rm -rf www-data && mkdir www-data && ../scripts/static.py --source-path www-data.in --output-path www-data)
