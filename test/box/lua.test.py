# encoding: utf-8
import os
import sys

admin("space = box.schema.create_space('tweedledum', { id = 0 })")
admin("space:create_index('primary', 'hash', { parts = { 0, 'num' }})")

# Test Lua from admin console. Whenever producing output,
# make sure it's a valid YAML.
admin("'  lua says: hello'")
# What's in the box?
admin("t = {} for n in pairs(box) do table.insert(t, tostring(n)) end table.sort(t)")
admin("t")
admin("t = nil")
# Test box.pack()
admin("box.pack()")
admin("box.pack(1)")
admin("box.pack('abc')")
admin("box.pack('a', ' - hello')")
admin("box.pack('Aa', ' - hello', ' world')")
admin("box.pack('w', 0x30)")
admin("box.pack('www', 0x30, 0x30, 0x30)")
admin("box.pack('www', 0x3030, 0x30)")
admin("string.byte(box.pack('w', 212345), 1, 2)")
admin("string.sub(box.pack('p', 1684234849), 2)")
admin("box.pack('p', 'this string is 45 characters long 1234567890 ')")
admin("box.pack('s', 0x4d)")
admin("box.pack('ssss', 25940, 29811, 28448, 11883)")
admin("box.pack('SSSS', 25940, 29811, 28448, 11883)")
admin("box.pack('SSSSSSSS', 28493, 29550, 27680, 27497, 29541, 20512, 29285, 8556)")
admin("box.pack('bsilww', 84, 29541, 1802444916, 2338318684567380014ULL, 103, 111)")
admin("box.unpack('b', 'T')")
admin("box.unpack('s', 'Te')")
admin("box.unpack('i', 'Test')")
admin("box.unpack('l', 'Test ok.')")
admin("box.unpack('bsil', box.pack('bsil', 255, 65535, 4294967295, tonumber64('18446744073709551615')))")
admin("box.unpack('www', box.pack('www', 255, 65535, 4294967295))")
admin("box.unpack('ppp', box.pack('ppp', 'one', 'two', 'three'))")
admin("num, str, num64 = box.unpack('ppp', box.pack('ppp', 666, 'string', tonumber64('666666666666666')))")
admin("box.unpack('i', num), str, box.unpack('l', num64)")
admin("box.unpack('=p', box.pack('=p', 1, '666'))")
admin("box.unpack('','')")
admin("box.unpack('ii', box.pack('i', 1))")
admin("box.unpack('i', box.pack('ii', 1, 1))")
admin("box.unpack('+p', box.pack('=p', 1, '666'))")

# Test the low-level box.process() call, which takes a binary packet
# and passes it to box for execution.
# insert:
admin("box.process(13, box.pack('iiippp', 0, 1, 3, 1, 'testing', 'lua rocks'))")
# select:
admin("box.process(17, box.pack('iiiiiip', 0, 0, 0, 2^31, 1, 1, 1))")
# delete:
admin("box.process(21, box.pack('iiip', 0, 1, 1, 1))")
# check delete:
admin("box.process(17, box.pack('iiiiiip', 0, 0, 0, 2^31, 1, 1, 1))")
admin("box.process(22, box.pack('iii', 0, 0, 0))")
sql("call box.process('abc', 'def')")
sql("call box.pack('test')")
sql("call box.pack('p', 'this string is 45 characters long 1234567890 ')")
sql("call box.pack('p', 'ascii symbols are visible starting from code 20')")
admin("function f1() return 'testing', 1, false, -1, 1.123, 1e123, nil end")
admin("f1()")
sql("call f1()")
admin("f1=nil")
sql("call f1()")
admin("function f1() return f1 end")
sql("call f1()")

