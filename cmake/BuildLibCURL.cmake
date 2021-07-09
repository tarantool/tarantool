# A macro to build the bundled libcurl
macro(curl_build)
    set(LIBCURL_SOURCE_DIR ${PROJECT_SOURCE_DIR}/third_party/curl)
    set(LIBCURL_BINARY_DIR ${PROJECT_BINARY_DIR}/build/curl/work)
    set(LIBCURL_INSTALL_DIR ${PROJECT_BINARY_DIR}/build/curl/dest)

    if (BUILD_STATIC)
        set(LIBZ_LIB_NAME libz.a)
    else()
        set(LIBZ_LIB_NAME z)
    endif()
    find_library(LIBZ_LIBRARY NAMES ${LIBZ_LIB_NAME})
    if ("${LIBZ_LIBRARY}" STREQUAL "LIBZ_LIBRARY-NOTFOUND")
        message(FATAL_ERROR "Unable to find zlib")
    endif()

    # Use the same OpenSSL library for libcurl as is used for
    # tarantool itself.
    get_filename_component(FOUND_OPENSSL_ROOT_DIR ${OPENSSL_INCLUDE_DIR} DIRECTORY)
    set(LIBCURL_OPENSSL_OPT "--with-ssl=${FOUND_OPENSSL_ROOT_DIR}")

    # Use either c-ares bundled with tarantool or
    # libcurl-default threaded resolver.
    if(BUNDLED_LIBCURL_USE_ARES)
        set(ASYN_DNS_USED "ares")
        set(ASYN_DNS_UNUSED "threaded-resolver")
        set(ASYN_DNS_PATH "=${ARES_INSTALL_DIR}")
    else()
        set(ASYN_DNS_USED "threaded-resolver")
        set(ASYN_DNS_UNUSED "ares")
        set(ASYN_DNS_PATH "")
    endif()

    set(ENABLED_DNS_OPT "--enable-${ASYN_DNS_USED}${ASYN_DNS_PATH}")
    set(DISABLED_DNS_OPT "--disable-${ASYN_DSN_UNUSED}")

    # Pass -isysroot=<SDK_PATH> option on Mac OS to a preprocessor
    # and a C compiler to find header files installed with an SDK.
    #
    # The idea here is to don't pass all compiler/linker options
    # that is set for tarantool, but only a subset that is
    # necessary for choosen toolchain, and let curl's configure
    # script set options that are appropriate for libcurl.
    set(LIBCURL_CPPFLAGS "")
    set(LIBCURL_CFLAGS "")
    if (TARGET_OS_DARWIN AND NOT "${CMAKE_OSX_SYSROOT}" STREQUAL "")
        set(LIBCURL_CPPFLAGS "${LIBCURL_CPPFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
        set(LIBCURL_CFLAGS "${LIBCURL_CFLAGS} ${CMAKE_C_SYSROOT_FLAG} ${CMAKE_OSX_SYSROOT}")
    endif()

    include(ExternalProject)
    ExternalProject_Add(
        bundled-libcurl-project
        SOURCE_DIR ${LIBCURL_SOURCE_DIR}
        PREFIX ${LIBCURL_INSTALL_DIR}
        DOWNLOAD_DIR ${LIBCURL_BINARY_DIR}
        TMP_DIR ${LIBCURL_BINARY_DIR}/tmp
        STAMP_DIR ${LIBCURL_BINARY_DIR}/stamp
        BINARY_DIR ${LIBCURL_BINARY_DIR}
        CONFIGURE_COMMAND
            cd <SOURCE_DIR> && ./buildconf &&
            cd <BINARY_DIR> && <SOURCE_DIR>/configure
                # Pass the same toolchain as is used to build
                # tarantool itself, because they can be
                # incompatible.
                CC=${CMAKE_C_COMPILER}
                LD=${CMAKE_LINKER}
                AR=${CMAKE_AR}
                RANLIB=${CMAKE_RANLIB}
                NM=${CMAKE_NM}
                STRIP=${CMAKE_STRIP}

                # Pass -isysroot=<SDK_PATH> option on Mac OS, see
                # above.
                # Note: Passing of CPPFLAGS / CFLAGS explicitly
                # discards using of corresponsing environment
                # variables.
                CPPFLAGS=${LIBCURL_CPPFLAGS}
                CFLAGS=${LIBCURL_CFLAGS}

                # Pass empty LDFLAGS to discard using of
                # corresponding environment variable.
                # It is possible that a linker flag assumes that
                # some compilation flag is set. We don't pass
                # CFLAGS from environment, so we should not do it
                # for LDFLAGS too.
                LDFLAGS=

                --prefix <INSTALL_DIR>
                --enable-static
                --enable-shared

                --with-zlib
                ${LIBCURL_OPENSSL_OPT}
                --with-ca-fallback

                --without-brotli
                --without-gnutls
                --without-mbedtls
                --without-cyassl
                --without-wolfssl
                --without-mesalink
                --without-nss
                --without-ca-bundle
                --without-ca-path
                --without-libpsl
                --without-libmetalink
                --without-librtmp
                --without-winidn
                --without-libidn2
                --without-nghttp2
                --without-ngtcp2
                --without-nghttp3
                --without-quiche
                --without-zsh-functions-dir
                --without-fish-functions-dir

                ${ENABLED_DNS_OPT}
                --enable-http
                --enable-proxy
                --enable-ipv6
                --enable-unix-sockets
                --enable-cookies
                --enable-http-auth
                --enable-mime
                --enable-dateparse
                --enable-smtp

                ${DISABLED_DNS_OPT}
                --disable-ftp
                --disable-file
                --disable-ldap
                --disable-ldaps
                --disable-rtsp
                --disable-dict
                --disable-telnet
                --disable-tftp
                --disable-pop3
                --disable-imap
                --disable-smb
                --disable-gopher
                --disable-manual
                --disable-sspi
                --disable-crypto-auth
                --disable-ntlm-wb
                --disable-tls-srp
                --disable-doh
                --disable-netrc
                --disable-progress-meter
                --disable-dnsshuffle
                --disable-alt-svc
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

    set(CURL_INCLUDE_DIRS ${LIBCURL_INSTALL_DIR}/include)
    set(CURL_LIBRARIES bundled-libcurl ${LIBZ_LIBRARY})
    if (BUNDLED_LIBCURL_USE_ARES)
        set(CURL_LIBRARIES ${CURL_LIBRARIES} ${ARES_LIBRARIES})
    endif()
    if (TARGET_OS_LINUX OR TARGET_OS_FREEBSD)
        set(CURL_LIBRARIES ${CURL_LIBRARIES} rt)
    endif()

    unset(ASYN_DNS_USED)
    unset(ASYN_DNS_UNUSED)
    unset(ASYN_DNS_PATH)
    unset(ENABLED_DNS_OPT)
    unset(DISABLED_DNS_OPT)
    unset(LIBCURL_CPPFLAGS)
    unset(LIBCURL_CFLAGS)
    unset(FOUND_OPENSSL_ROOT_DIR)
    unset(LIBCURL_INSTALL_DIR)
    unset(LIBCURL_BINARY_DIR)
    unset(LIBCURL_SOURCE_DIR)
endmacro(curl_build)
