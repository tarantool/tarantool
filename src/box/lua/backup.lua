-- backup.lua (internal file)

local fiber = require('fiber')
local utils = require('internal.utils')

local suid = function(f)
    return function(...)
        return box.session.su('admin', f, ...)
    end
end

local api_trace = function(f)
    return function(...)
        return utils.call_at(1, f, ...)
    end
end

box.internal.recovery_point_yield_loops = 1000

box.backup.recovery_point = {}

local box_backup_info = function()
    local info = box.internal.backup_info()
    if info == nil then
        return nil
    end
    local recovery_points = {}
    local begin_vclock
    assert(info.type == 'incremental' or
           info.type == 'full', 'unexpected backup type')
    if info.type == 'incremental' then
        begin_vclock = info.prev_vclock
    elseif info.type == 'full' then
        begin_vclock = info.checkpoint_vclock
    end
    local loops = 1
    for _, tuple in box.space._recovery_point.index.timestamp:pairs() do
        local begin_lsn = begin_vclock[tuple.replica_id] or 0
        local end_lsn = info.vclock[tuple.replica_id] or 0
        if tuple.lsn ~= nil and
           tuple.lsn > begin_lsn and
           tuple.lsn <= end_lsn then
            table.insert(recovery_points, tuple:tomap({names_only = true}))
        end
        if loops % box.internal.recovery_point_yield_loops == 0 then
            fiber.yield()
        end
        loops = loops + 1
    end
    info.recovery_points = recovery_points
    -- Clear internal field used only to filter recovery points.
    info.checkpoint_vclock = nil
    return info
end

box.backup.info = suid(box_backup_info)
box.backup.info = api_trace(box.backup.info)

box.backup.recovery_point.create = suid(box.internal.recovery_point_create)
box.backup.recovery_point.create = api_trace(box.backup.recovery_point.create)
