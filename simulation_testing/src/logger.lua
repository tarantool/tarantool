local fio_utils = require('fio_utils')
local random_cluster = require('random_cluster')
Logger = require('log')

_G.EXTRA_LOGS = function (...)
end

_G.HAS_ERROR = function ()
end

-- Определяем функции сразу как глобальные
_G.LogInfo = function(...)
    local t = {}
    for i = 1, select("#", ...) do
        t[i] = tostring(select(i, ...))
    end
    local msg = table.concat(t, "\t")
    EXTRA_LOGS(msg)
    Logger.info(string.format("%s \n", msg))
end

_G.LogError = function(...)
    local t = {}
    for i = 1, select("#", ...) do
        t[i] = tostring(select(i, ...))
    end
    local msg = table.concat(t, "\t")
    EXTRA_LOGS(msg)
    HAS_ERROR()
    Logger.error(string.format("%s\n", msg))
end

local function init_logger()
    os.remove('./working_log.log')
    os.remove('./memtx_dir')
    os.remove('./replicas_dir')
    Logger.cfg { log = './working_log.log' }
    fio_utils.create_memtx()
    fio_utils.clear_dirs_for_all_replicas()
end

return {
    Logger = Logger,
    init_logger = init_logger,
}