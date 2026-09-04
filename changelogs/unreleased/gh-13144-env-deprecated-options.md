## bugfix/core

* Deprecated `box.cfg` options set via `TT_*` environment variables are now
  translated to their replacement options, and the translation no longer
  silently overrides an explicitly provided value of a replacement option
  (gh-13144).
