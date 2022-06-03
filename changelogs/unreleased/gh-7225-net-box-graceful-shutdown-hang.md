## bugfix/core

* Fixed a bug in the net.box connector because of which a client could fail to
  close its connection on receiving a shutdown request from the server, which
  would lead to the server hanging at exit (gh-7225).
