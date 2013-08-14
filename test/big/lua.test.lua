dofile('index_random_test.lua')

box.space[1]:insert('brave', 'new', 'world')
box.space[1].index[1]:min()
box.space[1].index[1]:max()
box.select('1', '1', 'new', 'world')

-- A test case for Bug #904208
-- "assert failed, when key cardinality is greater than index cardinality"
--  https://bugs.launchpad.net/tarantool/+bug/904208

box.select('1', '1', 'new', 'world', 'order')
box.delete('1', 'brave')

-- A test case for Bug #902091
-- "Positioned iteration over a multipart index doesn't work"
-- https://bugs.launchpad.net/tarantool/+bug/902091

box.space[1]:insert('item 1', 'alabama', 'song')
box.select(1, 1, 'alabama')
box.space[1]:insert('item 2', 'california', 'dreaming ')
box.space[1]:insert('item 3', 'california', 'uber alles')
box.space[1]:insert('item 4', 'georgia', 'on my mind')
iter, tuple = box.space[1].index[1]:next('california')
tuple
_, tuple = box.space[1].index[1]:next(iter)
tuple
box.delete('1', 'item 1')
box.delete('1', 'item 2')
box.delete('1', 'item 3')
box.delete('1', 'item 4')

--
-- Check range scan over multipart keys
--
box.space[5]:insert('01234567', 'new', 'world')
box.space[5]:insert('00000000', 'of', 'puppets')
box.space[5]:insert('00000001', 'of', 'might', 'and', 'magic')
box.select_range(5, 1, 2, 'of')
box.select_reverse_range(5, 1, 2, 'of')
box.space[5]:truncate()

--
-- Lua 64bit numbers support
--
box.insert('8', tonumber64('18446744073709551615'), 'magic')
tu = box.select('8', '0', tonumber64('18446744073709551615'))
num = box.unpack('l', tu[0])
num
type(num) == 'cdata'
num == tonumber64('18446744073709551615')
num = box.unpack('l', tu[0])
num == tonumber64('18446744073709551615')
box.delete(8, 18446744073709551615ULL)
box.insert('8', 125ULL, 'magic')
tu = box.select('8', '0', 125)
tu2 = box.select('8', '0', 125LL)
num = box.unpack('l', tu[0])
num2 = box.unpack('l', tu2[0])
num, num2
type(num) == 'cdata'
type(num2) == 'cdata'
num == tonumber64('125')
num2 == tonumber64('125')
box.space[8]:truncate()

--
-- Lua select_reverse_range
--
box.insert(14, 0, 0)
box.insert(14, 1, 0)
box.insert(14, 2, 0)
box.insert(14, 3, 0)
box.insert(14, 4, 0)
box.insert(14, 5, 0)
box.insert(14, 6, 0)
box.insert(14, 7, 0)
box.insert(14, 8, 0)
box.insert(14, 9, 0)
box.select_range(14, 1, 10)
box.select_reverse_range(14, 1, 10)
box.select_reverse_range(14, 1, 4)
box.space[14]:truncate()

--
-- Tests for box.index iterators
--
pid = 1
tid = 999
-- setopt delimiter ';'
for sid = 1, 2 do
    for i = 1, 3 do
        box.space[16]:insert('pid_'..pid, 'sid_'..sid, 'tid_'..tid)
        pid = pid + 1
        tid = tid - 1
    end
end;
-- setopt delimiter ''

for k, v in box.space[16].index[1].next,       box.space[16].index[1], 'sid_1' do print(' - ', v) end
for k, v in box.space[16].index[1].prev,       box.space[16].index[1], 'sid_2' do print(' - ', v) end
for k, v in box.space[16].index[1].next_equal, box.space[16].index[1], 'sid_1' do print(' - ', v) end
for k, v in box.space[16].index[1].prev_equal, box.space[16].index[1], 'sid_1' do print(' - ', v) end
for k, v in box.space[16].index[1].next_equal, box.space[16].index[1], 'sid_2' do print(' - ', v) end
for k, v in box.space[16].index[1].prev_equal, box.space[16].index[1], 'sid_2' do print(' - ', v) end
box.space[16]:truncate()

