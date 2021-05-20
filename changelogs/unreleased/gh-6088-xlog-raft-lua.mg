## bugfix/core
 * Added decoding of election messages: `RAFT` and `PROMOTE` to
   `xlog` Lua module (gh-6088). Otherwise `tarantoolctl` shows
   plain number in `type`
   ```
   HEADER:
     lsn: 1
     replica_id: 4
     type: 31
     timestamp: 1621541912.4592
   ```
   instead of symbolic representation
   ```
   HEADER:
     lsn: 1
     replica_id: 4
     type: PROMOTE
     timestamp: 1621541912.4592
   ```
