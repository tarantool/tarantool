## bugfix/core
* Fixed a bug when it was impossible to bind multiple interfaces with a single
  port number (gh-7152).

## feature/core
* Now the server binds all URIs matching the `listen` parameter in the box
  configuration. Previously it bound only a single URI per entry. Now providing
  a single port number makes all interfaces be listened on that port.
