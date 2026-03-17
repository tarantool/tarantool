## bugfix/datetime

* Fixed timestamp type checking in `set()` (gh-12411).

For backward compatibility the `compat.datetime_setfn_timestamp_type_check`
option has been introduced. It's disabled by default for now ('old' behaviour),
which means no type check is performed. The 'new' behaviour (with type check)
is planned to be set as the default in version 4.x.
