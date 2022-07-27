## feature/lua/uri

* Add method `uri.values()` that allows a user to represent multivalue
  parameter's (gh-6832).

```
> uri.parse({"/tmp/unix.sock", params = params)
---
- host: unix/
  service: /tmp/unix.sock
  unix: /tmp/unix.sock
  params:
    q1:
    - v1
    - v2
...

```
