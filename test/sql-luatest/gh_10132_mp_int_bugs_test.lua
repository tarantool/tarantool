local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-10132')

g.before_all(function(cg)
    cg.server = server:new({alias = 'master'})
    cg.server:start()
    cg.server:exec(function()
        local msgpack = require('msgpack')
        local build_path = os.getenv("BUILDDIR")
        package.cpath = build_path..'/test/sql-luatest/?.so;'..
                        build_path..'/test/sql-luatest/?.dylib;'..package.cpath
        local opts = {language = 'C', returns = 'integer', param_list = {},
                      exports = {'SQL'}}
        box.schema.func.create('gh_10132.get_mp_int_one', opts);

        rawset(_G, 'conn', require('net.box').connect(box.cfg.listen))
        rawset(_G, 'do_sql', function(...)
            local rows = _G.conn:execute(...).rows
            t.assert_equals(#rows, 1)
            t.assert_equals(#rows[1], 1)
            return rows[1][1]
        end)
        rawset(_G, 'make_mp', function(...)
            return msgpack.object_from_raw(...)
        end)
        local format = {
            {'id', 'integer'},
        }
        local s = box.schema.create_space('test', {format = format})
        s:create_index('pk')
    end)
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test_bind = function(cg)
    cg.server:exec(function()
        t.assert_equals(_G.do_sql('SELECT ?', {_G.make_mp('\xd0\x01')}), 1)
    end)
end

g.test_compare = function(cg)
    cg.server:exec(function()
        box.space.test:replace{_G.make_mp('\xd0\x01')}
        t.assert(_G.do_sql('SELECT "id" > 0 FROM "test" WHERE "id" = 1'))
        t.assert_equals(_G.do_sql(
            'SELECT "id" + 0 FROM "test" WHERE "id" = 1'), 1)
        box.space.test:replace{_G.make_mp('\xd0\x02')}
        t.assert_equals(_G.do_sql('SELECT 1 FROM "test" WHERE "id" <= 1'), 1)
        box.space.test:delete{1}
        box.space.test:delete{2}
    end)
end

g.test_c_functions = function(cg)
    cg.server:exec(function()
        t.assert(_G.do_sql([[SELECT "gh_10132.get_mp_int_one"() > 0]]))
        t.assert_equals(
            _G.do_sql([[SELECT "gh_10132.get_mp_int_one"() + 0]]), 1)
    end)
end
