local fio = require('fio')
local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{alias = 'dflt'}
    g.server:start()
    g.server:exec(function()
        require("log").internal.ratelimit.disable()
    end)
end)

g.after_all(function()
    g.server:drop()
end)

local expected_log_entry_fmt =
    "C> Potentially long select from space '%s' %%(%d%%)"
local grep_log_bytes = 1024
local dangerous_call_fmts = {
    'box.space.%s:select(nil)',
    'box.space.%s:select({})',
    'box.space.%s:select(nil, {})',
    'box.space.%s:select(nil, {iterator = "EQ"})',
    'box.space.%s:select(nil, {iterator = "REQ"})',
    'box.space.%s:select(nil, {fullscan = false})',
    'box.space.%s:select(nil, {limit = 1001})',
    'box.space.%s:select(nil, {offset = 1})',
    'box.space.%s:select(nil, {limit = 500, offset = 501})',
    'box.space.%s:select({0}, {limit = 1001, iterator = "ALL"})',
    'box.space.%s:select({0}, {limit = 1001, iterator = "GE"})',
    'box.space.%s:select({0}, {limit = 1001, iterator = "GT"})',
    'box.space.%s:select({0}, {limit = 1001, iterator = "LE"})',
    'box.space.%s:select({0}, {limit = 1001, iterator = "LT"})',
}
local log_entry_absence_msg_fmt = 'log must not contain a critical entry ' ..
                                  'about `%s` call on a %s %s space'

g.test_log_entry_absence_for_sys_space = function()
    local sid = g.server:eval('return box.space._space.id')
    t.fail_if(sid > 512, "'_space' space is assumed to be a system space")
    local expected_log_entry = expected_log_entry_fmt:format("_space", sid)
    for _, call_fmt in pairs(dangerous_call_fmts) do
        local call = call_fmt:format('_space')
        g.server:eval(call)
        t.assert_not(g.server:grep_log(expected_log_entry, grep_log_bytes),
                     log_entry_absence_msg_fmt:format(call, "", "system"))
    end
end

for _, eng in pairs{'memtx', 'vinyl'} do
    g[('test_log_entry_presence_for_%s_user_space'):format(eng)] = function()
        local space = ('test_%s'):format(eng)
        g.server:eval(("box.schema.space.create('%s', " ..
                       "{engine = '%s'})"):format(space, eng))
        g.server:eval(("box.space.%s:create_index('pk')"):format(space))

        local sid = g.server:eval(('return box.space.%s.id'):format(space))
        local expected_log_entry = expected_log_entry_fmt:format(space, sid)
        require('log').info("expected_log_entry=%s", expected_log_entry)

        local safe_call_fmts = {
            'box.space.%s:select(nil, {limit = 1000, iter = "ALL"})',
            'box.space.%s:select(nil, {limit = 1000, iter = "GE"})',
            'box.space.%s:select(nil, {limit = 1000, iter = "GT"})',
            'box.space.%s:select(nil, {limit = 1000, iter = "LE"})',
            'box.space.%s:select(nil, {limit = 1000, iter = "LT"})',
            'box.space.%s:select(nil, {limit = 500, iter = "EQ"})',
            'box.space.%s:select(nil, {limit = 500, offset = 500})',
            'box.space.%s:select(nil, {limit = 250, offset = 250})',
            'box.space.%s:select({0}, {iter = "EQ"})',
            'box.space.%s:select({0}, {iter = "REQ"})',
            'box.space.%s:select({0}, {limit = 1001, iter = "EQ"})',
            'box.space.%s:select({0}, {iter = "EQ", fullscan = false})',
            'box.space.%s:select({0}, {iter = "ALL", fullscan = true})',
            'box.space.%s:select({0}, {iter = "GE", fullscan = true})',
            'box.space.%s:select({0}, {iter = "GT", fullscan = true})',
            'box.space.%s:select({0}, {iter = "LE", fullscan = true})',
            'box.space.%s:select({0}, {iter = "LT", fullscan = true})',
            'box.space.%s:select({0}, {limit = 1001, fullscan = true})',
        }
        for _, call_fmt in pairs(safe_call_fmts) do
            local call = call_fmt:format(space)
            g.server:eval(call)
            t.assert_not(g.server:grep_log(expected_log_entry, grep_log_bytes),
                         log_entry_absence_msg_fmt:format(call, eng, "user"))
        end

        local explicit_fullscan_call_fmts = {
            'box.space.%s:select(nil, {fullscan = true})',
            'box.space.%s:select(nil, {limit = 1001, fullscan = true})',
            'box.space.%s:select(nil, {offset = 1001, fullscan = true})',
            'box.space.%s:select(nil, {limit = 500, offset = 501, ' ..
            'fullscan = true})',
            'box.space.%s:select({0}, {limit = 1001, iterator = "ALL", ' ..
            'fullscan = true})',
            'box.space.%s:select({0}, {limit = 1001, iterator = "GE", ' ..
            'fullscan = true})',
            'box.space.%s:select({0}, {limit = 1001, iterator = "GT", ' ..
            'fullscan = true})',
            'box.space.%s:select({0}, {limit = 1001, iterator = "LE", ' ..
            'fullscan = true})',
            'box.space.%s:select({0}, {limit = 1001, iterator = "LT", ' ..
            'fullscan = true})',
        }
        for _, call_fmt in pairs(explicit_fullscan_call_fmts) do
            local call = call_fmt:format(space)
            g.server:eval(call)
            t.assert_not(g.server:grep_log(expected_log_entry, grep_log_bytes),
                        ('log must not contain a critical entry about ' ..
                         'an empty or nil `select` call on a %s '..
                         'user space with `fullscan = true` option '..
                         'set'):format(eng))
        end

        local log_file = g.server:eval('return box.cfg.log')
        for _, call_fmt in pairs(dangerous_call_fmts) do
            local call = call_fmt:format(space)
            g.server:eval(call)
            t.helpers.retrying({}, function()
                t.assert(g.server:grep_log(expected_log_entry, grep_log_bytes),
                         ('log must contain a critical entry ' ..
                          'about `%s` call on a %s user space'):format(call, eng))
            end)
            fio.truncate(log_file)
        end
    end
end
