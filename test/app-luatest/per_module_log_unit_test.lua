local t = require('luatest')
local g = t.group('per_module_log_unit')

local tarantool = require('tarantool')
local strip_cwd_from_path = tarantool._internal.strip_cwd_from_path
local module_name_from_filename = tarantool._internal.module_name_from_filename

g.test_strip_cwd_from_path = function()
    local testcases = {
        { cwd = '/', path = '/', expected = '' },
        { cwd = '/', path = '/aaa/bbb/ccc', expected = 'aaa/bbb/ccc' },
        { cwd = '/home/aaa/bbb', path = '/home/aaa/bbb/ccc/ddd', expected = 'ccc/ddd' },
        { cwd = '/home/some/other/path', path = '/home/aaa/bbb', expected = 'aaa/bbb' },
        { cwd = '/totally/different', path = '/home/aaa/bbb', expected = 'home/aaa/bbb' },
        { cwd = '/doesnt/matter', path = 'aaa/bbb', expected = 'aaa/bbb' },
        { cwd = '/qwerty', path = '/aaa/qwerty/bbb', expected = 'aaa/qwerty/bbb' },
        { cwd = '/home/a-aa/bbb', path = '/home/a-aa/bbbccc/ddd', expected = 'bbbccc/ddd' },
        { cwd = '/home/common_qwe', path = '/home/common_asd/aaa', expected = 'common_asd/aaa' },
    }
    for _, test in pairs(testcases) do
        t.assert_equals(strip_cwd_from_path(test.cwd, test.path), test.expected)
    end
end

g.test_module_name_from_filename = function()
    local fio = require('fio')
    local testcases = {
        { filename = 'my/modules/test1.lua', expected = 'my.modules.test1' },
        { filename = '/etc/modules/test2/init.lua', expected = 'etc.modules.test2' },
        { filename = fio.pathjoin(fio.cwd(), 'mod/test3.lua'), expected = 'mod.test3' },
        { filename = 'builtin/box/feedback_daemon.lua', expected = 'box.feedback_daemon' },
    }
    for _, test in pairs(testcases) do
        t.assert_equals(module_name_from_filename(test.filename), test.expected)
    end
end
