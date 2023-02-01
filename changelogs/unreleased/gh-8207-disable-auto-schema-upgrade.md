## feature/core

* Disabled automatic invocation of `box.schema.upgrade` on `box.cfg` for
  read-write instances that don't set up replication. Now, `box.schema.upgrade`
  may only be called manually by the admin (gh-8207).
