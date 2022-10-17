## bugfix/core

* Added boundary checking for getenv() return values and started copying them
  rather than using directly (gh-7797).
* Made `os.getenv()` always return values of sane size (gh-7797).
