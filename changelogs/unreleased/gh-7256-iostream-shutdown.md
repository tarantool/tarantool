## bugfix/core

* Fixed a bug because of which a net.box connection was not properly terminated
  when the process had a child (for example, started with `popen`) sharing the
  connection socket fd. The bug could lead to a server hanging on exit while
  executing the graceful shutdown protocol (gh-7256).
