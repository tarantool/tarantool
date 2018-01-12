#!/usr/bin/env tarantool

os.setenv('TEST_VAR', '48')
box.cfg { log = '|echo $TEST_VAR' }
os.exit(0)