sql("insert into t0 values (1, 'test box delete')")
sql("call box.delete('0', '\1\0\0\0')")
sql("call box.delete('0', '\1\0\0\0')")
sql("insert into t0 values (1, 'test box delete')")
admin("box.delete(0, 1)")
admin("box.delete(0, 1)")
sql("insert into t0 values ('abcd', 'test box delete')")
sql("call box.delete('0', '\1\0\0\0')")
sql("call box.delete('0', 'abcd')")
sql("call box.delete('0', 'abcd')")
sql("insert into t0 values ('abcd', 'test box delete')")
admin("box.delete(0, 'abcd')")
admin("box.delete(0, 'abcd')")
sql("call box.select('0', '0', 'abcd')")
sql("insert into t0 values ('abcd', 'test box.select()')")
sql("call box.select('0', '0', 'abcd')")
admin("box.select(0, 0, 'abcd')")
admin("box.select(0, 0)")
admin("box.select(0, 1)")
admin("box.select(0)")
sql("call box.replace('0', 'abcd', 'hello', 'world')")
sql("call box.replace('0', 'defc', 'goodbye', 'universe')")
sql("call box.select('0', '0', 'abcd')")
sql("call box.select('0', '0', 'defc')")
sql("call box.replace('0', 'abcd')")
sql("call box.select('0', '0', 'abcd')")
sql("call box.delete('0', 'abcd')")
sql("call box.delete('0', 'defc')")
sql("call box.insert('0', 'test', 'old', 'abcd')")
# test that insert produces a duplicate key error
sql("call box.insert('0', 'test', 'old', 'abcd')")
sql("call box.update('0', 'test', '=p=p', '\0\0\0\0', 'pass', 1, 'new')")
sql("call box.select('0', '0', 'pass')")
sql("call box.select_range(0, 0, 1, 'pass')")
sql("call box.update('0', 'miss', '+p', 2, '\1\0\0\0')")
sql("call box.update('0', 'pass', '+p', 2, '\1\0\0\0')")
sql("call box.update('0', 'pass', '-p', 2, '\1\0\0\0')")
sql("call box.update('0', 'pass', '-p', 2, '\1\0\0\0')")
admin("box.update(0, 'pass', '+p', 2, 1)")
sql("call box.select('0', '0', 'pass')")
admin("function field_x(space, key, field_index) return (box.select(space, 0, key))[tonumber(field_index)] end")
sql("call field_x('0', 'pass', '0')")
sql("call field_x('0', 'pass', '1')")
sql("call box.delete('0', 'pass')")
fifo_lua = os.path.abspath("box/fifo.lua")
# don't log the path name
sys.stdout.push_filter("dofile(.*)", "dofile(...)")
admin("dofile('{0}')".format(fifo_lua))
sys.stdout.pop_filter()
admin("fifomax")
admin("fifo_push('test', 1)")
admin("fifo_push('test', 2)")
admin("fifo_push('test', 3)")
admin("fifo_push('test', 4)")
admin("fifo_push('test', 5)")
admin("fifo_push('test', 6)")
admin("fifo_push('test', 7)")
admin("fifo_push('test', 8)")
admin("fifo_top('test')")
admin("box.delete(0, 'test')")
admin("fifo_top('test')")
admin("box.delete(0, 'test')")
admin("t = {} for k,v in pairs(box.cfg) do table.insert(t, k..': '..tostring(v)) end")
sys.stdout.push_filter("'reload: .*", "'reload: function_ptr'")
admin("t")
sys.stdout.pop_filter()
admin("t = {} for k,v in pairs(box.space[0]) do if type(v) ~= 'table' then table.insert(t, k..': '..tostring(v)) end end")
admin("t")
admin("box.cfg.reload()")
admin("t = {} for k,v in pairs(box.cfg) do table.insert(t, k..': '..tostring(v)) end")
sys.stdout.push_filter("'reload: .*", "'reload: function_ptr'")
admin("t")
sys.stdout.pop_filter()
admin("t = {} for k,v in pairs(box.space[0]) do if type(v) ~= 'table' then table.insert(t, k..': '..tostring(v)) end end")
admin("t")
# must be read-only
admin("box.cfg.nosuchoption = 1")
admin("box.space[300] = 1")

