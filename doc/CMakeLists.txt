
add_subdirectory(man)

if (ENABLE_DOC)
    add_subdirectory(user)
    add_subdirectory(developer)
endif()

add_subdirectory(www-data.in EXCLUDE_FROM_ALL)
