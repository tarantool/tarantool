## bugfix/vinyl

* Fixed a bug when a WAL write error could lead to the violation of a unique
  constraint in a space with the enabled `defer_deletes` option (gh-11969).
