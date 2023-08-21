local fio = require('fio')
local log = require('internal.config.utils.log')

-- Create a directory if it doesn't exist.
local function safe_mkdir(prefix, dir)
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
    configdata:filter(function(w)
        return w.schema.mkdir ~= nil
    end, {use_default = true}):each(function(w)
        if w.data == nil then
            return
        end
        local prefix = ('mkdir.apply[%s]'):format(table.concat(w.path, '.'))
        safe_mkdir(prefix, w.data)
    end)

    configdata:filter(function(w)
        return w.schema.mk_parent_dir ~= nil
    end, {use_default = true}):each(function(w)
        if w.data == nil then
            return
        end
        local prefix = ('mkdir.apply[%s]'):format(table.concat(w.path, '.'))
        safe_mkdir(prefix, fio.dirname(w.data))
    end)

    local console = configdata:get('console', {use_default = true})
    if console.enabled then
        local prefix = ('mkdir.apply[%s]'):format('console.socket')
        safe_mkdir(prefix, fio.dirname(console.socket))
    end
end

return {
    name = 'mkdir',
    apply = apply,
}
