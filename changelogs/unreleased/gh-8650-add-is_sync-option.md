## feature/box

* Added a new `is_sync` parameter to `box.atomic()`. To make the transaction
synchronous, set the `is_sync` option to `true`. Setting `is_sync = false` is
prohibited. If any value other than true/nil is set, for example
`is_sync = "some string"`, then an error will be thrown (gh-8650).
