session = box.session
session.su('admin')
env = require('test_run')
test_run = env.new()
--
-- Check max function limit
--
test_run:cmd("setopt delimiter ';'")
function func_limit()
    local i = 1
    while true do
        box.schema.func.create('func'..i)
        i = i + 1
    end
    return i
end;
function drop_limit_func()
    local i = 1
    while true do
        box.schema.func.drop('func'..i)
        i = i + 1
    end
end;
test_run:cmd("setopt delimiter ''");
func_limit()
drop_limit_func()
box.schema.user.create('testuser')
box.schema.user.grant('testuser', 'read,write', 'space', '_func')
box.schema.user.grant('testuser', 'create', 'function')
session.su('testuser')
func_limit()
drop_limit_func()
session.su('admin')
box.schema.user.drop('testuser')
