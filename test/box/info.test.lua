-- Test Lua from admin console. Whenever producing output,
-- make sure it's a valid YAML
box.info.unknown_variable
box.info[23]
box.info['unknown_variable']
string.match(box.info.version, '^[1-9]') ~= nil
string.match(box.info.pid, '^[1-9][0-9]*$') ~= nil
string.match(box.info.logger_pid, '^[1-9][0-9]*$') ~= nil
box.info.lsn > 0
box.info.recovery_lag
box.info.recovery_last_update
box.info.status
string.len(box.info.config) > 0
string.len(box.info.build.target) > 0
string.len(box.info.build.compiler) > 0
string.len(box.info.build.flags) > 0
string.len(box.info.build.options) > 0
string.len(box.info.uptime) > 0
string.match(box.info.uptime, '^[1-9][0-9]*$') ~= nil
t = {}
for k, _ in pairs(box.info()) do table.insert(t, k) end
table.sort(t)
t
box.info.snapshot_pid
-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
