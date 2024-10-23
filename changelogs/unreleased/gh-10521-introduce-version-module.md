## feature/core

* Introduced `version` Lua module, which supports creating and
  comparing schema version (aka `dd_version` in `box.status`, aka
  `box.space._schema:get('version')`). Intended to ease the use
  of blocked features before `box.schema.upgrade` (gh-10521).
