local fio = require('fio')
local instance_config = require('internal.config.instance_config')

local snapshot_path = nil

-- Determine where snapshot should reside based on the given
-- configuration.
--
-- To be called before first box.cfg().
local function effective_snapshot_dir(iconfig)
    assert(iconfig ~= nil)
    -- The snapshot directory has a default value in the schema
    -- (it is a string). So, it can't be nil or box.NULL.
    local snap_dir = instance_config:get(iconfig, 'snapshot.dir')
    assert(snap_dir ~= nil)

    -- If the path is absolute, just return it.
    --
    -- This check is necessary due to fio.pathjoin() peculiars,
    -- see gh-8816.
    if snap_dir:startswith('/') then
        return snap_dir
    end

    -- We assume that the startup working directory is the current
    -- working directory. IOW, that this function is called before
    -- first box.cfg() call. Let's verify it.
    assert(type(box.cfg) == 'function')

    -- If the snapshot directory is not absolute, it is relative
    -- to the working directory.
    --
    -- Determine an absolute path to the configured working
    -- directory considering that it may be relative to the
    -- working directory at the startup moment.
    local work_dir = instance_config:get(iconfig, 'process.work_dir')
    if work_dir == nil then
        work_dir = '.'
    end
    work_dir = fio.abspath(work_dir)

    -- Now we know the absolute path to the configured working
    -- directory. Let's determine the snapshot directory path.
    return fio.abspath(fio.pathjoin(work_dir, snap_dir))
end

-- Determine whether the instance will be recovered from an existing
-- snapshot and return its path. Should be called before box.cfg.
--
-- To be called before first box.cfg().
local function get_snapshot_path(iconfig)
    assert(type(box.cfg) == 'function')
    if snapshot_path == nil then
        local snap_dir = effective_snapshot_dir(iconfig)
        local glob = fio.glob(fio.pathjoin(snap_dir, '*.snap'))
        if #glob > 0 then
            table.sort(glob)
            snapshot_path = glob[#glob]
        end
    end

    return snapshot_path
end

return {
    get_path = get_snapshot_path,
}
