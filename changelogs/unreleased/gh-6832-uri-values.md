## feature/lua/uri

* Added a new method `uri.values()` for representing multivalue parameters (gh-6832).

```
> params = {q1 = uri.values("v1", "v2")}}
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
