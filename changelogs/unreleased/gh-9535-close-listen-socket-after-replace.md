## bugfix/config

* Fixed a bug that causes Tarantool to continue listening on the previous socket
  even after `console.socket` has changed (gh-9535).
