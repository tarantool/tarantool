## feature/core

- Disabled DDL operations with an old system schema. Now you have to run
  `box.schema.upgrade()` before you can execute any DDL operations (gh-7149).
