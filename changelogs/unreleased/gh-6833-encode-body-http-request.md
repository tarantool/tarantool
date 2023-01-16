## feature/lua/http client

* Now the HTTP client is able to encode Lua objects automatically when it is
  possible. The encoding format depends on the request content type.
  The following content types are supported by default: `application/json`,
  `application/yaml`, `application/msgpack`. Users can define encoding rules
  for other content types by writing their own encoding functions (gh-6833).
