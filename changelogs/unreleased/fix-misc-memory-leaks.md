## bugfix/box

- Fixed memory leak on disconnectiong from replica.
- Fixed memory leak on user ddl on access check failure.
- Fixed memory leak on xlog open failure.
- Fixed memory leak on space ddl on certain condition.
- Fixed memory leak on iproto override usage (since 3.1).
- Fixed memory leak on ddl failed due to foreign key constraint check.
- Fixed memory leak on space drop with sequence bound to json path.

## bugfix/core

- Fixed memory leak on connecting to replica or connecting via netbox.
  and failure to connect on first address obtained thru resolver.
