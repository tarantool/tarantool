## feature/connpool

* `experimental.connpool` methods `call()` and `filter()` became faster. They do
  not wait for unavailable instances if it is known they have been inaccessible
  recently.
