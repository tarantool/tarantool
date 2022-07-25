## feature/lua/http client

* New method "response:decode()" has been introduced. It allows to decode body
  to a Lua object when decoding function is available. Decode function depends
  on content type and following content types are supported by default:
  "application/json", "application/yaml" and "application/msgpack". Now user can
  define his own decode function (gh-6833).
