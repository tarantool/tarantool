## feature/config

* Introduced a `early_load: true` tag for roles and scripts. When set, it makes
  Tarantool load the role or execute the script before `box.cfg` (gh-10182).
