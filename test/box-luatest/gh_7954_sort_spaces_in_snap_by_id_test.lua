local t = require('luatest')
local g = t.group('gh-7954')

g.before_all(function(cg)
    local server = require('luatest.server')
    cg.server = server:new({alias = 'master'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Check that spaces in the snapshot file are strictly ordered.
g.test_spaces_in_snap = function(cg)
    local fio = require('fio')
    local xlog = require('xlog')

    -- Create some spaces, fill them with data, and write a snapshot.
    local snap_name, expected_user_ids = cg.server:exec(function()
        local spaces = {}
        local ids = {167, 193, 239, 739, 761, 863, 881, 907, 937}
        for i, id in ipairs(ids) do
            spaces[i] = box.schema.create_space('test' .. id, {id = id})
            spaces[i]:create_index('primary')
        end
        for i = 1, 100 do
            spaces[1 + i % #spaces]:insert{i, i*i}
        end
        box.snapshot()
        local checkpoints = box.info.gc().checkpoints
        local snap_lsn = checkpoints[#checkpoints].signature
        local snap_name = string.format("%020d.snap", snap_lsn)
        return snap_name, ids
    end)

    -- Expected order:
    -- 1. System spaces (id > 256 && id < 511)
    -- 2. Spaces 167, 193, 239
    -- 3. Spaces 739, 761, 863, 881, 907, 937
    -- For each subgroup id >= prev_id
    local prev_id = 0
    local snap_user_ids = {}
    local switched_to_user = false
    local snap_path = fio.pathjoin(cg.server.workdir, snap_name)
    for _, row in xlog.pairs(snap_path) do
        if row.HEADER.type == "INSERT" then
            local id = row.BODY.space_id
            if switched_to_user then
                -- Checking order of user spaces
                if id ~= prev_id then
                    table.insert(snap_user_ids, id)
                end
            else
                -- Checking order of system spaces
                if id ~= expected_user_ids[1] then
                    t.assert_ge(id, prev_id)
                else
                    switched_to_user = true
                    table.insert(snap_user_ids, id)
                end
            end
            prev_id = id
        end
    end
    t.assert_equals(snap_user_ids, expected_user_ids)
end
