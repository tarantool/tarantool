## bugfix/sql

* Fixed a bug when an incorrect query result could be returned if tables
  participated in a join and their names met certain conditions. The bug was
  added in version `3.0.0-beta1` in issue gh-4467 (gh-9445).
