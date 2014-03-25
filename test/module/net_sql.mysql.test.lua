package.path  = os.getenv("TARANTOOL_SRC_DIR").."/src/module/sql/?.lua"
package.cpath  = "?.so"

require("sql")
if type(box.net.sql) ~= "table" then error("net.sql load failed") end

os.execute("mkdir -p box/net/")
os.execute("cp ../../src/module/mysql/mysql.so box/net/")

require("box.net.mysql")
--# setopt delimiter ';'
do
    stat, err = pcall(box.net.sql.connect, 'abcd')
    err, _ = err:gsub('.*/src/module/sql/sql.lua', 'error: src/module/sql/sql.lua')
    return err == 'error: src/module/sql/sql.lua:29: Unknown driver \'abcd\''
end;
do
    stat, err = pcall(box.net.sql.connect, 'mysql')
    err, _ = err:gsub('.*/src/module/sql/sql.lua', 'error: src/module/sql/sql.lua')
    return err == 'error: src/module/sql/sql.lua:64: Usage: box.net.sql.connect(\'mysql\', host, port, user, password, db, ...)'
end;
--# setopt delimiter ''
function dump(v) return box.cjson.encode(v) end

connect = {}
for tk in string.gmatch(os.getenv('MYSQL'), '[^:]+') do table.insert(connect, tk) end

-- mysql
c = box.net.sql.connect('mysql', unpack(connect))
for k, v in pairs(c) do print(k, ': ', type(v)) end

--# setopt delimiter ';'
do
    stat, err = pcall(c.execute, c, 'SEL ECT 1')
    err, _ = err:gsub('.*/src/module/sql/sql.lua', 'error: src/module/sql/sql.lua')
    return err == 'error: src/module/sql/sql.lua:105: You have an error in your SQL syntax; check the manual that corresponds to your MySQL server version for the right syntax to use near \'SEL ECT 1\' at line 1'
end;
--# setopt delimiter ''
dump({c:execute('SELECT ? AS bool1, ? AS bool2, ? AS nil, ? AS num, ? AS str', true, false, nil, 123, 'abc')})

dump({c:execute('SELECT * FROM (SELECT ?) t WHERE 1 = 0', 2)})
dump({c:execute('CREATE PROCEDURE p1() BEGIN SELECT 1 AS One; SELECT 2 AS Two, 3 AS Three; END')})
dump({c:execute('CALL p1')})
dump({c:execute('DROP PROCEDURE p1')})
dump({c:execute('SELECT 1 AS one UNION ALL SELECT 2')})
dump({c:execute('SELECT 1 AS one UNION ALL SELECT 2; SELECT ? AS two', 'abc')})

c:quote('test \"abc\" test')

c:begin_work()
c:rollback()
c:begin_work()
c:commit()

os.execute("rm -rf box/net/")
