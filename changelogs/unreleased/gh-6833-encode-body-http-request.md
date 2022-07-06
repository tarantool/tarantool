## feature/lua/http client

* HTTP client is able to encode Lua objects automatically when it is possible.
  Encoding depends on content type passed to HTTP request and following types
  are supported: "application/json", "application/yaml" and
  "application/msgpack". Now user can define his own encoding function (gh-6833).
