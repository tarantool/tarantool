local fio = require('fio')
local popen = require('popen')

local TARANTOOL_PATH = fio.abspath(arg[-1])
local SCRIPT_PATH = fio.abspath(arg[0])
local WORK_DIR = fio.dirname(SCRIPT_PATH)

if not arg[1] then
    -- master
    box.cfg{
        work_dir = fio.pathjoin(WORK_DIR, 'master'),
        checkpoint_count = 1,
        listen = '/tmp/tt.sock',
    }
    box.schema.user.grant('guest', 'super')

    popen.new({TARANTOOL_PATH, SCRIPT_PATH, 'replica'}):wait()
    box.snapshot()
    os.exit(0)
else
    -- replica
    box.cfg{
        work_dir = fio.pathjoin(WORK_DIR, 'replica'),
        replication = '/tmp/tt.sock',
    }
    os.exit(0)
end
