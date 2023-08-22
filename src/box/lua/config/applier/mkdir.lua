local fio = require('fio')
local log = require('internal.config.utils.log')

-- Create a directory if it doesn't exist.
local function safe_mkdir(prefix, dir, work_dir)
    -- All the directories are interpreted as relative to
    -- `process.work_dir`. The first box.cfg() call sets a
    -- current working directory to this path.
    --
    -- However, we should prepend paths manually before the first
    -- box.cfg() call.
    --
    -- The absolute path is checked explicitly due to gh-8816.
    local needs_prepending = type(box.cfg) == 'function' and
        work_dir ~= nil and not dir:startswith('/')
    if needs_prepending then
        dir = fio.pathjoin(work_dir, dir)
    end

    local stat = fio.stat(dir)

    if stat == nil then
        log.verbose('%s: create directory: %s', prefix, dir)
        local _, err = fio.mktree(dir)
        if err ~= nil then
            error(('%s: failed to create directory %s: %s'):format(prefix,
                dir, err))
        end
    else
        if fio.path.is_dir(dir) then
            log.verbose('%s: the directory already exists: %s', prefix, dir)
        else
            error(('%s: the file is supposed to be a directory, ' ..
                'but it is not a directory: %s'):format(prefix, dir))
        end
    end
end

local function apply(config)
    local configdata = config._configdata
    local work_dir = configdata:get('process.work_dir', {use_default = true})

    -- Create process.work_dir directory if box.cfg() is not
    -- called yet.
    if type(box.cfg) == 'function' and work_dir ~= nil then
        local prefix = ('mkdir.apply[%s]'):format('process.work_dir')
        safe_mkdir(prefix, work_dir)
    end

    configdata:filter(function(w)
        return w.schema.mkdir ~= nil
    end, {use_default = true}):each(function(w)
        if w.data == nil then
            return
        end
        local prefix = ('mkdir.apply[%s]'):format(table.concat(w.path, '.'))
        safe_mkdir(prefix, w.data, work_dir)
    end)

    configdata:filter(function(w)
        return w.schema.mk_parent_dir ~= nil
    end, {use_default = true}):each(function(w)
        if w.data == nil then
            return
        end
        local prefix = ('mkdir.apply[%s]'):format(table.concat(w.path, '.'))
        safe_mkdir(prefix, fio.dirname(w.data), work_dir)
    end)

    local console = configdata:get('console', {use_default = true})
    if console.enabled then
        local prefix = ('mkdir.apply[%s]'):format('console.socket')
        safe_mkdir(prefix, fio.dirname(console.socket), work_dir)
    end

    local log = configdata:get('log', {use_default = true})
    if log.to == 'file' then
        local prefix = ('mkdir.apply[%s]'):format('log.file')
        safe_mkdir(prefix, fio.dirname(log.file), work_dir)
    end
end

return {
    name = 'mkdir',
    apply = apply,
}
