## bugfix/replication

* Fixed an assertion failure on master when replica resubscribes with a
  smaller vclock than previously seen (gh-5158).
