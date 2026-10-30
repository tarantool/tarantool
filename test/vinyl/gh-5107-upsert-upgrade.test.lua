test_run = require('test_run').new()

-- Upsert's internal format have changed: now update operations are stored
-- with additional map package. Let's test backward compatibility.
--
-- Make sure that upserts will be parsed and squashed correctly.
--

work_dir = 'vinyl/upgrade/2.5.1/gh-5107-upsert-upgrade/'

test_run:cmd('create server upgrade with script="vinyl/upgrade.lua", workdir="' .. work_dir .. '"')
test_run:cmd('start server upgrade')
test_run:switch('upgrade')

-- No need to call :upgrade() since no user-visible schema changes took
-- place, only internal format of :upserts have been changed.
-- box.schema.upgrade()

box.space.test:select()

test_run:switch('default')
test_run:cmd('stop server upgrade')
test_run:cmd('cleanup server upgrade')
