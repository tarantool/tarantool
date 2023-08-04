local buffer = require('buffer')
local ffi = require('ffi')
local popen = require('popen')
local t = require('luatest')

local g = t.group()

local TARANTOOL_PATH = arg[-1]

ffi.cdef([[
    int pipe(int pipefd[2]);
]])

g.test_inherit_fds = function()
    t.assert_error_msg_equals(
        'popen.new: wrong parameter "opts.inherit_fds": ' ..
        'expected table or nil, got number',
        popen.new, {TARANTOOL_PATH}, {inherit_fds = 0})
    t.assert_error_msg_equals(
        'popen.new: wrong parameter "opts.inherit_fds": ' ..
        'expected table or nil, got string',
        popen.new, {TARANTOOL_PATH}, {inherit_fds = 'foo'})
    t.assert_error_msg_equals(
        'popen.new: wrong parameter "opts.inherit_fds[1]": ' ..
        'expected nonnegative integer, got string',
        popen.new, {TARANTOOL_PATH}, {inherit_fds = {'foo'}})
    t.assert_error_msg_equals(
        'popen.new: wrong parameter "opts.inherit_fds[2]": ' ..
        'expected nonnegative integer, got number',
        popen.new, {TARANTOOL_PATH}, {inherit_fds = {0, 1.5}})
    t.assert_error_msg_equals(
        'popen.new: wrong parameter "opts.inherit_fds[3]": ' ..
        'expected nonnegative integer, got number',
        popen.new, {TARANTOOL_PATH}, {inherit_fds = {0, 1, 1e100}})
    t.assert_error_msg_equals(
        'popen.new: wrong parameter "opts.inherit_fds[3]": ' ..
        'expected nonnegative integer, got number',
        popen.new, {TARANTOOL_PATH}, {inherit_fds = {0, 1, -1e100}})
    t.assert_error_msg_equals(
        'popen.new: wrong parameter "opts.inherit_fds[4]": ' ..
        'expected nonnegative integer, got number',
        popen.new, {TARANTOOL_PATH}, {inherit_fds = {0, 1, 2, -1}})

    local fds = ffi.new('int[2]')
    t.assert_equals(ffi.C.pipe(fds), 0)

    local msg = 'foobar'
    local script = string.format([[require('ffi').C.write(%d, '%s', %d)]],
                                 fds[1], msg, #msg)
    local ph = popen.new({TARANTOOL_PATH, '-e', script},
                         {close_fds = true, inherit_fds = {fds[1]}})
    t.assert_equals(ffi.C.close(fds[1]), 0)
    t.assert_covers(ph:wait(), {exit_code = 0})

    local ibuf = buffer.ibuf()
    t.assert_equals(ffi.C.read(fds[0], ibuf:reserve(#msg), #msg), #msg)
    t.assert_equals(ffi.C.close(fds[0]), 0)
    t.assert_equals(ffi.string(ibuf.rpos, #msg), msg)
    ibuf:recycle()
end
