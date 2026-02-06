## bugfix/replication

* Fixed a bug where the replication could get stuck upon receipt
  of a 'promote' entry from a newly elected leader. This was
  likely to happen when the receiver itself was also a leader not
  long ago and still had some transactions in the synchro queue
  not yet written to the journal (gh-12205).
