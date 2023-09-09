## feature/config

* On read-only instances, Tarantool now synchronizes credentials
  in the background when switching to read-write mode instead of
  skipping the synchronization (gh-8967).
