## bugfix/replication

* Fixed a possible split-brain when the old synchro queue owner might finalize the
  transactions in the presence of the new owner (gh-5295).

* Improved the detection of possible split-brain situations, for example, when
  multiple leaders were working independently due to manually lowered quorum.
  Once a node discovers that it received some foreign data, it immediately
  stops replication from such a node with an ER_SPLIT_BRAIN error (gh-5295).
