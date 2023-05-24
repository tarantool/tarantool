## bugfix/replication

* Fixed nodes configured with `election_mode = 'candidate'` spuriously detecting
  a split-vote when another candidate should win with exactly a quorum of votes
  for it (gh-8698).
