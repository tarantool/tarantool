## bugfix/lua

* Fixed closing a Unix listener removing the wrong socket file after the
  current directory or a parent symlink changes (gh-13086).

## bugfix/config

* Fixed console reload comparing socket paths incorrectly when they contain
  symlinks. Different paths to the same location no longer reopen the
  listener, and changing a parent symlink moves it to the new location
  (gh-13086).
