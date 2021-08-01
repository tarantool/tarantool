test_run = require('test_run').new()

-- Upgrade from 1.6.8.
test_run:cmd('create server upgrade with script="xlog/upgrade.lua", \
             workdir="xlog/upgrade/1.6.8/gh-5894-pre-1.7.7-upgrade"')
test_run:cmd('start server upgrade')
test_run:switch('upgrade')

assert(not box.internal.schema_needs_upgrade())
box.space.distro:select{}
box.space._index:select{box.space.distro.id}
box.space._space:format()
box.schema.user.info('admin')
box.schema.user.info('guest')
box.schema.user.info('someuser')
box.schema.role.info('somerole')

test_run:switch('default')
test_run:cmd('stop server upgrade')
test_run:cmd('delete server upgrade')

-- Upgrade from 1.7.1.
test_run:cmd('create server upgrade with script="xlog/upgrade.lua", \
             workdir="xlog/upgrade/1.7.1/gh-5894-pre-1.7.7-upgrade"')
test_run:cmd('start server upgrade')
test_run:switch('upgrade')

assert(not box.internal.schema_needs_upgrade())
box.space.distro:select{}
box.space._index:select{box.space.distro.id}
box.space._space:format()
box.schema.user.info('admin')
box.schema.user.info('guest')
box.schema.user.info('someuser')
box.schema.role.info('somerole')

test_run:switch('default')
test_run:cmd('stop server upgrade')
test_run:cmd('delete server upgrade')

-- Upgrade from 1.7.2.
test_run:cmd('create server upgrade with script="xlog/upgrade.lua", \
             workdir="xlog/upgrade/1.7.2/gh-5894-pre-1.7.7-upgrade"')
test_run:cmd('start server upgrade')
test_run:switch('upgrade')

assert(not box.internal.schema_needs_upgrade())
box.space.distro:select{}
box.space._index:select{box.space.distro.id}
box.space._space:format()
box.schema.user.info('admin')
box.schema.user.info('guest')
box.schema.user.info('someuser')
box.schema.role.info('somerole')

test_run:switch('default')
test_run:cmd('stop server upgrade')
test_run:cmd('delete server upgrade')

-- Upgrade from 1.7.5.
test_run:cmd('create server upgrade with script="xlog/upgrade.lua", \
             workdir="xlog/upgrade/1.7.5/gh-5894-pre-1.7.7-upgrade"')
test_run:cmd('start server upgrade')
test_run:switch('upgrade')

assert(not box.internal.schema_needs_upgrade())
box.space.distro:select{}
box.space._index:select{box.space.distro.id}
box.space._space:format()
box.schema.user.info('admin')
box.schema.user.info('guest')
box.schema.user.info('someuser')
box.schema.role.info('somerole')

test_run:switch('default')
test_run:cmd('stop server upgrade')
test_run:cmd('delete server upgrade')
