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

g.test_popen_is_closed = function()
    local ph = popen.shell('true')

    -- Check that status can be seen if 'is_closed()' returns 'false'.
    t.assert_equals(ph:is_closed(), false)
    t.assert_equals(type(ph.status.state), 'string')
    t.assert_equals(type(ph:info().status.state), 'string')

    -- Check the error description for an invalid argument to the 'is_closed()'
    -- when the handle is alive.
    local exp_err = {
        type = 'IllegalParams',
        message = 'Bad params, use: ph:is_closed()',
    }
    t.assert_error_covers(exp_err, ph.is_closed)

    -- Check that 'is_closed()' works properly when handle is closed.
    t.assert_equals(ph:close(), true)
    t.assert_equals(ph:is_closed(), true)

    -- Check the error description for an invalid argument to the 'is_closed()'
    -- when the handle is closed.
    t.assert_error_covers(exp_err, ph.is_closed)

    -- Check that status cannot be seen if 'is_closed()' returns 'true'.
    exp_err = {
        type = 'IllegalParams',
        message = 'Attempt to index a closed popen handle',
    }
    t.assert_error_covers(exp_err, ph.__index, ph, 'status')

    exp_err = {
        type = 'IllegalParams',
        message = 'popen: attempt to operate on a closed handle',
    }
    t.assert_error_covers(exp_err, ph.info, ph)
end
