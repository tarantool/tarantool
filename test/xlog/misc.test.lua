test_run = require('test_run').new()
test_run:cmd('restart server default with cleanup=1')

fio = require('fio')
xlog = require('xlog')
netbox = require('net.box')

box.schema.user.grant('guest', 'read,write,execute', 'universe')

--
-- Check that xlogs doesn't contain IPROTO_SYNC
--

conn = netbox.connect(box.cfg.listen)
-- insert some row using the binary protocol
conn.space._schema:insert({'test'})
-- rotate xlog
box.snapshot()
-- dump xlog
xlog_path = fio.pathjoin(box.cfg.wal_dir, string.format("%020d.xlog", 0))
result = {}
fun, param, state = xlog.pairs(xlog_path)
-- skip grants until our insert into _schema
repeat state, row = fun(param, state) until row.BODY.space_id == box.space._schema.id
row.HEADER.type
row.HEADER.sync
row.BODY
box.space._schema:delete('test')

--
-- Clean up
--
box.schema.user.revoke('guest', 'read,write,execute', 'universe')
netbox = nil
xlog = nil
fio = nil
