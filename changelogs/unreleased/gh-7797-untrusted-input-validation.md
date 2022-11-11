## bugfix/core

* Added boundary checking for `getenv()` return values. Also, for security
  reasons, Tarantool code now copies these values instead of using them directly (gh-7797).
* `os.getenv()` now always returns values of sane size (gh-7797).
