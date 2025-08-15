local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    cg.server = server:new()
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Checks that an invalid value of `ERRINJ_TUPLE_FIELD_COUNT_LIMIT` is handled
-- correctly.
g.test_invalid_tuple_field_count_limit_errinj = function()
    t.tarantool.skip_if_not_debug()

    local tuple = box.tuple.new{0, 0}
    box.error.injection.set('ERRINJ_TUPLE_FIELD_COUNT_LIMIT', 1)
    local msg = 'Tuple field count limit reached: see box.schema.FIELD_MAX'
    t.assert_error_msg_equals(msg, function()
        tuple:update{{'!', #tuple, 1}}
    end)
end
