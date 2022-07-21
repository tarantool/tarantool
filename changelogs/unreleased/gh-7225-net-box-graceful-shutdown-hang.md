## bugfix/core

* Fixed a bug in the net.box connector because of which a client could fail to
  close its connection when receiving a shutdown request from the server. This
  could lead to the server hanging on exit (gh-7225).
