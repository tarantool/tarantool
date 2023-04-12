## feature/replication

* Added a new `bootstrap_leader` configuration option to specify the node from
  which a replica should bootstrap. To do this, set `box.cfg.bootstrap_strategy`
  to `'config'` and set `bootstrap_leader` value to either the URI or UUID of
  the desired bootstrap leader. For example:
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
