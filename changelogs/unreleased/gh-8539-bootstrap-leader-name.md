## feature/replication

* Added the ability to set the `bootstrap_leader` configuration option to the
  instance name of the desired bootstrap leader:
  ```lua
    box.cfg{
        bootstrap_strategy = 'config',
        bootstrap_leader = 'leader-name',
        replication = {
            ...
        },
        ...
    }
  ```
  (gh-7999, gh-8539).
