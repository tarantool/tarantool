-- wal is off, good opportunity to test something more CPU intensive:
env = require('test_run')
test_run = env.new()
spaces = {}
box.schema.FORMAT_ID_MAX
test_run:cmd("setopt delimiter ';'")
-- too many formats
for k = 1, box.schema.FORMAT_ID_MAX, 1 do
    local s = box.schema.space.create('space'..k)
    table.insert(spaces, s)
end;
#spaces;
-- cleanup
for k, v in pairs(spaces) do
    v:drop()
end;
test_run:cmd("setopt delimiter ''");
