## bugfix/datetime

* Fixed timestamp type checking in `set()` (gh-12411).

For backward compatibility the `compat.datetime_setfn_timestamp_type_check`
option is introduced. It's disabled by default for now ('old' behaviour),
which means no check. The 'new' behaviour (check) is planned to set
as default on 4.x.
