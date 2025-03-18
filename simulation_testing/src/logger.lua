local fio_utils = require('fio_utils')
local random_cluster = require('random_cluster')
Logger = require('log')

-- function extra_logs(...)
-- end

local function init_logger()
    os.remove('./working_log.log')
    os.remove('./memtx_dir')
    os.remove('./replicas_dir')
    Logger.cfg { log = './working_log.log' }




    function LogInfo(...)
        local t = {}
        for i = 1, select("#", ...) do
            t[i] = tostring(select(i, ...))
        end
        local msg = table.concat(t, "\t")
        extra_logs(msg)
        Logger.info(string.format("%s \n", msg))
    end

    function LogError(...)
        local t = {}
        for i = 1, select("#", ...) do
            t[i] = tostring(select(i, ...))
        end
        local msg = table.concat(t, "\t")
        extra_logs(msg)
        Logger.error(string.format("%s\n", msg))
    end

    fio_utils.create_memtx()
    fio_utils.clear_dirs_for_all_replicas()

end

return {
    Logger = Logger,
    init_logger = init_logger,
    extra_logs = extra_logs
}