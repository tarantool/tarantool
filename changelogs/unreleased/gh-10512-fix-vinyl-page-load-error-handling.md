## bugfix/vinyl

* Fixed a bug when `index.select()` could silently skip a tuple if it failed to
  load a row from a run file (gh-10512).
