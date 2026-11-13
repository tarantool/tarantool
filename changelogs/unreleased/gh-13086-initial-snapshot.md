## bugfix/config

* Preserve initial snapshot lookup through a not-yet-created workdir followed
  by parent components, including when an existing symlink precedes the missing
  components (gh-13086).
