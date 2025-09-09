## bugfix/box

* Users and roles defined in `credentials.*` are now synchronized with the
  config on reload and instance restart: users and roles removed from config
  are dropped automatically, while manually created users and roles remain
  untouched (gh-11827).

----

Manual action required: ensure that all users and roles are managed by the
YAML configuration.

A customer has to run the following script [1] to ensure that all the users
and roles are managed solely by the YAML configuration.

It allows to identify users/roles that are kept forever, because created from
Lua (or from config on tarantool version less than 3.6.0), and decide whether
to transfer the ownership to the YAML configuration or finally delete them.

[1]: https://raw.githubusercontent.com/tarantool/tarantool/refs/heads/master/tools/find-orphan-users.lua'
