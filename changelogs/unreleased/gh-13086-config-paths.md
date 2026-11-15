## bugfix/config

* Fixed initial snapshot discovery and context-file reading using the wrong
  folder when a configured path contains a symlink followed by `..`
  (gh-13086).
* Fixed configuration creating directories in the wrong folder when their
  paths contain a symlink followed by `..` (gh-13086).
