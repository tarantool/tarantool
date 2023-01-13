## bugfix/lua/json

* Now the forward slash (`/`) is not escaped in `json.encode()` and msgpack.
  A new `tarantool.compat` option `json_escape_forward_slash` is added for
  switching to the new behavior (gh-6200).
