## bugfix/replication

* Fixed a possible split-brain when old synchro queue owner might finalize the
  transactions in presence of a new synchro queue owner (gh-5295).

* Fixed servers not noticing possible split-brain situations, for example when
  multiple leaders were working independently due to manually lowered quorum.
  Once a node discovers that it received some foreign data, it immediately
  stops replication from such a node with ER_SPLIT_BRAIN error (gh-5295).
