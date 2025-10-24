## bugfix/replication

* Fixed a bug where a node configured with `election_mode = 'off'` would prevent
  nodes with `election_mode = 'candidate'` from starting new elections after the
  leader death (gh-12018).
