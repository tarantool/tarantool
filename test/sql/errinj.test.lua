remote = require('net.box')
errinj = box.error.injection
fiber = require('fiber')

box.sql.execute('create table test (id primary key, a float, b text)')
box.schema.user.grant('guest','read,write,execute', 'universe')
cn = remote.connect(box.cfg.listen)
cn:ping()

-- gh-2601 iproto messages are corrupted
errinj = box.error.injection
fiber = require('fiber')
errinj.set("ERRINJ_WAL_DELAY", true)
insert_res = nil
select_res = nil
function execute_yield() insert_res = cn:execute("insert into test values (100, 1, '1')") end
function execute_notyield() select_res = cn:execute('select 1') end
f1 = fiber.create(execute_yield)
while f1:status() ~= 'suspended' do fiber.sleep(0) end
f2 = fiber.create(execute_notyield)
while f2:status() ~= 'dead' do fiber.sleep(0) end
errinj.set("ERRINJ_WAL_DELAY", false)
while f1:status() ~= 'dead' do fiber.sleep(0) end
insert_res
select_res

cn:close()
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
box.sql.execute('drop table test')
