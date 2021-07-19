## bugfix/core

* Added missing broadcast to net.box.future:discard() so that now fibers
  waiting for a request result are woken up when the request is discarded
  (gh-6250).
