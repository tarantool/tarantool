local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local g = t.group()

g.before_all(treegen.init)
g.after_all(treegen.clean)

-- A few test cases for the loaders.require_first() function.
--
-- Note: The loaders module is internal.
g.test_basic = function(g)
    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'a.lua', 'return {whoami = "a"}')
    treegen.write_script(dir, 'b.lua', 'return {whoami = "b"}')

    treegen.write_script(dir, 'main.lua', string.dump(function()
        local loaders = require('internal.loaders')
        local t = require('luatest')

        for i, case in ipairs({
            -- Positive test cases.
            {args = {'a', 'b'}, exp = {whoami = 'a'}},
            {args = {'b', 'a'}, exp = {whoami = 'b'}},
            {args = {'a', 'c'}, exp = {whoami = 'a'}},
            {args = {'c', 'a'}, exp = {whoami = 'a'}},
            -- Negative test cases.
            {args = {}, err = 'loaders.require_first expects at least one ' ..
                'argument'},
            {args = {'c'}, err = 'neither of modules "c" are found'},
            {args = {'c', 'd'}, err = 'neither of modules "c", "d" are found'},
        }) do
            if case.exp ~= nil then
                assert(case.err == nil)
                local res = loaders.require_first(unpack(case.args))
                t.assert_equals(res, case.exp, ('case #%03d'):format(i))
            else
                assert(case.err ~= nil)
                t.assert_error_msg_equals(case.err, loaders.require_first,
                    unpack(case.args))
            end
        end
    end))

    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, {'main.lua'}, opts)
    t.assert_equals(res.exit_code, 0, {res.stdout, res.stderr})
end
