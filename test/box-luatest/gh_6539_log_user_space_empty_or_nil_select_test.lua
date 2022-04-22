local fio = require('fio')
local server = require('test.luatest_helpers.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new{alias = 'dflt'}
    g.server:start()
end)

g.after_all(function()
    g.server:drop()
end)

local expected_log_entry = 'C> empty or nil `select` call on user space ' ..
                           'with id=%d+'
local grep_log_bytes = 256
local dangerous_call_fmts = {'box.space.%s:select()', 'box.space.%s:select{}',
                             'box.space.%s:select(nil, {})',
                             'box.space.%s:select(nil, {fullscan = false})'}
local log_entry_absence_msg_fmt = 'log must not contain a critical entry ' ..
                                  'about `%s` call on a %s %s space'

g.test_log_entry_absence_for_sys_space = function()
    t.fail_if(g.server:eval('return box.space._space.id') > 512,
              '`_space` space is assumed to be a system space')
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

        local safe_call_fmts = {'box.space.%s:select{0}',
                                'box.space.%s:select({0}, {fullscan = false})',
                                'box.space.%s:select({0}, {fullscan = true})'}
        for _, call_fmt in pairs(safe_call_fmts) do
            local call = call_fmt:format(space)
            g.server:eval(call)
            t.assert_not(g.server:grep_log(expected_log_entry, grep_log_bytes),
                         log_entry_absence_msg_fmt:format(call, eng, "user"))
        end

        local explicit_call_fmts = {'box.space.%s:select(nil, ' ..
                                    '{fullscan = true})',
                                    'box.space.%s:select({}, ' ..
                                    '{fullscan = true})'}
        for _, call_fmt in pairs(explicit_call_fmts) do
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
            t.assert(g.server:grep_log(expected_log_entry, grep_log_bytes),
                     ('log must contain a critical entry ' ..
                      'about `%s` call on a %s user space'):format(call, eng))
            fio.truncate(log_file)
        end
    end
end
