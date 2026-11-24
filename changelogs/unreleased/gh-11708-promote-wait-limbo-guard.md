## bugfix/replication

* Fixed concurrent calls to `box.ctl.promote()` and `box.ctl.demote()` sometimes
  being accepted while an earlier promotion was still running. These calls
  could interrupt the earlier promotion and cause it to fail. They are now
  rejected until the earlier call finishes (gh-11708).
