#!/usr/bin/env tarantool
local fio = require('fio')

-- Execute errinj_set_with_enviroment_vars_script.lua
-- via tarantool with presetted environment variables.
local TARANTOOL_PATH = arg[-1]
local bool_env = 'ERRINJ_TESTING=true ' ..
                 'ERRINJ_WAL_IO=True ' ..
                 'ERRINJ_WAL_ROTATE=TRUE ' ..
                 'ERRINJ_WAL_WRITE=false ' ..
                 'ERRINJ_INDEX_ALLOC=False ' ..
                 'ERRINJ_WAL_WRITE_DISK=FALSE'
local integer_env = 'ERRINJ_WAL_WRITE_PARTIAL=2 ' ..
                    'ERRINJ_WAL_FALLOCATE=+2 ' ..
                    'ERRINJ_VY_INDEX_DUMP=-2'
local double_env = 'ERRINJ_VY_READ_PAGE_TIMEOUT=2.5 ' ..
                   'ERRINJ_VY_SCHED_TIMEOUT=+2.5 ' ..
                   'ERRINJ_RELAY_TIMEOUT=-2.5'
local set_env_str = ('%s %s %s'):format(bool_env, integer_env, double_env)
local path_to_test_file = fio.pathjoin(
        os.getenv('PWD'),
        'box-tap',
        'errinj_set_with_enviroment_vars_script.lua')
local shell_command = ('%s %s %s'):format(
                set_env_str,
                TARANTOOL_PATH,
                path_to_test_file)

os.exit(os.execute(shell_command))
