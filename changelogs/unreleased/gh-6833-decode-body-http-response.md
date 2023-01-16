## feature/lua/http client

* Added a new method `response:decode()`. It decodes an HTTP response body to
  a Lua object. The decoding result depends on the response content type. The following
  content types are supported by default: `application/json`, `application/yaml`,
  `application/msgpack`. Users can define decoding rules for other content types
  by writing their own decoding functions. (gh-6833).
