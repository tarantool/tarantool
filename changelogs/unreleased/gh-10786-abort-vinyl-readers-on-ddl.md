## bugfix/vinyl

* A DDL operation altering a space now aborts all transactions reading
  from the altered space. This should fix use-after-free and non-repeatable
  read issues that could occur during a DDL operation (gh-10707, gh-10786).
