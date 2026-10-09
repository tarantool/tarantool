## feature/core

* Moved the Lua module used for interaction with application threads out of
  the experimental namespace: it can now be required as `threads`. For
  backward compatibility, the old module name `experimental.threads` still
  works (gh-12207).
