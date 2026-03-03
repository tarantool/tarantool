## bugfix/election

* Fixed a bug where a newly elected leader could become writable (`box.info.ro`
  is `false`) immediately after `box.info.election.state == 'leader'` but before
  it claimed the synchronous transactions queue (`box.info.synchro.queue.owner`
  is not this node). This could occur only when the synchronous queue was
  previously unclaimed, which typically happens on cluster startup (gh-12333).
