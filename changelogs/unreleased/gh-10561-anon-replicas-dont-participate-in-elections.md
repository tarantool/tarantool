## bugfix/replication

* Fixed a bug when anonymous replicas could participate in elections or even
  be chosen as a leader. It is now forbidden to configure a replica so
  that `replication_anon` is `true` and `election_mode` is not `off`
  (gh-10561).
