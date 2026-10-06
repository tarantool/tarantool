## bugfix/build

* Updated libcurl to version 8.23.0-114 (gh-13360).

  Note for those who build Tarantool from source: this libcurl version supports
  OpenSSL 4, but it *requires* OpenSSL 3 or later. Use
  `-DENABLE_BUNDLED_OPENSSL=ON` or `-DENABLE_BUNDLED_LIBCURL=OFF` if needed.
