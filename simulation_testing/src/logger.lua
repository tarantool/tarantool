local fio = require('fio')
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
    
    local log_dir = fio.dirname(WORKING_LOG_PATH)
    if not fio.path.is_dir(log_dir) then
        fio.mkdir(log_dir)
    end
    if fio.path.exists(WORKING_LOG_PATH) then
        os.remove(WORKING_LOG_PATH)
    end

    os.remove('./memtx_dir')
    os.remove('./replicas_dir')
    Logger.cfg { log = WORKING_LOG_PATH }
    fio_utils.create_memtx()
    fio_utils.clear_dirs_for_all_replicas()
end

return {
    Logger = Logger,
    init_logger = init_logger,
}