admin("box.index.bind('abc', 'cde')")
admin("box.index.bind(1, 2)")
admin("box.index.bind(0, 1)")
admin("box.index.bind(0, 0)")
admin("#box.index.bind(0,0)")
admin("#box.space[0].index[0].idx")
admin("box.insert(0, 'test')")
admin("box.insert(0, 'abcd')")
admin("#box.index.bind(0,0)")
admin("#box.space[0].index[0].idx")
admin("box.delete(0, 'test')")
admin("#box.index.bind(0,0)")
admin("box.delete(0, 'abcd')")
admin("#box.space[0].index[0].idx")
admin("#box.index.bind(0,0)")
admin("box.space[0]:insert('test', 'hello world')")
admin("box.space[0]:update('test', '=p', 1, 'bye, world')")
admin("box.space[0]:delete('test')")
# test tuple iterators
admin("t=box.space[0]:insert('test')")
admin("t:next('abcd')")
admin("t:next(1)")
admin("t:next(t)")
admin("t:next(t:next())")
admin("ta = {} for k, v in t:pairs() do table.insert(ta, v) end")
admin("ta")
admin("t=box.space[0]:replace('test', 'another field')")
admin("ta = {} for k, v in t:pairs() do table.insert(ta, v) end")
admin("ta")
admin("t=box.space[0]:replace('test', 'another field', 'one more')")
admin("ta = {} for k, v in t:pairs() do table.insert(ta, v) end")
admin("ta")
admin("t=box.tuple.new({'a', 'b', 'c', 'd'})")
admin("ta = {} for it,field in t:pairs() do table.insert(ta, field); end")
admin("ta")
admin("it, field = t:next()")
admin("getmetatable(it)")
admin("box.space[0]:truncate()")
admin("box.fiber.sleep(0)")
admin("box.fiber.sleep(0.01)")
admin("box.fiber.sleep(0.0001)")
admin("box.fiber.sleep('hello')")
admin("box.fiber.sleep(box, 0.001)")
admin("box.fiber.cancel(box.fiber.self())")
admin("f = box.fiber.self()")
admin("old_id = f:id()")
admin("box.fiber.cancel(f)")
admin("box.fiber.self():id() - old_id < 3")
admin("box.fiber.cancel(box.fiber.self())")
admin("box.fiber.self():id() - old_id < 5")
admin("g = box.fiber.self()")
admin("f==g")
admin("function r() f = box.fiber.create(r) return (box.fiber.resume(f)) end")
admin("r()")
admin("f = box.fiber.create(print('hello')")
admin("box.fiber.resume(f)")
# test passing arguments in and out created fiber
admin("function r(a, b) return a, b end")
admin("f=box.fiber.create(r)")
admin("box.fiber.resume(f)")
admin("f=box.fiber.create(r)")
admin("box.fiber.resume(f, 'hello')")
admin("f=box.fiber.create(r)")
admin("box.fiber.resume(f, 'hello', 'world')")
admin("f=box.fiber.create(r)")
admin("box.fiber.resume(f, 'hello', 'world', 'wide')")
admin("function y(a, b) c=box.fiber.yield(a) return box.fiber.yield(b, c) end")
admin("f=box.fiber.create(y)")
admin("box.fiber.resume(f, 'hello', 'world')")
admin("box.fiber.resume(f, 'wide')")
admin("box.fiber.resume(f)")
admin("function y() box.fiber.detach() while true do box.replace(0, 'test', os.time()) box.fiber.sleep(0.001) end end")
admin("f = box.fiber.create(y)")
admin("box.fiber.resume(f)")
admin("box.fiber.sleep(0.002)")
admin("box.fiber.cancel(f)")
admin("box.fiber.resume(f)")
admin("f=nil")
admin("for k=1, 10000, 1 do box.fiber.create(function() box.fiber.detach() end) end")
admin("collectgarbage('collect')")
# check that these newly created fibers are garbage collected
admin("box.fiber.find(900)")
admin("box.fiber.find(910)")
admin("box.fiber.find(920)")

