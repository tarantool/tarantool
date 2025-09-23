## bugfix/box

* Fixed an issue where the predefined `replication` role was not treated
  as a system role and therefore could be dropped or modified. Now it is
  properly protected like other system roles (`guest`, `admin`, `public`,
  `super`) (gh-11848).
