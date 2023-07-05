local t = require('luatest')

local function before_all(cg)
    local server = require('luatest.server')
    cg.server = server:new()
    cg.server:start()
end

local function after_all(cg)
    cg.server:drop()
end

local g1 = t.group('gh-4544-1')
g1.before_all(before_all)
g1.after_all(after_all)

-- Check that key_def compare doesn't crash after collation drop.
g1.test_keydef_crash = function(cg)
    cg.server:exec(function()
        local key_def = require('key_def')
        local coll_name = 'unicode_af_s1'
        local kd = key_def.new({{field = 1, type = 'string',
                                 collation = coll_name}})
        box.internal.collation.drop(coll_name)
        kd:compare({'a'}, {'b'})
        t.assert_equals(kd:totable()[1].collation, '<deleted>')
    end)
end

-- Replace old collation, which is used by key_def, by the new one with the same
-- fingerprint.
g1.test_keydef_replace_coll_same = function(cg)
    cg.server:exec(function()
        local key_def = require('key_def')
        local old_name = 'my old coll'
        local new_name = 'my new coll'
        box.internal.collation.create(old_name, 'ICU', '', {})
        local kd = key_def.new({{field = 1, type = 'string',
                                 collation = old_name}})
        box.internal.collation.drop(old_name)
        box.internal.collation.create(new_name, 'ICU', '', {})
        t.assert_equals(kd:totable()[1].collation, new_name)
        box.internal.collation.drop(new_name)
    end)
end

-- Replace old collation, which is used by key_def, by the new one with
-- different fingerprint.
g1.test_keydef_replace_coll_different = function(cg)
    cg.server:exec(function()
        local key_def = require('key_def')
        local old_name = 'my old coll'
        local new_name = 'my new coll'
        box.internal.collation.create(old_name, 'ICU', '', {})
        local kd = key_def.new({{field = 1, type = 'string',
                                 collation = old_name}})
        box.internal.collation.drop(old_name)
        box.internal.collation.create(new_name, 'ICU', 'ru-RU', {})
        t.assert_equals(kd:totable()[1].collation, '<deleted>')
        box.internal.collation.drop(new_name)
    end)
end
