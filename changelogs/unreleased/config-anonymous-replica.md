## bugfix/config

* Effectively supported `replication.anon` option, which was broken in several
  ways before (gh-9432).

  There are caveats that are not resolved yet:

  * An anonymous replica can't be bootstrapped from a replicaset, where all the
    instance are in read-only mode, however there are no technical problems
    there (just too tight validation).
