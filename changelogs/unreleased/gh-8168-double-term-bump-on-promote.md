## bugfix/raft

* Fixed nodes configured with `election_mode` = "manual" sometimes bumping the
  election term excessively after their promotion (gh-8168).
