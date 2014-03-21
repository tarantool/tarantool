--# push filter 'error: .*/src/module/sql/sql.lua' to 'error: src/module/sql/sql.lua'
package.path  = os.getenv("TARANTOOL_SRC_DIR").."/src/module/sql/?.lua"
package.cpath  = "?.so"

require("sql")
if type(box.net.sql) ~= "table" then error("net.sql load failed") end

os.execute("mkdir -p box/net/")
os.execute("cp ../../src/module/pg/pg.so box/net/")

require("box.net.pg")

c = box.net.sql.connect('abcd')
dump = function(v) return box.cjson.encode(v) end

connect = {}
for tk in string.gmatch(os.getenv('PG'), '[^:]+') do table.insert(connect, tk) end

-- postgresql
c = box.net.sql.connect('pg', unpack(connect))
dump({c:execute('SELECT 123::text AS bla, 345')})
dump({c:execute('SELECT -1 AS neg, NULL AS abc')})
dump({c:execute('SELECT -1.1 AS neg, 1.2 AS pos')})
dump({c:execute('SELECT ARRAY[1,2] AS neg, 1.2 AS pos')})
dump({c:execute('SELECT ? AS val', 'abc')})
dump({c:execute('SELECT ? AS val', 123)})
dump({c:execute('SELECT ? AS val', true)})
dump({c:execute('SELECT ? AS val', false)})
dump({c:execute('SELECT ? AS val, ? AS num, ? AS str', false, 123, 'abc')})
dump({c:execute('DROP TABLE IF EXISTS unknown_table')})
dump({c:execute('SELECT * FROM (VALUES (1,2), (2,3)) t')})
c:ping()
dump({c:select('SELECT * FROM (VALUES (1,2), (2,3)) t')})
dump({c:single('SELECT * FROM (VALUES (1,2), (2,3)) t')})
dump({c:single('SELECT * FROM (VALUES (1,2)) t')})
dump({c:perform('SELECT * FROM (VALUES (1,2), (2,3)) t')})
c:execute('SELEC T')

c = box.net.sql.connect('abcd')

c:quote('abc\"cde\"def')

c:begin_work()
c:rollback()
c:begin_work()
c:commit()

c:txn(function(dbi) dbi:single('SELECT 1') end)

os.execute("rm -rf box/net/")
--# clear filter