#
# Test box.fiber.wrap()
#
# This should try to infinitely create fibers,
# but hit the fiber stack size limit and fail
# with an error.
#
admin("f = function() box.fiber.wrap(f) end")
sql("call f()")
#
# Test argument passing
#
admin("f = function(a, b) box.fiber.wrap(function(arg) result = arg end, a..b) end")
admin("f('hello ', 'world')")
admin("result")
admin("f('bye ', 'world')")
admin("result")
#
# Test that the created fiber is detached
#
admin("box.fiber.wrap(function() result = box.fiber.status() end)")
admin("result")
#
#
print """# A test case for Bug#933487
# tarantool crashed during shutdown if non running LUA fiber
# was created
#"""
admin("f = box.fiber.create(function () return true end)")
admin("box.snapshot()")
admin("box.snapshot()")
admin("box.snapshot()")
admin("box.fiber.resume(f)")
admin("f = box.fiber.create(function () return true end)")
#
#
print """#
#
#"""
admin("box.space[0]:insert('test', 'something to splice')")
admin("box.space[0]:update('test', ':p', 1, box.pack('ppp', 0, 4, 'no'))")
admin("box.space[0]:update('test', ':p', 1, box.pack('ppp', 0, 2, 'every'))")
# check an incorrect offset
admin("box.space[0]:update('test', ':p', 1, box.pack('ppp', 100, 2, 'every'))")
admin("box.space[0]:update('test', ':p', 1, box.pack('ppp', -100, 2, 'every'))")
admin("box.space[0]:truncate()")
admin("box.space[0]:insert('test', 'hello', 'october', '20th'):unpack()")
admin("box.space[0]:truncate()")
# check how well we can return tables
admin("function f1(...) return {...} end")
admin("function f2(...) return f1({...}) end")
sql("call f1('test_', 'test_')")
sql("call f2('test_', 'test_')")
sql("call f1()")
sql("call f2()")

# check multi-tuple return
admin("function f3() return {{'hello'}, {'world'}} end")
sql("call f3()")
admin("function f3() return {'hello', {'world'}} end")
sql("call f3()")
admin("function f3() return 'hello', {{'world'}, {'canada'}} end")
sql("call f3()")
admin("function f3() return {}, '123', {{}, {}} end")
sql("call f3()")
admin("function f3() return { {{'hello'}} } end")
sql("call f3()")
admin("function f3() return { box.tuple.new('hello'), {'world'} } end")
sql("call f3()")
admin("function f3() return { {'world'}, box.tuple.new('hello') } end")
sql("call f3()")

sql("call f1('jason')")
sql("call f1('jason', 1, 'test', 2, 'stewart')")
lua = """
function box.crossjoin(space0, space1, limit)
  space0 = tonumber(space0)
  space1 = tonumber(space1)
  limit = tonumber(limit)
  local result = {}
  for k0, v0 in box.space[space0]:pairs() do
    for k1, v1 in box.space[space1]:pairs() do
      if limit <= 0 then
        return unpack(result)
      end
      newtuple = {v0:unpack()}
      for _, v in v1:pairs() do table.insert(newtuple, v) end
      table.insert(result, newtuple)
      limit = limit - 1
    end
  end
  return unpack(result)
end"""
admin(lua.replace('\n', ' '))
admin("box.crossjoin(0, 0, 0)")
admin("box.crossjoin(0, 0, 10000)")
admin("box.space[0]:insert(1)")
sql("call box.crossjoin('0', '0', '10000')")
admin("box.space[0]:insert(2)")
sql("call box.crossjoin('0', '0', '10000')")
admin("box.space[0]:insert(3, 'hello')")
sql("call box.crossjoin('0', '0', '10000')")
admin("box.space[0]:insert(4, 'world')")
admin("box.space[0]:insert(5, 'hello world')")
sql("call box.crossjoin('0', '0', '10000')")
admin("box.space[0]:truncate()")
admin("box.crossjoin = nil")
print """
# A test case for Bug#901674
# No way to inspect exceptions from Box in Lua
"""
admin("pcall(box.insert, 99, 1, 'test')")
admin("pcall(box.insert, 0, 1, 'hello')")
admin("pcall(box.insert, 0, 1, 'hello')")
admin("box.space[0]:truncate()")
print """
# A test case for Bug#908094
# Lua provides access to os.execute()
"""
admin("os.execute('ls')")


print """
#
# box.fiber test (create, resume, yield, status)
#
"""

box_fiber_lua = os.path.abspath("box/box_fiber.lua")
# don't log the path name
sys.stdout.push_filter("dofile(.*)", "dofile(...)")
admin("dofile('{0}')".format(box_fiber_lua))
sys.stdout.pop_filter()

print """
# test box.fiber.status functions: invalid arguments
"""
admin("box.fiber.status(1)")
admin("box.fiber.status('fafa-gaga')")
admin("box.fiber.status(nil)")

