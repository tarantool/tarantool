## bugfix/core

* Fiber IDs were switched to monotonically increasing unsigned 8-byte
  integers so that there wouldn't be IDs wrapping anymore. This allows
  to detect fiber's precedence by their IDs if needed (gh-5846).
