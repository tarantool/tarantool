## bugfix/box

* Fixed a null pointer dereference when an `ER_EXACT_MATCH` error occurred on an attempt to call `get()` in a `read_view` from the deleted space. (ghe-787).
