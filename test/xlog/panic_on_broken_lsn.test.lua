-- Issue 3105: Test logging of request with broken lsn before panicking
env = require('test_run')
test_run = env.new()
test_run:cleanup_cluster()

fio = require('fio')

-- Testing case of panic on recovery
test_run:cmd('create server panic with script="xlog/panic.lua"')
test_run:cmd('start server panic')
test_run:switch('panic')

box.space._schema:replace{"t0", "v0"}
lsn = box.info.vclock[1]
box.error.injection.set("ERRINJ_WAL_BREAK_LSN", lsn + 1)
box.space._schema:replace{"t0", "v1"}
box.error.injection.set("ERRINJ_WAL_BREAK_LSN", -1)

test_run:switch('default')
test_run:cmd('stop server panic')

dirname = fio.pathjoin(fio.cwd(), "panic")
xlogs = fio.glob(dirname .. "/*.xlog")
fio.unlink(xlogs[#xlogs])

test_run:cmd('start server panic with crash_expected=True')

-- Check that log contains the mention of broken LSN and the request printout
filename = fio.pathjoin(fio.cwd(), 'panic.log')
str = string.format("LSN for 1 is used twice or COMMIT order is broken: confirmed: 1, new: 1, req: .*")
found = test_run:grep_log(nil, str, 256, {filename = filename})
(found:gsub('^.*, req: ', ''):gsub('lsn: %d+', 'lsn: <lsn>'))

test_run:cmd('cleanup server panic')
test_run:cmd('delete server panic')
