## bugfix/box

* Fixed bug when WAL queue is no flushed properly. In particular
 when building index of vinyl space. In the latter case it may lead
 the new index missing data from transactions in the queue (gh-11118, gh-11119).
