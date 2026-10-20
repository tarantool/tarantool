## bugfix/memtx

* Fixed a crash after a failed replace in a space with a functional index
  whose function raised an error for the replaced tuple (gh-13481).
