local server = require('luatest.server')
local t = require('luatest')

local g = t.group('gh-6986-c-constraint',
                  {{engine = 'memtx'}, {engine = 'vinyl'}})

g.before_all(function(cg)
    cg.server = server:new({alias = 'test_c_constraint'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:stop()
end)

g.test_c_constraint = function(cg)
    cg.server:exec(function(engine)
        local build_path = os.getenv("BUILDDIR")
        package.cpath = build_path..'/test/engine-luatest/?.so;'..
                        build_path..'/test/engine-luatest/?.dylib;'..
                        package.cpath
        local func = {language = 'C', is_deterministic = true}
        box.schema.func.create('gh_6986.get_check', func);
        local s = box.schema.create_space('test', {engine = engine})
        s:create_index('pk')
        s:alter{constraint='gh_6986.get_check'}
        t.assert_equals(s:insert{1}, {1})
        t.assert_error_msg_content_equals(
            "Check constraint 'gh_6986.get_check' failed for tuple",
            function() s:insert{2} end
        )
        s:drop()
    end, {cg.params.engine})
end
