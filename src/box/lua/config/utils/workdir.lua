local fio = require('fio')
local errno = require('errno')
local file = require('internal.config.utils.file')

local startup_dir
local prepared = false
-- configured workdir string
local prepared_value
-- physical absolute directory entered
local prepared_dir

local function initialize()
    startup_dir = fio.cwd()
end

local function is_prepared()
    -- A preload or command-line chunk may have configured the database
    -- before declarative startup. Its workdir cannot change until restart.
    return prepared or type(box.cfg) ~= 'function'
end

local function get_prepared_dir()
    return prepared_dir
end

-- An early application may change cwd after directory preparation. Keep
-- the raw option public, but always enter the saved target before recovery.
local function box_cfg_path(path)
    if path ~= nil and prepared and path == prepared_value then
        return prepared_dir
    end
    return path
end

local function resolve(path)
    if path == nil or path:startswith('/') then
        return path
    end
    -- fio.abspath() would collapse symlink/.. lexically, changing
    -- the target of normal filesystem traversal.
    return (startup_dir or fio.cwd()) .. '/' .. path
end

local function prepare(path)
    assert(not prepared)
    if is_prepared() then
        prepared = true
        return
    end
    prepared_value = path
    path = resolve(path)
    if path ~= nil then
        local ok, err, failed_dir = file.mktree(path)
        if not ok then
            error(('process.work_dir: unable to create directory %q: %s')
                  :format(failed_dir, err), 0)
        end
        if not fio.chdir(path) then
            local err = errno.strerror()
            error(('process.work_dir: unable to enter directory %q: %s')
                  :format(path, err), 0)
        end
        prepared_dir = assert(fio.cwd())
    end
    prepared = true
end

return {
    initialize = initialize,
    resolve = resolve,
    prepare = prepare,
    is_prepared = is_prepared,
    get_prepared_dir = get_prepared_dir,
    box_cfg_path = box_cfg_path,
}
