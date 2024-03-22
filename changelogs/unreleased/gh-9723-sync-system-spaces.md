## feature/replication

* Made all of the system spaces synchronous by default when the synchronous
  queue is claimed, i.e., `box.info.synchro.queue.owner ~= 0`. Added a
  `box_consider_system_spaces_synchronous` backward compatibility option to
  control this behavior. Added a new read-only `state` subtable for the Lua
  space object returned from the `box.space` registry. This subtable has an
  `is_sync` field that reflects the effective state of replication for system
  spaces. For user spaces, this field will always mirror the `is_sync` option
  set for the space (gh-9723).
