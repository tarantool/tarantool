session = box.session
session.su('admin')
--
-- Check max function limit
--
--# setopt delimiter ';'
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
func_limit();
drop_limit_func();
box.schema.user.create('testuser');
box.schema.user.grant('testuser', 'read, write, execute', 'universe');
session.su('testuser');
func_limit();
drop_limit_func();
session.su('admin')
box.schema.user.drop('testuser');
--# setopt delimiter ''
