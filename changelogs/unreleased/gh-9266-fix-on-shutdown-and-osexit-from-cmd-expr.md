## bugfix/box

* Fixed a bug when `on_shutdown` triggers weren't run if `os.exit()` was
  called from `-e` command-line option (gh-9266).
