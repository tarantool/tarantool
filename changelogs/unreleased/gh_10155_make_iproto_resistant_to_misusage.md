## bugfix/core

* Fixed a bug when a server could crash if a client sent an IPROTO replication
  request without waiting for pending requests to complete (gh-10155).