print """
# run fiber's test
"""
admin("box_fiber_run_test()")
# Testing 64bit
admin("tonumber64(123)")
admin("tonumber64('123')")
admin("type(tonumber64('123')) == 'cdata'")
admin("tonumber64('9223372036854775807') == tonumber64('9223372036854775807')")
admin("tonumber64('9223372036854775807') - tonumber64('9223372036854775800')")
admin("tonumber64('18446744073709551615') == tonumber64('18446744073709551615')")
admin("tonumber64('18446744073709551615') + 1")
admin("tonumber64(-1)")
admin("tonumber64('184467440737095516155')")
admin("string.byte(box.pack('p', tonumber64(123)))")
# test delete field
admin("box.space[0]:truncate()")
sql("call box.insert('0', 'tes1', 'tes2', 'tes3', 'tes4', 'tes5')")
sql("call box.update('0', 'tes1', '#p', 0, '')")
sql("call box.update('0', 'tes2', '#p', 0, '')")
sql("call box.update('0', 'tes3', '#p', 0, '')")
sql("call box.update('0', 'tes4', '#p', 0, '')")
admin("box.update(0, 'tes5', '#p', 0, '')")
admin("box.space[0]:truncate()")

# test delete multiple fields
admin("box.insert(0, 0, 1, 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 12, 13, 14, 15)")
admin("box.update(0, 0, '#p', 42, 1)")
admin("box.update(0, 0, '#p', 3, 'abirvalg')")
admin("box.update(0, 0, '#p#p#p', 1, 1, 3, 2, 5, 1)")
admin("box.update(0, 0, '#p', 3, 3)")
admin("box.update(0, 0, '#p', 4, 123456)")
admin("box.update(0, 0, '#p', 2, 4294967295)")
admin("box.update(0, 0, '#p', 1, 0)")
admin("box.space[0]:truncate()")

print """
# test box.update: INSERT field
"""
admin("box.insert(0, 1, 3, 6, 9)")
admin("box.update(0, 1, '!p', 1, 2)")
admin("box.update(0, 1, '!p!p!p!p', 3, 4, 3, 5, 4, 7, 4, 8)")
admin("box.update(0, 1, '!p!p!p', 9, 10, 9, 11, 9, 12)")
admin("box.space[0]:truncate()")
admin("box.insert(0, 1, 'tuple')")
admin("box.update(0, 1, '#p!p=p', 1, '', 1, 'inserted tuple', 2, 'set tuple')")
admin("box.space[0]:truncate()")
admin("box.insert(0, 1, 'tuple')")
admin("box.update(0, 1, '=p!p#p', 1, 'set tuple', 1, 'inerted tuple', 2, '')")
admin("box.update(0, 1, '!p!p', 0, 3, 0, 2)")
admin("box.space[0]:truncate()")
print """
# Test for Bug #955226
# Lua Numbers are passed back wrongly as strings
#
"""
admin("function foo() return 1, 2, '1', '2' end")
sql("call foo()")


print """
# test update's assign opearations
"""
admin("box.replace(0, 1, 'field string value')")
admin("box.update(0, 1, '=p=p=p', 1, 'new field string value', 2, 42, 3, 0xdeadbeef)")

print """
# test update's arith opearations
"""
admin("box.update(0, 1, '+p&p|p^p', 2, 16, 3, 0xffff0000, 3, 0x0000a0a0, 3, 0xffff00aa)")

print """
# test update splice operation
"""
admin("ops_list = {}")
admin("table.insert(ops_list, box.upd.splice(1, 0, 3, 'the newest'))")
admin("box.update(0, 1, ':p', 1, box.pack('ppp', 0, 3, 'the newest'))")

print """
# test update delete operations
"""
admin("box.update(0, 1, '#p#p', 3, '', 2, '')")

print """
# test update insert operations
"""
admin("box.update(0, 1, '!p!p!p!p', 1, 1, 1, 2, 1, 3, 1, 4)")

admin("box.space[0]:truncate()")


print """
#
# test that ffi extension is inaccessible
#
"""

admin("ffi")


print """
#
# Lua init lua script test
#
"""

print """
# Load testing init lua script
"""
server.stop()
server.deploy(init_lua="box/test_init.lua")

print """
# Test asscess to box configuration
"""
sys.stdout.push_filter("'reload = .*", "'reload = function_ptr'")
admin("print_config()")
sys.stdout.pop_filter()

print """
# Test bug #977898
"""
# Run a dummy insert to avoid race conditions under valgrind
admin("box.insert(0, 4, 8, 16)")

