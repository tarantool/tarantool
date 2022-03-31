## bugfix/core

* Banned DDL operations in space `on_replace` triggers, since they could lead
  to a crash (gh-6920).
