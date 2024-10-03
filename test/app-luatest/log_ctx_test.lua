-- @TODO
-- * 310.02 app-tap/logger.test.lua
-- * 310.02 app-luatest/log_ctx_test.lua
local fio = require('fio')

local t = require('luatest')

local tg = t.group('log_ctx', {
    {log_level = 'debug'},
    {log_level = 'verbose'},
    {log_level = 'info'},
    {log_level = 'warn'},
    {log_level = 'error'},
})

-- @TODO
-- local tempdir = fio.tempdir()
local tempdir = '/tmp/newtest'
local server = require('luatest.server'):new({
    box_cfg = {
        log = tempdir .. '/log.log',
        log_format = 'json',
    }
})

t.before_suite(function()
    -- @TODO remove.
    fio.rmtree(tempdir)

    fio.mkdir(tempdir)
    server:start()
end)

t.after_suite(function()
    server:drop()

    -- @TODO uncomment.
    -- fio.rmtree(tempdir)
end)

tg.test_default_ctx = function(g)
    server:exec(function(log_level)
        print(1)
        box.cfg({log_level = log_level})
        print(2)
        require('log')[log_level]({message = string.upper(log_level)})
        print(3)
        box.cfg({log_level = 'info'})
        print(4)
    end, {g.params.log_level})
end
