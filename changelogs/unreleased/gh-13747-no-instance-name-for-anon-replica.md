## bugfix/replication

* Setting `instance_name` for an anonymous replica now returns an error
  instead of leaving it in an inconsistent state that could crash the
  instance on deanonymization (gh-13747).
