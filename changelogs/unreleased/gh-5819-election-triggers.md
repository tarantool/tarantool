## feature/replication

* Introduce on_election triggers. The triggers may be registered via
`box.ctl.on_election()` interface and are run asynchronously each time
`box.info.election` changes (gh-5819).
