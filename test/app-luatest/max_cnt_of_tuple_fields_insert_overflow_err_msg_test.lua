local t = require('luatest')

local g = t.group()

g.before_each(function()
    t.tarantool.skip_if_not_debug()
end)

g.after_each(function()
    box.error.injection.set('ERRINJ_TUPLE_FIELD_COUNT_LIMIT', -1)
end)

-- Checks that the overflow of the tuple field count limit returns an error
-- (gh-6198).
g.test_tuple_field_count_limit_overflow = function()
    local tuple = box.tuple.new{0}
    box.error.injection.set('ERRINJ_TUPLE_FIELD_COUNT_LIMIT', 1)
    local msg = 'Tuple field count limit reached: see box.schema.FIELD_MAX'
    t.assert_error_msg_equals(msg, function()
        tuple:update{{'!', #tuple, 1}}
    end)
end

-- Checks that an invalid value of `ERRINJ_TUPLE_FIELD_COUNT_LIMIT` is handled
-- correctly (gh-10033).
g.test_invalid_tuple_field_count_limit_errinj = function()
    local tuple = box.tuple.new{0, 0}
    box.error.injection.set('ERRINJ_TUPLE_FIELD_COUNT_LIMIT', 1)
    local msg = 'Tuple field count limit reached: see box.schema.FIELD_MAX'
    t.assert_error_msg_equals(msg, function()
        tuple:update{{'!', #tuple, 1}}
    end)
end
