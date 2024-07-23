local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new {
        alias   = 'dflt',
        box_cfg = {memtx_use_mvcc_engine = true}
    }
    cg.server:start()
    cg.server:exec(function()
        local s = box.schema.create_space('s')
        s:create_index('pk', {type = 'HASH'})
    end)
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that a deprecation warning is printed exactly once when using memtx HASH
-- index 'GT' iterator type.
g.test_memtx_hash_idx_iter_gt_deprecation = function(cg)
    cg.server:exec(function()
        box.space.s:select({}, {iterator = 'GT'})
    end)
    local deprecation_warning = "HASH index 'GT' iterator type is " ..
                                "deprecated since Tarantool 2.11 and should " ..
                                "not be used. It will be removed in a " ..
                                "future Tarantool release."
    t.assert_is_not(cg.server:grep_log(deprecation_warning, 256), nil)
    local log_file = g.server:exec(function()
        return rawget(_G, 'box_cfg_log_file') or box.cfg.log
    end)
    fio.truncate(log_file)
    cg.server:exec(function()
        box.space.s:select({}, {iterator = 'GT'})
    end)
    t.assert_is(cg.server:grep_log(deprecation_warning, 256), nil)
end
