## bugfix/box

* Users and roles defined in `credentials.*` are now synchronized with the
  config on reload and instance restart: users and roles removed from config
  are dropped automatically, while manually created users and roles remain
  untouched (gh-11827).

----

Notable change: users/roles are now synced with config.
After upgrading, administrators are recommended to check for orphan users and
roles that were created in previous Tarantool versions (before origins were
tracked). Such objects are not removed automatically even if they are missing
from the configuration.

A helper script that lists all non-config users/roles and generates drop
commands is provided in the Tarantool wiki:

<https://github.com/tarantool/tarantool/wiki/?>
