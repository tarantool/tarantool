local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new{box_cfg = {memtx_use_mvcc_engine = true}}
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:stop()
end)

-- Checks that unsupported MVCC features cannot be enabled along with MVCC.
g.test_unsupported_features = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        s:create_index('pk')

        box.schema.func.create('test', {
            is_deterministic = true,
            body = [[function(tuple)
                return {tuple[1]}
            end]]
        })

        box.schema.func.create('test_mk', {
            is_deterministic = true,
            is_multikey = true,
            body = [[function(tuple)
                return {{tuple[1]}, {tuple[2]}}
            end]]
        })

        local errmsg = "Memtx MVCC engine does not support functional " ..
                       "indexes with 'exclude_null'"
        t.assert_error_msg_content_equals(errmsg, function()
            s:create_index('test_secondary', {
                func = 'test',
                parts = {{1, 'unsigned', is_nullable = true, exclude_null = true}},
            })
        end)

        errmsg = "Memtx MVCC engine does not support functional " ..
                 "multikey indexes"
        t.assert_error_msg_content_equals(errmsg, function()
            s:create_index('test_secondary', {
                func = 'test_mk',
                parts = {{1, 'unsigned'}},
            })
        end)

        errmsg = "Memtx MVCC engine does not support multikey indexes"
        t.assert_error_msg_content_equals(errmsg, function()
            s:create_index('test_secondary', {
                parts = {{1, 'unsigned', path = '[*]'}},
            })
        end)
    end)
end
