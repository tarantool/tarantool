netbox = require('net.box')
test_run = require('test_run').new()

box.execute("CREATE TABLE t (id INT PRIMARY KEY AUTOINCREMENT, a INT NOT NULL, c TEXT COLLATE \"unicode_ci\");")
box.execute("INSERT INTO t VALUES (1, 1, 'aSd');")

remote = test_run:get_cfg('remote') == 'true'
execute = nil
test_run:cmd("setopt delimiter ';'")
if remote then
    box.schema.user.grant('guest','read, write, execute', 'universe')
    box.schema.user.grant('guest', 'create', 'space')
    cn = netbox.connect(box.cfg.listen)
    execute = function(...) return cn:execute(...) end
else
    execute = function(...)
        local res, err = box.execute(...)
        if err ~= nil then
            error(err)
        end
        return res
    end
end;
test_run:cmd("setopt delimiter ''");

execute("PRAGMA full_metadata = true;")
-- Make sure collation is presented in extended metadata.
--
execute("SELECT 'aSd' COLLATE \"unicode_ci\";")
execute("SELECT c FROM t;")
execute("SELECT c COLLATE \"unicode\" FROM t;")

execute("PRAGMA full_metadata = false;")

test_run:cmd("setopt delimiter ';'")
if remote then
    cn:close()
    box.schema.user.revoke('guest', 'read, write, execute', 'universe')
    box.schema.user.revoke('guest', 'create', 'space')
end;
test_run:cmd("setopt delimiter ''");

box.space.T:drop()
