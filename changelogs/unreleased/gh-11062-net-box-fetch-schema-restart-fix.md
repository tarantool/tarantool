## bugfix/lua/netbox

* Fixed a bug when the request counter used by a `net.box` client to implement
  the graceful shutdown protocol could underflow while it was fetching the
  schema from the remote end. In case of a debug build, the bug would crash
  the client. In case of a release build, the bug would result in a timeout
  while executing the remote server shutdown (gh-11062).
