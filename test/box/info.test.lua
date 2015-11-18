-- Test Lua from admin console. Whenever producing output,
-- make sure it's a valid YAML
box.info.unknown_variable
box.info[23]
box.info['unknown_variable']
string.match(box.info.version, '^[1-9]') ~= nil
string.match(box.info.pid, '^[1-9][0-9]*$') ~= nil
#box.info.server > 0
box.info.replication
box.info.status
string.len(box.info.uptime) > 0
string.match(box.info.uptime, '^[1-9][0-9]*$') ~= nil
t = {}
for k, _ in pairs(box.info()) do table.insert(t, k) end
table.sort(t)
t
