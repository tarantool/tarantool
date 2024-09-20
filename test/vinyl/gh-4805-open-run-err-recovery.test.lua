fiber = require('fiber')
test_run = require('test_run').new()

test_run:cmd('create server err_recovery with script = "vinyl/errinj_recovery.lua"')
test_run:cmd('start server err_recovery')
test_run:cmd('switch err_recovery')

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {run_count_per_level = 100, page_size = 128, range_size = 1024})

digest = require('digest')
test_run:cmd("setopt delimiter ';'")
function dump(big)
    local step = big and 1 or 5
    for i = 1, 20, step do
        s:replace{i, digest.urandom(1000)}
    end
    box.snapshot()
end;
test_run:cmd("setopt delimiter ''");

dump(true)
dump()
dump()

test_run:cmd('switch default')
test_run:cmd('stop server err_recovery')
test_run:cmd('start server err_recovery with crash_expected=True')

opts = {}
opts.filename = 'errinj_recovery.log'
test_run:grep_log('err_recovery', "Error injection 'vinyl run recover'", 1000, opts) ~= nil

test_run:cmd('delete server err_recovery')
