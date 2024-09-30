## feature/box

* Added support for asynchronous wait modes (`box.commit{wait = ...}`) to
  synchronous transactions. Changes committed this way can be observed with the
  `read-committed` isolation level (gh-10583).
