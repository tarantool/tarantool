local fio = require('fio')
local errno = require('errno')

local startup_dir
local prepared = false

local function initialize()
    startup_dir = fio.cwd()
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
    path = resolve(path)
    if path ~= nil then
        -- fio.mktree() uses fio.abspath() internally. Walk the
        -- components without normalizing away symlink/.. instead.
        local prefix = ''
        for component in path:gmatch('[^/]+') do
            prefix = prefix .. '/' .. component
            if not fio.path.is_dir(prefix) then
                local ok, err = fio.mkdir(prefix)
                if not ok and not fio.path.is_dir(prefix) then
                    error(('process.work_dir: unable to create directory ' ..
                           '%q: %s'):format(prefix, err), 0)
                end
            end
        end
        if not fio.chdir(path) then
            local err = errno.strerror()
            error(('process.work_dir: unable to enter directory %q: %s')
                  :format(path, err), 0)
        end
    end
    prepared = true
end

return {
    initialize = initialize,
    resolve = resolve,
    prepare = prepare,
    is_prepared = function() return prepared end,
}
