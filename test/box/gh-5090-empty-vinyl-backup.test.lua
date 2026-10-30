test_run = require('test_run').new()

test_run:cmd('create server vinyl with script="box/lua/simple_instance.lua"')
test_run:cmd('start server vinyl')
test_run:cmd('switch vinyl')

s = box.schema.space.create('test', {engine = 'vinyl'})
index = s:create_index('primary', {parts={1, 'unsigned'}})

box.snapshot()
backup_files = box.backup.start()

test_run:cmd('switch default')

backup_files = test_run:eval('vinyl', 'backup_files')[1]
for _, file in pairs(backup_files) do os.execute('cp ' .. file .. ' .') end

test_run:drop_cluster({'vinyl'})

test_run:cmd("create server vinyl with script='box/lua/simple_instance.lua'")
for _, file in pairs(backup_files) do os.execute('mv ' .. file:match('.*/(.*)') .. ' simple_instance/') end
test_run:cmd('start server vinyl')
test_run:cmd('switch vinyl')

box.space.test:insert{1}
box.snapshot()

-- Cleanup.
test_run:cmd('switch default')
test_run:drop_cluster({'vinyl'})
