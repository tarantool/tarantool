local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-13416')

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {checkpoint_interval = 0}})
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.test_rewritten_request_type = function(cg)
    local lsn = cg.server:exec(function()
        local s = box.space.test
        s:insert{1, 'original'}
        local lsn = box.info.lsn

        local returned_tuple, before_type, after_type
        s:before_replace(function(_, _, _, operation)
            before_type = operation
            return returned_tuple
        end)
        s:on_replace(function(_, _, _, operation)
            after_type = operation
        end)

        s:replace{1, 'changed'}
        t.assert_equals(before_type, 'REPLACE')
        t.assert_equals(after_type, 'DELETE')
        t.assert_equals(s:select(), {})

        returned_tuple = box.tuple.new{1, 'rewritten'}
        s:insert{1, 'original'}
        t.assert_equals(before_type, 'INSERT')
        t.assert_equals(after_type, 'REPLACE')
        t.assert_equals(s:get{1}, {1, 'rewritten'})

        returned_tuple = s:get{1}
        after_type = nil
        s:replace{1, 'ignored'}
        t.assert_equals(before_type, 'REPLACE')
        t.assert_equals(after_type, nil)
        t.assert_equals(s:get{1}, {1, 'rewritten'})

        return lsn
    end)

    local fio = require('fio')
    local xlog = require('xlog')
    local wal_types = {}
    for _, path in ipairs(fio.glob(cg.server.workdir .. '/*.xlog')) do
        for _, row in xlog.pairs(path) do
            if row.HEADER.lsn > lsn then
                table.insert(wal_types, row.HEADER.type)
            end
        end
    end
    t.assert_equals(wal_types, {'DELETE', 'REPLACE', 'NOP'})
end
