## feature/core

* Allow DDL before calling `box.schema.upgrade` since schema version 2.11.1.
  Creating persistent trigger is forbidden until schema version 3.1.0.
  Using persistent names is allowed since schema version 2.11.5 (gh-10520).
