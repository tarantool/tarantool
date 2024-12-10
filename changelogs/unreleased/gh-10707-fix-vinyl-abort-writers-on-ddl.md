## bugfix/vinyl

* Fixed a use-after-free bug in the transaction manager that could be triggered
  by a race between DDL and DML operations affecting the same space (gh-10707).