print """
# Test insert from init.lua
"""
admin("box.select(0, 0, 1)")
admin("box.select(0, 0, 2)")
admin("box.select(0, 0, 4)")

print """
# Test bug #1002272
"""
admin("floor(0.5)")
admin("floor(0.9)")
admin("floor(1.1)")

print """
# clean-up after tests
"""
server.stop()
server.deploy(init_lua=None)

admin("box.insert(box.schema.SPACE_ID, 0, 0, 'tweedledum')")
admin("box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')")

print """
# Test box.tuple:slice()
"""
admin("t=box.tuple.new({'0', '1', '2', '3', '4', '5', '6', '7'})")
admin("t:slice(0)")
admin("t:slice(-1)")
admin("t:slice(1)")
admin("t:slice(-1, -1)")
admin("t:slice(-1, 1)")
admin("t:slice(1, -1)")
admin("t:slice(1, 3)")
admin("t:slice(7)")
admin("t:slice(8)")
admin("t:slice(9)")
admin("t:slice(100500)")
admin("t:slice(9, -1)")
admin("t:slice(6, -1)")
admin("t:slice(4, 4)")
admin("t:slice(6, 4)")
admin("t:slice(0, 0)")
admin("t:slice(9, 10)")
admin("t:slice(-7)")
admin("t:slice(-8)")
admin("t:slice(-9)")
admin("t:slice(-100500)")
admin("t:slice(500, 700)")
admin("box.space[0]:truncate()")

print """
# A test case for Bug#911641 box.fiber.sleep() works incorrectly if
# a fiber is attached.
"""
admin("function r() return box.fiber.sleep(0.01) end")
admin("f = box.fiber.create(r)")
admin("box.fiber.resume(f)")
admin("box.fiber.resume(f)")
admin("function r() box.fiber.yield(box.space[0]:insert(0, 0, 1)) box.fiber.yield(box.space[0]:select(0, 0)) box.fiber.yield(box.space[0]:truncate()) end")
admin("f = box.fiber.create(r)")
admin("box.fiber.resume(f)")
admin("box.fiber.resume(f)")
admin("box.fiber.resume(f)")
admin("box.fiber.resume(f)")
admin("function r() return box.fiber.yield(box.fiber.create(r)) end")
admin("f = r()")
admin("f1 = box.fiber.resume(f)")
admin("f2 = box.fiber.resume(f1)")
admin("f3 = box.fiber.resume(f2)")
admin("f4 = box.fiber.resume(f3)")
admin("f5 = box.fiber.resume(f4)")
admin("f6 = box.fiber.resume(f5)")
admin("f7 = box.fiber.resume(f6)")
admin("f8 = box.fiber.resume(f7)")
admin("f9 = box.fiber.resume(f8)")
admin("f10 = box.fiber.resume(f9)")
admin("f11 = box.fiber.resume(f10)")
admin("f12 = box.fiber.resume(f11)")
admin("f13 = box.fiber.resume(f12)")
admin("f14 = box.fiber.resume(f13)")
admin("f15 = box.fiber.resume(f14)")
admin("f16 = box.fiber.resume(f15)")
admin("f17 = box.fiber.resume(f16)")
admin("box.fiber.resume(f)")
admin("box.fiber.resume(f1)")
admin("box.fiber.resume(f2)")
admin("box.fiber.resume(f3)")
admin("box.fiber.resume(f4)")
admin("box.fiber.resume(f5)")
admin("box.fiber.resume(f6)")
admin("box.fiber.resume(f7)")
admin("box.fiber.resume(f8)")
admin("box.fiber.resume(f9)")
admin("box.fiber.resume(f10)")
admin("box.fiber.resume(f11)")
admin("box.fiber.resume(f12)")
admin("box.fiber.resume(f13)")
admin("box.fiber.resume(f14)")
admin("box.fiber.resume(f15)")
admin("box.fiber.resume(f16)")
admin("f17 = nil")
admin("function r() box.fiber.detach() box.fiber.sleep(1000) end")
admin("f = box.fiber.create(r)")
admin("box.fiber.resume(f)")
admin("box.fiber.resume(f)")
admin("box.fiber.cancel(f)")
admin("box.fiber.resume(f)")

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
sql("call f()")
sql("call test.f()")
sql("call test.test.f()")
print """
# A test case for box.counter
"""
admin("box.counter.inc(0, 1)")
admin("box.select(0, 0, 1)")
admin("box.counter.inc(0, 1)")
admin("box.counter.inc(0, 1)")
admin("box.select(0, 0, 1)")
admin("box.counter.dec(0, 1)")
admin("box.counter.dec(0, 1)")
admin("box.select(0, 0, 1)")
admin("box.counter.dec(0, 1)")
admin("box.select(0, 0, 1)")


