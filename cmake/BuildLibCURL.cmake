# A macro to build the bundled libcurl
macro(curl_build)
    set(LIBCURL_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/curl)
    set(LIBCURL_BINARY_DIR ${PROJECT_BINARY_DIR}/build/curl/work)
    set(LIBCURL_INSTALL_DIR ${PROJECT_BINARY_DIR}/build/curl/dest)
    set(LIBCURL_CMAKE_FLAGS "")

    message(STATUS "Looking for zlib")
    find_path(ZLIB_INCLUDE_DIR zlib.h)
    message(STATUS "Looking for zlib.h - ${ZLIB_INCLUDE_DIR}")
    if (BUILD_STATIC)
        set(LIBZ_LIB_NAME libz.a)
    else()
        set(LIBZ_LIB_NAME z)
    endif()
    find_library(LIBZ_LIBRARY NAMES ${LIBZ_LIB_NAME})
    message(STATUS "Looking for libz - ${LIBZ_LIBRARY}")

    if (NOT ZLIB_INCLUDE_DIR OR NOT LIBZ_LIBRARY)
        message(FATAL_ERROR "Unable to find zlib")
    endif()
    get_filename_component(FOUND_ZLIB_ROOT_DIR ${ZLIB_INCLUDE_DIR} DIRECTORY)
    list(APPEND LIBCURL_CMAKE_FLAGS "-DZLIB_ROOT=${FOUND_ZLIB_ROOT_DIR}")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_ZLIB=ON")

    # Check '-pthread' in openssl compile options.
    execute_process(COMMAND openssl version -f
                    OUTPUT_VARIABLE OPENSSL_COMPILE_OPTIONS)
    # Add pthread library for openssl static library linking.
    if(NOT OPENSSL_COMPILE_OPTIONS MATCHES ".* -pthread .*")
        list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_C_FLAGS=-pthread")
    endif()

    # Add librt for clock_gettime function definition.
    if(${CMAKE_MAJOR_VERSION} VERSION_LESS "3")
        CHECK_LIBRARY_EXISTS (rt clock_gettime "" HAVE_LIBRT)
        if (HAVE_LIBRT)
            list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_C_FLAGS=-lrt")
        endif()
    endif()

    # Switch on the static build.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_STATICLIB=ON")

    # Switch off the shared build.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DBUILD_SHARED_LIBS=OFF")

    # curl uses visibility hiding mode for its symbols by default and
    # Tarantool tests can't use it. To fix it symbols hiding disabled
    # for gcc and clang.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_HIDDEN_SYMBOLS=OFF")

    # Let's disable testing for curl to save build time.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DBUILD_TESTING=OFF")

    # Setup use of openssl, use the same OpenSSL library
    # for libcurl as is used for tarantool itself.
    get_filename_component(FOUND_OPENSSL_ROOT_DIR ${OPENSSL_INCLUDE_DIR} DIRECTORY)
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_USE_OPENSSL=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DOPENSSL_ROOT_DIR=${FOUND_OPENSSL_ROOT_DIR}")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_MODULE_PATH=${PROJECT_SOURCE_DIR}/cmake")

    # Setup ARES and its library path, use either c-ares bundled
    # with tarantool or libcurl-default threaded resolver.
    if(BUNDLED_LIBCURL_USE_ARES)
        set(ENABLE_ARES "ON")
        list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_FIND_ROOT_PATH=${ARES_INSTALL_DIR}")
    else()
        set(ENABLE_ARES "OFF")
    endif()
    list(APPEND LIBCURL_CMAKE_FLAGS "-DENABLE_ARES=${ENABLE_ARES}")

    # On cmake CMAKE_USE_LIBSSH2 flag is enabled by default, we need to switch it
    # off to avoid of issues, like:
    #   ld: libssh2.c:(.text+0x4d8): undefined reference to `libssh2_*...
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_USE_LIBSSH2=OFF")

    # Switch off the group of protocols with special flag HTTP_ONLY:
    #   ftp, file, ldap, ldaps, rtsp, dict, telnet, tftp, pop3, imap, smtp.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DHTTP_ONLY=OFF")

    # Additionaly disable some more protocols.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_SMB=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_GOPHER=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_CRYPTO_AUTH=ON")

    # Switch on ca-fallback feature.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_CA_FALLBACK=ON")

    # Even though we set the external project's install dir
    # below, we still need to pass the corresponding install
    # prefix via cmake arguments.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_INSTALL_PREFIX=${LIBCURL_INSTALL_DIR}")

    # The default values for the options below are not always
    # "./lib", "./bin"  and "./include", while curl expects them
    # to be.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_INSTALL_LIBDIR=lib")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_INSTALL_INCLUDEDIR=include")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_INSTALL_BINDIR=bin")

    # Pass the same toolchain as is used to build tarantool itself,
    # because they can be incompatible.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_C_COMPILER=${CMAKE_C_COMPILER}")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_LINKER=${CMAKE_LINKER}")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_AR=${CMAKE_AR}")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_RANLIB=${CMAKE_RANLIB}")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_NM=${CMAKE_NM}")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_STRIP=${CMAKE_STRIP}")

    # Need to set values explicitly everything that is default, because
    # we don't know how defaults will be changed in a future and we don't
    # want to verify them each time we'll bump new libcurl version.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DPICKY_COMPILER=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DBUILD_CURL_EXE=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_BROTLI=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DUSE_GNUTLS=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_USE_MBEDTLS=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_USE_WOLFSSL=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCMAKE_USE_NSS=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_CA_BUNDLE=none")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_CA_PATH=none")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DUSE_LIBRTMP=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DHAVE_LIBIDN2=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DUSE_NGHTTP2=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DUSE_NGTCP2=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DUSE_NGHTTP3=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DUSE_QUICHE=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_HTTP=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_PROXY=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DENABLE_IPV6=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_COOKIES=OFF")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DENABLE_UNIX_SOCKETS=ON")
    # Should be already set by "-DHTTP_ONLY=ON" above.
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_FTP=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_FILE=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_LDAP=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_LDAPS=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_RTSP=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_DICT=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_TELNET=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_TFTP=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_POP3=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_IMAP=ON")
    list(APPEND LIBCURL_CMAKE_FLAGS "-DCURL_DISABLE_SMTP=OFF")

    include(ExternalProject)
    ExternalProject_Add(
        bundled-libcurl-project
        SOURCE_DIR ${LIBCURL_SOURCE_DIR}
        PREFIX ${LIBCURL_INSTALL_DIR}
        DOWNLOAD_DIR ${LIBCURL_BINARY_DIR}
        TMP_DIR ${LIBCURL_BINARY_DIR}/tmp
        STAMP_DIR ${LIBCURL_BINARY_DIR}/stamp
        BINARY_DIR ${LIBCURL_BINARY_DIR}/curl
        CONFIGURE_COMMAND
            cd <BINARY_DIR> && ${CMAKE_COMMAND} <SOURCE_DIR>
                ${LIBCURL_CMAKE_FLAGS}
        BUILD_COMMAND cd <BINARY_DIR> && $(MAKE)
        INSTALL_COMMAND cd <BINARY_DIR> && $(MAKE) install)

    add_library(bundled-libcurl STATIC IMPORTED GLOBAL)
    set_target_properties(bundled-libcurl PROPERTIES IMPORTED_LOCATION
        ${LIBCURL_INSTALL_DIR}/lib/libcurl.a)
    if (BUNDLED_LIBCURL_USE_ARES)
        # Need to build ares first
        add_dependencies(bundled-libcurl-project bundled-ares)
    endif()
    add_dependencies(bundled-libcurl bundled-libcurl-project)

    # Setup CURL_INCLUDE_DIRS & CURL_LIBRARIES for global use.
    set(CURL_INCLUDE_DIRS ${LIBCURL_INSTALL_DIR}/include)
    set(CURL_LIBRARIES bundled-libcurl ${LIBZ_LIBRARY})
    if (BUNDLED_LIBCURL_USE_ARES)
        set(CURL_LIBRARIES ${CURL_LIBRARIES} ${ARES_LIBRARIES})
    endif()
    if (TARGET_OS_LINUX OR TARGET_OS_FREEBSD)
        set(CURL_LIBRARIES ${CURL_LIBRARIES} rt)
    endif()

    unset(FOUND_ZLIB_ROOT_DIR)
    unset(FOUND_OPENSSL_ROOT_DIR)
    unset(LIBCURL_INSTALL_DIR)
    unset(LIBCURL_BINARY_DIR)
    unset(LIBCURL_SOURCE_DIR)
endmacro(curl_build)
