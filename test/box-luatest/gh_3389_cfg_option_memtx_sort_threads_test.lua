local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function(cg)
    -- test setting memtx_sort_threads to non default value
    cg.server = server:new{}
    cg.server:start()
    cg.server:exec(function()
        -- this will trigger calling tt_sort on box.cfg{}
        local s = box.schema.space.create('test')
        s:create_index('pri')
        s:replace{2}
        s:replace{1}
        box.snapshot()
    end)
end)

g.after_all(function(cg)
    if cg.server ~= nil then
        cg.server:drop()
    end
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:stop()
    end
end)

g.test_setting_cfg_option = function(cg)
    cg.server = server:new{box_cfg = {memtx_sort_threads = 3}}
    cg.server:start()
    cg.server:exec(function()
        t.assert_error_msg_equals(
            "Can't set option 'memtx_sort_threads' dynamically",
            box.cfg, {memtx_sort_threads = 5})
    end)
end

g.test_setting_openmp_env_var = function(cg)
    cg.server = server:new{box_cfg = {log_level = 'warn'},
                           env = {OMP_NUM_THREADS = ' 3'}}
    cg.server:start()
    t.helpers.retrying({}, function()
        local p = "OMP_NUM_THREADS is used to set number" ..
                  " of sorting threads. Use cfg option" ..
                  " 'memtx_sort_threads' instead."
        t.assert_not_equals(cg.server:grep_log(p), nil)
    end)
    cg.server:stop()
end

g.test_setting_openmp_env_var_bad = function(cg)
    cg.server = server:new{env= {OMP_NUM_THREADS = '300'}}
    cg.server:start()
end
