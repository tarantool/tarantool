## bugfix/vinyl

* Fixed a bug in the tuple cache when a transaction operating in a read view
  could skip a tuple deleted after the read view creation (gh-11079, gh-11294).
