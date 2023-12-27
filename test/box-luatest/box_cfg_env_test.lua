local fun = require('fun')
local json = require('json')
local t = require('luatest')
local treegen = require('test.treegen')
local justrun = require('test.justrun')

local g = t.group()

g.before_all(treegen.init)
g.after_all(treegen.clean)

-- TT_LISTEN and TT_REPLICATION have many allowed forms.
--
-- We can't listen on hardcoded port numbers in the test, because
-- we don't know whether it will be free on running of the test
-- suite. Attempt to do so would ruin the stability of the test.
--
-- So, these test cases don't call `box.cfg()`, but rather use an
-- internal API to parse environment variables into box.cfg option
-- values.
local uri_list_cases = {
    -- Plain URI.
    {
        env = '3301',
        cfg = 3301,
    },
    {
        env = 'localhost:3301',
        cfg = 'localhost:3301',
    },
    {
        env = 'localhost:3301?transport=plain',
        cfg = 'localhost:3301?transport=plain',
    },
    -- Array of plain URIs.
    {
        env = '3301,3302',
        cfg = {3301, 3302},
    },
    {
        env = 'localhost:3301,localhost:3302',
        cfg = {'localhost:3301', 'localhost:3302'},
    },
    {
        env = 'localhost:3301?transport=plain,' ..
            'localhost:3302?transport=plain',
        cfg = {'localhost:3301?transport=plain',
            'localhost:3302?transport=plain'},
    },
}

-- Verify TT_LISTEN and TT_REPLICATION using box.internal.cfg.env.
--
-- It is a kind of a unit test.
--
-- All the test cases are run using one popen call
-- (justrun.tarantool()) that speeds up the execution.
g.test_uri_list = function(g)
    -- Write cases.lua and main.lua.
    local dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(dir, 'cases.lua', json.encode(uri_list_cases))
    treegen.write_script(dir, 'main.lua', string.dump(function()
        local json = require('json')
        local fio = require('fio')
        local t = require('luatest')

        -- Read cases.lua.
        local fh, err = fio.open('cases.lua', {'O_RDONLY'})
        if err ~= nil then
            error(err)
        end
        local cases_str, err = fh:read()
        if err ~= nil then
            error(err)
        end
        fh:close()
        local cases = json.decode(cases_str)

        local function verify(case, env_var_name, box_cfg_option_name)
            os.setenv(env_var_name, case.env)
            local res = box.internal.cfg.env[box_cfg_option_name]
            local ok, err = pcall(t.assert_equals, res, case.cfg)
            if not ok then
                error(err.message, 0)
            end
            os.setenv(env_var_name, nil)
        end

        -- Set the environment variable, get the box.cfg option
        -- from box.internal.cfg.env, check it against a reference
        -- value, unset the environment variable.
        for _, case in ipairs(cases) do
            verify(case, 'TT_LISTEN', 'listen')
            verify(case, 'TT_REPLICATION', 'replication')
        end
    end))

    local opts = {nojson = true, stderr = true}
    local res = justrun.tarantool(dir, {}, {'main.lua'}, opts)
    t.assert_equals(res, {
        exit_code = 0,
        stdout = '',
        stderr = '',
    })
end

-- These test cases use a real box.cfg() call inside a child
-- process. A kind of a functional test.
local cases = {
    -- String options.
    {
        env = {['TT_LOG_FORMAT'] = 'json'},
        cfg = {log_format = 'json'},
    },
    {
        env = {['TT_LOG_LEVEL'] = 'debug'},
        cfg = {log_level = 'debug'},
    },
    {
        env = {['TT_CUSTOM_PROC_TITLE'] = 'mybox'},
        cfg = {custom_proc_title = 'mybox'},
    },
    -- Numeric options.
    {
        env = {['TT_READAHEAD'] = '32640'},
        cfg = {readahead = 32640},
    },
    {
        env = {['TT_LOG_LEVEL'] = '7'},
        cfg = {log_level = 7},
    },
    {
        env = {['TT_REPLICATION_TIMEOUT'] = '0.3'},
        cfg = {replication_timeout = 0.3},
    },
    {
        env = {['TT_IPROTO_THREADS'] = '16'},
        cfg = {iproto_threads = 16},
    },
    -- Boolean options.
    {
        env = {['TT_MEMTX_USE_MVCC_ENGINE'] = 'true'},
        cfg = {memtx_use_mvcc_engine = true},
    },
    {
        env = {['TT_MEMTX_USE_MVCC_ENGINE'] = 'false'},
        cfg = {memtx_use_mvcc_engine = false},
    },
    {
        env = {['TT_STRIP_CORE'] = 'false'},
        cfg = {strip_core = false},
    },
}

-- Write a script to be used in the test cases below.
g.before_all(function(g)
    g.dir = treegen.prepare_directory(g, {}, {})
    treegen.write_script(g.dir, 'main.lua', [[
        local json = require('json')
        box.cfg()
        print(json.encode(box.cfg))
        os.exit()
    ]])
end)

-- Verify TT_* variables of different types.
for i, case in ipairs(cases) do
    g[('test_box_cfg_%03d'):format(i)] = function(g)
        local res = justrun.tarantool(g.dir, case.env, {'main.lua'})

        local res_cfg = fun.iter(case.cfg):map(function(box_cfg_option_name)
            if res.stdout == nil or #res.stdout ~= 1 then
                return box_cfg_option_name, '???'
            end
            return box_cfg_option_name, res.stdout[1][box_cfg_option_name]
        end):tomap()

        t.assert_equals({
            exit_code = res.exit_code,
            cfg = res_cfg,
        }, {
            exit_code = 0,
            cfg = case.cfg,
        })
    end
end
