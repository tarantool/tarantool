## feature/replication

* You may now specify which node a replica should bootstrap from. In order to do
  so, set `box.cfg.bootstrap_strategy` to `'config'` and set the new
  `bootstrap_leader` configuration option to either the URI or UUID of the
  desired bootstrap leader. For example:
  ```lua
    box.cfg{
        bootstrap_strategy = 'config',
        bootstrap_leader = 'localhost:3301',
        replication = {
            'localhost:3301',
            'localhost:3302',
        },
        listen = '3302',
    }
  ```
  (gh-7999).
