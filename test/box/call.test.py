import os
import sys

def call(name, *args):
    return iproto.call(name, *args)

admin("box.schema.user.create('test', { password = 'test' })")
admin("box.schema.user.grant('test', 'execute,read,write', 'universe')")
iproto.authenticate('test', 'test')
# workaround for gh-770 centos 6 float representation
admin('exp_notation = 1e123')
admin("function f1() return 'testing', 1, false, -1, 1.123, math.abs(exp_notation - 1e123) < 0.1, nil end")
admin("f1()")
call("f1")
admin("f1=nil")
call("f1")
admin("function f1() return f1 end")
call("f1")

# A test case for https://github.com/tarantool/tarantool/issues/44
# IPROTO required!
call("box.error", 33333, 'Hey!')

print """
# A test case for Bug#103491
# server CALL processing bug with name path longer than two
# https://bugs.launchpad.net/tarantool/+bug/1034912
"""
admin("f = function() return 'OK' end")
admin("test = {}")
admin("test.f = f")
admin("test.test = {}")
admin("test.test.f = f")
call("f")
call("test.f")
call("test.test.f")

print """
# Test for Bug #955226
# Lua Numbers are passed back wrongly as strings
#
"""
admin("function foo() return 1, 2, '1', '2' end")
call("foo")

#
# check how well we can return tables
#
admin("function f1(...) return {...} end")
admin("function f2(...) return f1({...}) end")
call("f1", 'test_', 'test_')
call("f2", 'test_', 'test_')
call("f1")
call("f2")
#
# check multi-tuple return
#
admin("function f3() return {{'hello'}, {'world'}} end")
call("f3")
admin("function f3() return {'hello', {'world'}} end")
call("f3")
admin("function f3() return 'hello', {{'world'}, {'canada'}} end")
call("f3")
admin("function f3() return {}, '123', {{}, {}} end")
call("f3")
admin("function f3() return { {{'hello'}} } end")
call("f3")
admin("function f3() return { box.tuple.new('hello'), {'world'} } end")
call("f3")
admin("function f3() return { {'world'}, box.tuple.new('hello') } end")
call("f3")
admin("function f3() return { { test={1,2,3} }, { test2={1,2,3} } } end")
call("f3")

call("f1", 'jason')
call("f1", 'jason', 1, 'test', 2, 'stewart')

admin("space = box.schema.space.create('tweedledum', { id = 0 })")
admin("index = space:create_index('primary', { type = 'hash' })")

admin("function myreplace(...) return space:replace{...} end")
admin("function myinsert(...) return space:insert{...} end")

call("myinsert", 1, 'test box delete')
call("space:delete", 1)
call("myinsert", 1, 'test box delete')
call("space:delete", 1)
call("space:delete", 1)
call("myinsert", 2, 'test box delete')
call("space:delete", 1)
call("space:delete", 2)
call("space:delete", 2)
admin("space:delete{2}")

call("myinsert", 2, 'test box delete')
call("space:get", 2)
admin("space:delete{2}")
call("space:get", 2)
call("myinsert", 2, 'test box.select()')
call("space:get", 2)
call("space:select", 2)
admin("space:get{2}")
admin("space:select{2}")
admin("space:get{1}")
admin("space:select{1}")
call("myreplace", 2, 'hello', 'world')
call("myreplace", 2, 'goodbye', 'universe')
call("space:get", 2)
call("space:select", 2)
admin("space:get{2}")
admin("space:select{2}")
call("myreplace", 2)
call("space:get", 2)
call("space:select", 2)
call("space:delete", 2)
call("space:delete", 2)
call("myinsert", 3, 'old', 2)
# test that insert produces a duplicate key error
call("myinsert", 3, 'old', 2)
admin("space:update({3}, {{'=', 1, 4}, {'=', 2, 'new'}})")
admin("space:insert(space:get{3}:update{{'=', 1, 4}, {'=', 2, 'new'}}) space:delete{3}")
call("space:get", 4)
call("space:select", 4)
admin("space:update({4}, {{'+', 3, 1}})")
admin("space:update({4}, {{'-', 3, 1}})")
call("space:get", 4)
call("space:select", 4)
admin("function field_x(key, field_index) return space:get(key)[field_index] end")
call("field_x", 4, 1)
call("field_x", 4, 2)
call("space:delete", 4)
admin("space:drop()")

admin("space = box.schema.space.create('tweedledum')")
admin("index = space:create_index('primary', { type = 'tree' })")


def lua_eval(name, *args):
    print 'eval (%s)(%s)' % (name, ','.join([ str(arg) for arg in args]))
    print '---'
    print iproto.py_con.eval(name, args)

def lua_call(name, *args):
    print 'call %s(%s)' % (name, ','.join([ str(arg) for arg in args]))
    print '---'
    print iproto.py_con.call(name, args)

def test(expr, *args):
    lua_eval('return ' + expr, *args)
    admin('function f(...) return ' + expr + ' end')
    lua_call('f', *args)

# Return values
test("1")
test("1, 2, 3")
test("true")
test("nil")
test("")
test("{}")
test("{1}")
test("{1, 2, 3}")
test("{k1 = 'v1', k2 = 'v2'}")
test("{k1 = 'v1', k2 = 'v2'}")
# gh-791: maps are wrongly assumed to be arrays
test("{s = {1, 1428578535}, u = 1428578535, v = {}, c = {['2'] = {1, 1428578535}, ['106'] = { 1, 1428578535} }, pc = {['2'] = {1, 1428578535, 9243}, ['106'] = {1, 1428578535, 9243}}}")
test("true, {s = {1, 1428578535}, u = 1428578535, v = {}, c = {['2'] = {1, 1428578535}, ['106'] = { 1, 1428578535} }, pc = {['2'] = {1, 1428578535, 9243}, ['106'] = {1, 1428578535, 9243}}}")
test("{s = {1, 1428578535}, u = 1428578535, v = {}, c = {['2'] = {1, 1428578535}, ['106'] = { 1, 1428578535} }, pc = {['2'] = {1, 1428578535, 9243}, ['106'] = {1, 1428578535, 9243}}}, true")
admin("t = box.tuple.new('tuple', {1, 2, 3}, { k1 = 'v', k2 = 'v2'})")
test("t")
test("t, t, t")
test("{t}")
test("{t, t, t}")
test("error('exception')")
test("box.error(0)")
test('...')
test('...', 1, 2, 3)
test('...',  None, None, None)
test('...', { 'k1': 'v1', 'k2': 'v2'})
# Transactions
test('space:auto_increment({"transaction"})')
test('space:select{}')
test('box.begin(), space:auto_increment({"failed"}), box.rollback()')
test('space:select{}')
test('require("fiber").sleep(0)')
# Other
lua_eval('!invalid expression')

admin("space:drop()")
admin("box.schema.user.drop('test')")

# Re-connect after removing user
iproto.py_con.close()