print """# box.dostring()"""
admin("box.dostring('abc')")
admin("box.dostring('abc=2')")
admin("box.dostring('return abc')")
admin("box.dostring('return ...', 1, 2, 3)")

print """# box.update: push/pop fields"""
admin("box.insert(0, 'abcd')")
admin("box.update(0, 'abcd', '#p', 1, '')")
admin("box.update(0, 'abcd', '=p', -1, 'push1')")
admin("box.update(0, 'abcd', '=p', -1, 'push2')")
admin("box.update(0, 'abcd', '=p', -1, 'push3')")
admin("box.update(0, 'abcd', '#p=p', 1, '', -1, 'swap1')")
admin("box.update(0, 'abcd', '#p=p', 1, '', -1, 'swap2')")
admin("box.update(0, 'abcd', '#p=p', 1, '', -1, 'swap3')")
admin("box.update(0, 'abcd', '#p=p', -1, '', -1, 'noop1')")
admin("box.update(0, 'abcd', '#p=p', -1, '', -1, 'noop2')")
admin("box.update(0, 'abcd', '#p=p', -1, '', -1, 'noop3')")
admin("box.space[0]:truncate()")

print """# A test case for Bug#1043804 lua error() -> server crash"""
admin("error()")
print """# Test box.fiber.name()"""
admin("old_name = box.fiber.name()")
admin("box.fiber.name() == old_name")
admin("box.fiber.self():name() == old_name")
admin("box.fiber.name('hello fiber')")
admin("box.fiber.name()")
admin("box.fiber.self():name('bye fiber')")
admin("box.fiber.self():name()")
admin("box.fiber.self():name(old_name)")

print """# A test case for bitwise operations """
admin("bit.lshift(1, 32)")
admin("bit.band(1, 3)")
admin("bit.bor(1, 2)")

print """# A test case for Bug#1061747 'tonumber64 is not transitive'"""
admin("tonumber64(tonumber64(2))")
admin("tostring(tonumber64(tonumber64(3)))")

print """# box.tuple.new test"""
admin("box.tuple.new()")
admin("box.tuple.new(1)")
admin("box.tuple.new('string')")
admin("box.tuple.new(tonumber64('18446744073709551615'))")
admin("box.tuple.new({tonumber64('18446744073709551615'), 'string', 1})")
print """# A test case for the key as an tuple"""
admin("t=box.insert(0, 777, '0', '1', '2', '3')")
admin("t")
admin("box.replace(0, t)")
admin("box.replace(0, 777, { 'a', 'b', 'c', {'d', 'e', t}})")
print """# A test case for tuple:totable() method"""
admin("t=box.select(0, 0, 777):totable()")
admin("t[2], t[3], t[4], t[5]")
admin("box.space[0]:truncate()")
print """# A test case for Bug#1119389 '(lbox_tuple_index) crashes on 'nil' argument'"""
admin("t=box.insert(0, 8989)")
admin("t[nil]")
print """# A test case for Bug#1131108 'tonumber64 from negative int inconsistency'"""
admin("tonumber64(-1)")
admin("tonumber64(-1LL)")
admin("tonumber64(-1ULL)")
admin("-1")
admin("-1LL")
admin("-1ULL")
admin("tonumber64(-1.0)")
admin("6LL - 7LL")
print """# A test case for Bug#1131108 'incorrect conversion from boolean lua value to tarantool tuple'
"""
admin("function bug1075677() local range = {} table.insert(range, 1>0) return range end")
sql("call bug1075677()")
admin("bug1075677=nil")
admin("box.tuple.new(false)")
admin("box.tuple.new({false})")


admin("t = box.tuple.new('abc')")
admin("t")
admin("t:bsize()")
admin("box.delete(0, 8989)", silent=True)

admin("box.space[0]:drop()")

sys.stdout.clear_all_filters()
