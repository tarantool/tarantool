fio = require('fio')
xlog = require('xlog').pairs
env = require('test_run')
test_run = env.new()

test_run:cmd("setopt delimiter ';'")
function read_xlog(file)
    local val = {}
    for k, v in xlog(file) do
        table.insert(val, setmetatable(v, { __serialize = "map"}))
    end
    return val
end;
test_run:cmd("setopt delimiter ''");


-- gh-2798 check for journal transaction encoding
_ = box.schema.space.create('test'):create_index('pk')
-- generate a new xlog
box.snapshot()
lsn = box.info.lsn
-- autocommit transaction
box.space.test:replace({1})
-- one row transaction
box.begin() box.space.test:replace({2}) box.commit()
-- two row transaction
box.begin() for i = 3, 4 do box.space.test:replace({i}) end box.commit()
-- four row transaction
box.begin() for i = 5, 8 do box.space.test:replace({i}) end box.commit()
-- open a new xlog
box.snapshot()
-- read a previous one
lsn_str = tostring(lsn)
data = read_xlog(fio.pathjoin(box.cfg.wal_dir, string.rep('0', 20 - #lsn_str) .. tostring(lsn_str) .. '.xlog'))
-- check nothing changed for single row transactions
data[1].HEADER.tsn == nil and data[1].HEADER.commit == nil
data[2].HEADER.tsn == nil and data[2].HEADER.commit == nil
-- check two row transaction
data[3].HEADER.tsn == data[3].HEADER.lsn and data[3].HEADER.commit == nil
data[4].HEADER.tsn == data[3].HEADER.tsn and data[4].HEADER.commit == true
-- check four row transaction
data[5].HEADER.tsn == data[5].HEADER.lsn and data[5].HEADER.commit == nil
data[6].HEADER.tsn == data[5].HEADER.tsn and data[6].HEADER.commit == nil
data[7].HEADER.tsn == data[5].HEADER.tsn and data[7].HEADER.commit == nil
data[8].HEADER.tsn == data[5].HEADER.tsn and data[8].HEADER.commit == true
box.space.test:drop()
