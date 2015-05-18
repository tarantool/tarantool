import sys
import uuid
import random

def call(name, *args):
    return iproto.call(name, *args)

login = 'u'+str(uuid.uuid4())[0:8]
passw = 'p'+str(uuid.uuid4())[0:8]

sys.stdout.push_filter('box.schema.user.create.*', 'box.schema.user.create()')
sys.stdout.push_filter('box.schema.user.grant.*', 'box.schema.user.grant()')

admin("box.schema.user.create('%s', { password = '%s' })" % (login, passw))
admin("box.schema.user.grant('%s', 'execute,read,write', 'universe')" % (login))
iproto.authenticate(login, passw)
admin("function f1() return 'testing', 1, false, -1, 1.123, 1e123, nil end")
admin("f1()")
call("f1")
admin("f1=nil")
call("f1")
admin("function f1() return f1 end")
call("f1")

# A test case for https://github.com/tarantool/tarantool/issues/44
# IPROTO required!
call("dostring('box.error(33333, \"Hey!\")')")

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

call("f1", 'jason')
call("f1", 'jason', 1, 'test', 2, 'stewart')

admin("space = box.schema.create_space('tweedledum', { id = 0 })")
admin("space:create_index('primary', { type = 'hash' })")

admin("function myreplace(...) return space:replace{...} end")
admin("function myinsert(...) return space:insert{...} end")
call("myinsert", 1, 'test box delete')
call("space:delete", 1)
call("insert into t0 values (1, 'test box delete')")
call("space:delete", 1)
call("space:delete", 1)
call("insert into t0 values (2, 'test box delete')")
call("space:delete", 1)
call("space:delete", 2)
call("space:delete", 2)
admin("space:delete{2}")
call("insert into t0 values (2, 'test box delete')")
call("space:get", 2)
admin("space:delete{2}")
call("space:get", 2)
call("insert into t0 values (2, 'test box.select()')")
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
call("space:delete",2 )
call("myinsert", 3, 'old', 2)
# test that insert produces a duplicate key error
call("myinsert", 3, 'old', 2)
admin("space:update({3}, {{'=', 1, 4}, {'=', 2, 'new'}})")
call("space:get", 4)
call("space:select", 4)
admin("space:update({4}, {{'+', 3, 1}})")
admin("space:update({4}, {{'-', 3, 1}})")
call("space:get", 4)
call("space:select", 4)
admin("function field_x(key, field_index) return space:get(key)[field_index] end")
call("field_x(4, 1)", 4, 1)
call("field_x(4, 2)", 4, 1)
call("space:delete", 4)

admin("space:drop()")
sys.stdout.push_filter('box.schema.user.drop(.*)', 'box.schema.user.drop()')
admin("box.schema.user.drop('%s')" % login)
