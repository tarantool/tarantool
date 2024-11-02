## feature/net-box

- Add a new `netbox:connect()` option `no_activity_timeout`. The
  parameter can be used to close the connection after an inactivity
  period. The connection deadline can now be additionally bumped using a
  new `connection:bump_activity()` method (gh-10352).
