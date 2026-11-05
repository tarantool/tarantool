## bugfix/replication

* Fixed a hang on shutdown of a node whose promotion was waiting for a
  quorum, for example, a new leader waiting for the previous leader's
  unconfirmed transactions (gh-13625).
