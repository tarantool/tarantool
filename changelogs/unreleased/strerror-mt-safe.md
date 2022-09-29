## bugfix/core

* Switched from MT-Unsafe `strerror()` to MT-Safe `strerror_r()`. The usage
  of the unsafe function could result in corrupted error messages.
