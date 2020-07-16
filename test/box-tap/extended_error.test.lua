#! /usr/bin/env tarantool

local netbox = require('net.box')
local os = require('os')
local tap = require('tap')

box.cfg{
    listen = os.getenv('LISTEN')
}

--
-- gh-4398: error objects after transmission through network
-- should not loose their fields and type.
--

-- Create AccessDeniedError. It is going to be used in the tests
-- below.
function forbidden_function()
    return nil
end
local user = box.session.user()
box.session.su('admin')
box.schema.func.create('forbidden_function')
box.session.su('guest')
local tmp = box.func.forbidden_function
local access_denied_error
local _
_, access_denied_error = pcall(tmp.call, tmp)
box.session.su('admin')
box.schema.func.drop('forbidden_function')
box.session.su(user)

local test = tap.test('Error marshaling')
test:plan(12)

function error_new(...)
    return box.error.new(...)
end

function error_throw(...)
    box.error(error_new(...))
end

function error_new_stacked(args1, args2)
    local e1 = box.error.new(args1)
    local e2 = box.error.new(args2)
    e1:set_prev(e2)
    return e1
end

function error_throw_stacked(...)
    box.error(error_new_stacked(...))
end

function error_access_denied()
    return access_denied_error
end

function error_throw_access_denied()
    box.error(access_denied_error)
end

local function check_error(err, check_list)
    assert(type(check_list) == 'table')
    if type(err.trace) ~= 'table' or err.trace[1] == nil or
       err.trace[1].file == nil or err.trace[1].line == nil then
        return false
    end
    for k, v in pairs(check_list) do
        if err[k] ~= v then
            return false
        end
    end
    return true
end

box.schema.user.grant('guest', 'super')
local c = netbox.connect(box.cfg.listen)
c:eval('box.session.settings.error_marshaling_enabled = true')
local args = {{code = 1000, reason = 'Reason'}}
local err = c:call('error_new', args)
local checks = {
    code = 1000,
    message = 'Reason',
    base_type = 'ClientError',
    type = 'ClientError',
}
test:ok(check_error(err, checks), "ClientError marshaling")
_, err = pcall(c.call, c, 'error_throw', args)
test:ok(check_error(err, checks), "ClientError marshaling in iproto fields")

args = {{code = 1001, reason = 'Reason2', type = 'MyError'}}
err = c:call('error_new', args)
checks = {
    code = 1001,
    message = 'Reason2',
    base_type = 'CustomError',
    type = 'MyError',
}
test:ok(check_error(err, checks), "CustomError marshaling")
_, err = pcall(c.call, c, 'error_throw', args)
test:ok(check_error(err, checks), "CustomError marshaling in iproto fields")

err = c:call('error_access_denied')
checks = {
    code = 42,
    type = 'AccessDeniedError',
    base_type = 'AccessDeniedError',
    message = "Execute access to function 'forbidden_function' is denied for user 'guest'",
    object_type = 'function',
    object_name = 'forbidden_function',
    access_type = 'Execute',
}
test:ok(check_error(err, checks), "AccessDeniedError marshaling")
_, err = pcall(c.call, c, 'error_throw_access_denied')
test:ok(check_error(err, checks), "AccessDeniedError marshaling in iproto fields")

args = {
    {code = 1003, reason = 'Reason3', type = 'MyError2'},
    {code = 1004, reason = 'Reason4'}
}
err = c:call('error_new_stacked', args)
local err1 = err
local err2 = err.prev
test:isnt(err2, nil, 'Stack is received')
local checks1 = {
    code = 1003,
    message = 'Reason3',
    base_type = 'CustomError',
    type = 'MyError2'
}
test:ok(check_error(err1, checks1), "First error in the stack")
local checks2 = {
    code = 1004,
    message = 'Reason4',
    base_type = 'ClientError',
    type = 'ClientError'
}
test:ok(check_error(err2, checks2), "Second error in the stack")

_, err = pcall(c.call, c, 'error_throw_stacked', args)
err1 = err
err2 = err.prev
test:isnt(err2, nil, 'Stack is received via iproto fields')
test:ok(check_error(err1, checks1), "First error in the stack in iproto fields")
test:ok(check_error(err2, checks2), "Second error in the stack in iproto fields")

c:close()
box.schema.user.revoke('guest', 'super')

os.exit(test:check() and 0 or 1)
