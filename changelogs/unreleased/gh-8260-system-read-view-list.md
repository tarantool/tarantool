## feature/core

* Added the new function `box.read_view.list()` that returns a list of all
  active database read views. The list includes both system read views (created
  to make a checkpoint or join a replica) and read views created by application
  code (available only in Enterprise Edition) (gh-8260).
