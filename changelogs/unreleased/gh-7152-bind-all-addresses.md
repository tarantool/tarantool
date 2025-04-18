## bugfix/core
* Fixed a bug when it was impossible to bind multiple interfaces with a single
  port number (gh-7152).

## feature/core
* Now the server binds all URIs matching the `listen` parameter in the box
  configuration while it used to bind only a single URI per entry. So providing
  a single port number results in all interfaces being listened on that port.