--
-- Tests for lua idx:count()
--
box.insert(17, 1, 1, 1)
box.insert(17, 2, 2, 0)
box.insert(17, 3, 2, 1)
box.insert(17, 4, 3, 0)
box.insert(17, 5, 3, 1)
box.insert(17, 6, 3, 2)
box.space[17].index[1]:count(1)
box.space[17].index[1]:count(2)
box.space[17].index[1]:count(2, 1)
box.space[17].index[1]:count(2, 2)
box.space[17].index[1]:count(3)
box.space[17].index[1]:count(3, 3)
box.space[17].index[1]:count()
box.space[17]:truncate()

--
-- Tests for lua box.auto_increment
--
box.space[18]:truncate()
box.auto_increment(18, 'a')
box.insert(18, 5)
box.auto_increment(18, 'b')
box.auto_increment(18, 'c')
box.space[18]:truncate()

--
-- Tests for lua box.auto_increment with NUM64 keys
--
box.space[25]:truncate()
box.auto_increment(25, 'a')
box.insert(25, tonumber64(5))
box.auto_increment(25, 'b')
box.auto_increment(25, 'c')
box.space[25]:truncate()

--
-- Tests for lua tuple:transform()
--
t = box.insert(12, '1', '2', '3', '4', '5', '6', '7')
t:transform(7, 0, '8', '9', '100')
t:transform(0, 1)
t:transform(1, 4)
t:transform(-1, 1)
t:transform(-3, 2)
t:transform(0, 0, 'A')
t:transform(-1, 0, 'A')
t:transform(0, 1, 'A')
t:transform(-1, 1, 'B')
t:transform(0, 2, 'C')
t:transform(2, 0, 'hello')
t:transform(0, -1, 'C')
t:transform(0, 100)
t:transform(-100, 1)
t:transform(0, 3, 1, 2, 3)
t:transform(3, 1, tonumber64(4))
t:transform(0, 1, {})

--
-- Tests for lua tuple:find() and tuple:findall()
--
t = box.insert(12, 'A', '2', '3', '4', '3', '2', '5', '6', '3', '7')
t:find('2')
t:find('4')
t:find('5')
t:find('A')
t:find('0')

t:findall('A')
t:findall('2')
t:findall('3')
t:findall('0')

t:find(2, '2')
t:find(89, '2')
t:findall(4, '3')

t = box.insert(12, 'Z', '2', 2, 3, tonumber64(2))
t:find(2)
t:find(tonumber64(2))
t:find('2')
box.space[12]:truncate()

-- A test case for Bug #1038784
-- transform returns wrong tuple and put broken reply into socket
-- https://bugs.launchpad.net/tarantool/+bug/1038784
dofile('push.lua')

push_collection(0, 1038784, 'hello')
push_collection(0, 1038784, 'hello')
push_collection(0, 1038784, 'hello')

push_collection(1, 1038784, 'hi')
push_collection(2, 1038784, 'hi')
push_collection(2, 1038784, 'hi')

push_collection(5, 1038784, 'hey')
push_collection(5, 1038784, 'hey')
push_collection(5, 1038784, 'hey')
push_collection(5, 1038784, 'hey')

--
-- A test case for Bug#1060967: truncation of 64-bit numbers
--
box.space[5]:insert(2^51, 'hello', 'world')
box.space[5]:select(0, 2^51)
box.space[5]:truncate()

--
-- Test that we print index number in error ER_INDEX_VIOLATION
--
box.space[1]:insert(1, 'hello', 'world')
box.space[1]:insert(2, 'hello', 'world')
box.space[1]:truncate()

-- A test case for Bug #1042798
-- Truncate hangs when primary key is not in linear or starts at the first field
-- https://bugs.launchpad.net/tarantool/+bug/1042798

-- Print key fields in pk
for k, f in pairs(box.space[23].index[0].key_field) do print(k,  ' => ', f.fieldno) end
box.insert(23, 1, 2, 3, 4)
box.insert(23, 10, 20, 30, 40)
box.insert(23, 20, 30, 40, 50)
for _, v in box.space[23]:pairs() do print(' - ', v) end

-- Truncate must not hang
box.space[23]:truncate()

-- Empty result
for _, v in box.space[23]:pairs() do print(' - ', v) end

-------------------------------------------------------------------------------
-- TreeIndex::random()
-------------------------------------------------------------------------------

index_random_test(26, 0)

-------------------------------------------------------------------------------
-- HashIndex::random()
-------------------------------------------------------------------------------

index_random_test(26, 1)

-- vim: tabstop=4 expandtab shiftwidth=4 softtabstop=4 syntax=lua
