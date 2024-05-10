## bugfix/replication

* Fixed a bug that the `box.info.replication[...].downstream.lag` value could be
  misleading, not updating in time, frozen (gh-9748).
