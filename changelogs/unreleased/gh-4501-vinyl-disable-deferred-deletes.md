## feature/vinyl

* Disabled the deferred DELETE optimization in Vinyl to avoid possible
  performance degradation of secondary index reads. Now, to enable the
  optimization, one has to set the `defer_deletes` flag in space options
  (gh-4501).
