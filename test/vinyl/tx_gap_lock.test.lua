test_run = require('test_run').new()
txn_proxy = require('txn_proxy')

c = txn_proxy.new()
c1 = txn_proxy.new()
c2 = txn_proxy.new()
c3 = txn_proxy.new()
c4 = txn_proxy.new()
c5 = txn_proxy.new()
c6 = txn_proxy.new()

----------------------------------------------------------------
-- SELECT ALL
----------------------------------------------------------------
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')

_ = s:insert{1}
_ = s:insert{3}

c:begin()
c("s:select()") -- {1}, {3}
_ = s:insert{2} -- send c to read view
c("s:select()") -- {1}, {3}
c:commit()

s:truncate()
----------------------------------------------------------------
_ = s:insert{1}
_ = s:insert{2}

c:begin()
c("s:select()") -- {1}, {2}
_ = s:insert{3} -- send c to read view
c("s:select()") -- {1}, {2}
c:commit()

s:truncate()
----------------------------------------------------------------
_ = s:insert{2}
_ = s:insert{3}

c:begin()
c("s:select()") -- {2}, {3}
_ = s:insert{1} -- send c to read view
c("s:select()") -- {2}, {3}
c:commit()

s:truncate()
----------------------------------------------------------------
_ = s:insert{123}

c1:begin()
c2:begin()
c1("s:select({}, {iterator = 'GT'})") -- {123}
c2("s:select({}, {iterator = 'LT'})") -- {123}
_ = s:replace{123, 456} -- send c1 and c2 to read view
c1("s:select({}, {iterator = 'GT'})") -- {123}
c2("s:select({}, {iterator = 'LT'})") -- {123}
c1:commit()
c2:commit()

s:truncate()
----------------------------------------------------------------
-- SELECT GT/GE
----------------------------------------------------------------
_ = s:insert{10}
_ = s:insert{20}
_ = s:insert{30}

c1:begin()
c2:begin()
c3:begin()
c4:begin()
c5:begin()
c6:begin()
c1("s:select({10}, {iterator = 'GE'})") -- {10}, {20}, {30}
c2("s:select({10}, {iterator = 'GT'})") -- {20}, {30}
c3("s:select({15}, {iterator = 'GE'})") -- {20}, {30}
c4("s:select({15}, {iterator = 'GT'})") -- {20}, {30}
c5("s:select({25}, {iterator = 'GE'})") -- {30}
c6("s:select({30}, {iterator = 'GE'})") -- {30}
_ = s:replace{10, 1} -- send c1 to read view
c1("s:get(10)") -- {10}
c2("s:get(10)") -- {10, 1}
c3("s:get(10)") -- {10, 1}
c4("s:get(10)") -- {10, 1}
c5("s:get(10)") -- {10, 1}
c6("s:get(10)") -- {10, 1}
_ = s:replace{15, 2} -- send c2 and c3 to read view
c2("s:get(15)") -- none
c3("s:get(15)") -- none
c4("s:get(15)") -- {15, 2}
c5("s:get(15)") -- {15, 2}
c6("s:get(15)") -- {15, 2}
_ = s:replace{35, 3} -- send c4, c5, and c6 to read view
c4("s:get(35)") -- none
c5("s:get(35)") -- none
c6("s:get(35)") -- none
c1:commit()
c2:commit()
c3:commit()
c4:commit()
c5:commit()
c6:commit()

s:truncate()
----------------------------------------------------------------
-- SELECT LT/LE
----------------------------------------------------------------
_ = s:insert{10}
_ = s:insert{20}
_ = s:insert{30}

c1:begin()
c2:begin()
c3:begin()
c4:begin()
c5:begin()
c6:begin()
c1("s:select({30}, {iterator = 'LE'})") -- {30}, {20}, {10}
c2("s:select({30}, {iterator = 'LT'})") -- {20}, {10}
c3("s:select({25}, {iterator = 'LE'})") -- {20}, {10}
c4("s:select({25}, {iterator = 'LT'})") -- {20}, {10}
c5("s:select({15}, {iterator = 'LE'})") -- {10}
c6("s:select({10}, {iterator = 'LE'})") -- {10}
_ = s:replace{30, 1} -- send c1 to read view
c1("s:get(30)") -- {30}
c2("s:get(30)") -- {30, 1}
c3("s:get(30)") -- {30, 1}
c4("s:get(30)") -- {30, 1}
c5("s:get(30)") -- {30, 1}
c6("s:get(30)") -- {30, 1}
_ = s:replace{25, 2} -- send c2 and c3 to read view
c2("s:get(25)") -- none
c3("s:get(25)") -- none
c4("s:get(25)") -- {25, 2}
c5("s:get(25)") -- {25, 2}
c6("s:get(25)") -- {25, 2}
_ = s:replace{5, 3} -- send c4, c5, and c6 to read view
c4("s:get(5)") -- none
c5("s:get(5)") -- none
c6("s:get(5)") -- none
c1:commit()
c2:commit()
c3:commit()
c4:commit()
c5:commit()
c6:commit()

s:truncate()
----------------------------------------------------------------
-- SELECT LIMIT
----------------------------------------------------------------
for i = 1, 9 do s:insert{i * 10} end

c1:begin()
c2:begin()
c3:begin()
c4:begin()
c1("s:select({20}, {iterator = 'GE', limit = 3})") -- {20}, {30}, {40}
c2("s:select({80}, {iterator = 'LE', limit = 3})") -- {80}, {70}, {60}
c3("s:select({10}, {iterator = 'GE', limit = 3})") -- {10}, {20}, {30}
c4("s:select({90}, {iterator = 'LE', limit = 3})") -- {90}, {80}, {70}
_ = s:replace{50, 1}
c1("s:get(50)") -- {50, 1}
c2("s:get(50)") -- {50, 1}
c3("s:get(50)") -- {50, 1}
c4("s:get(50)") -- {50, 1}
_ = s:replace{40, 2} -- send c1 to read view
c1("s:get(40)") -- {40}
c2("s:get(40)") -- {40, 2}
c3("s:get(40)") -- {40, 2}
c4("s:get(40)") -- {40, 2}
_ = s:replace{60, 3} -- send c2 to read view
c2("s:get(60)") -- {60}
c3("s:get(60)") -- {60, 3}
c4("s:get(60)") -- {60, 3}
_ = s:replace{25, 4} -- send c3 to read view
c3("s:get(25)") -- none
c4("s:get(25)") -- {25, 4}
_ = s:replace{75, 5} -- send c4 to read view
c4("s:get(75)") -- none
c1:commit()
c2:commit()
c3:commit()
c4:commit()

s:drop()
----------------------------------------------------------------
-- SELECT EQ/REQ
----------------------------------------------------------------
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {parts = {1, 'unsigned', 2, 'unsigned'}})

_ = s:insert{1, 1}
_ = s:insert{2, 1}
_ = s:insert{2, 2}
_ = s:insert{2, 3}
_ = s:insert{3, 3}

c1:begin()
c2:begin()
c1("s:select({2}, {iterator = 'EQ'})")  -- {2, 1}, {2, 2}, {2, 3}
c2("s:select({2}, {iterator = 'REQ'})") -- {2, 3}, {2, 2}, {2, 1}
_ = s:replace{1, 10}
c1("s:select({1})") -- {1, 1}, {1, 10}
c2("s:select({1})") -- {1, 1}, {1, 10}
_ = s:replace{3, 30}
c1("s:get({3, 30})") -- {3, 30}
c2("s:get({3, 30})") -- {3, 30}
_ = s:replace{2, 20} -- send c1 and c2 to read view
c1("s:select({2}, {iterator = 'EQ'})")  -- {2, 1}, {2, 2}, {2, 3}
c2("s:select({2}, {iterator = 'REQ'})") -- {2, 3}, {2, 2}, {2, 1}
c1:commit()
c2:commit()

s:drop()
----------------------------------------------------------------
-- Interval merging
----------------------------------------------------------------
function gap_lock_count() return box.info.vinyl().tx.gap_locks end

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')

_ = s:insert{10}
_ = s:insert{20}
_ = s:insert{30}
_ = s:insert{40}

gap_lock_count() -- 0

c:begin()
c("s:select({10}, {iterator = 'GE', limit = 4})") -- locks [10, 40]
gap_lock_count() -- 1
c("s:select({15}, {iterator = 'GE', limit = 2})") -- locks [15, 30]
gap_lock_count() -- 1
c("s:select({35}, {iterator = 'LE', limit = 2})") -- locks [20, 35]
gap_lock_count() -- 1
c("s:select({5},  {iterator = 'GT', limit = 2})") -- locks  (5, 20]
gap_lock_count() -- 1
c("s:select({45}, {iterator = 'LT', limit = 2})") -- locks [30, 45)
gap_lock_count() -- 1
_ = s:insert{5}
_ = s:insert{45}
c("s:get(5)")  -- {5}
c("s:get(45)") -- {45}
_ = s:insert{25} -- send c to read view
c("s:get(25)") -- none
c:commit()

s:truncate()
----------------------------------------------------------------
_ = s:insert{10}
_ = s:insert{20}
_ = s:insert{30}
_ = s:insert{40}

gap_lock_count() -- 0

c:begin()
c("s:select({1},  {iterator = 'GT', limit = 1})") -- locks  (1, 10]
c("s:select({50}, {iterator = 'LT', limit = 1})") -- locks [40, 50)
c("s:select({20}, {iterator = 'GE', limit = 2})") -- locks [20, 30]
gap_lock_count() -- 3
c("s:select({5},  {iterator = 'GT', limit = 4})") -- locks  (5, 40]
gap_lock_count() -- 1
_ = s:insert{1}
_ = s:insert{50}
c("s:get(1)")  -- {1}
c("s:get(50)") -- {50}
_ = s:insert{5} -- send c to read view
c("s:get(5)") -- none
c:commit()

s:truncate()
----------------------------------------------------------------
_ = s:insert{100}

gap_lock_count() -- 0

c:begin()
c("s:select({100}, {iterator = 'GT'})") -- locks (100, +inf)
c("s:select({100}, {iterator = 'LT'})") -- locks (-inf, 100)
gap_lock_count() -- 2
c("s:get(100)") -- locks [100]
gap_lock_count() -- 1
_ = s:insert{1000} -- send c to read view
c("s:get(1000)") -- none
c:commit()

s:truncate()
----------------------------------------------------------------
_ = s:insert{1, 0}
_ = s:insert{2, 0}
_ = s:insert{3, 0}
_ = s:insert{4, 0}

gap_lock_count() -- 0

c:begin()
c("s:select({1}, {iterator = 'GE', limit = 2})") -- locks [1, 2]
c("s:select({2}, {iterator = 'GT', limit = 2})") -- locks (2, 4]
gap_lock_count() -- 1
c:commit()

s:drop()
----------------------------------------------------------------
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {parts = {1, 'unsigned', 2, 'unsigned'}})

gap_lock_count() -- 0

c1:begin()
c2:begin()
c3:begin()
c4:begin()
c1("s:select({100}, {iterator = 'GE'})") -- c1: locks [{100}, +inf)
c1("s:select({100, 100}, {iterator = 'GE'})") -- c1: locks [{100, 100}, +inf)
c2("s:select({100}, {iterator = 'GE'})") -- c2: locks [{100}, +inf)
c2("s:select({100, 100}, {iterator = 'GT'})") -- c2: locks ({100, 100}, +inf)
c3("s:select({100}, {iterator = 'GT'})") -- c3: locks ({100}, +inf)
c3("s:select({100, 100}, {iterator = 'GE'})") -- c3: locks [{100, 100}, +inf)
c4("s:select({100}, {iterator = 'GT'})") -- c4: locks ({100}, +inf)
c4("s:select({100, 100}, {iterator = 'GT'})") -- c4: locks ({100, 100}, +inf)
gap_lock_count() -- 4
_ = s:insert{100, 50} -- send c1 and c2 to read view
c1("s:get({100, 50})") -- none
c2("s:get({100, 50})") -- none
c3("s:get({100, 50})") -- {100, 50}
c4("s:get({100, 50})") -- {100, 50}
gap_lock_count() -- 6; new intervals: c3:[{100, 50}], c4:[{100, 50}]
_ = s:insert{100, 100} -- send c3 to read view
c3("s:get({100, 100})") -- none
c4("s:get({100, 100})") -- {100, 100}
gap_lock_count() -- 6; c4:[{100, 100}] is merged with c4:({100, 100}, +inf)
_ = s:insert{100, 101} -- send c4 to read view
c4("s:get({100, 101})") -- none
gap_lock_count() -- 6
c1:commit()
c2:commit()
c3:commit()
c4:commit()

s:truncate()
----------------------------------------------------------------
gap_lock_count() -- 0

c1:begin()
c2:begin()
c3:begin()
c4:begin()
c1("s:select({100}, {iterator = 'LE'})") -- c1: locks (-inf, {100}]
c1("s:select({100, 100}, {iterator = 'LE'})") -- c1: locks (-inf, {100, 100}]
c2("s:select({100}, {iterator = 'LE'})") -- c2: locks (-inf, {100}]
c2("s:select({100, 100}, {iterator = 'LT'})") -- c2: locks (-inf, {100, 100})
c3("s:select({100}, {iterator = 'LT'})") -- c3: locks (-inf, {100})
c3("s:select({100, 100}, {iterator = 'LE'})") -- c3: locks (-inf, {100, 100}]
c4("s:select({100}, {iterator = 'LT'})") -- c4: locks (-inf, {100})
c4("s:select({100, 100}, {iterator = 'LT'})") -- c4: locks (-inf, {100, 100})
gap_lock_count() -- 4
_ = s:insert{100, 150} -- send c1 and c2 to read view
c1("s:get({100, 150})") -- none
c2("s:get({100, 150})") -- none
c3("s:get({100, 150})") -- {100, 150}
c4("s:get({100, 150})") -- {100, 150}
gap_lock_count() -- 6; new intervals: c3:[{100, 150}], c4:[{100, 150}]
_ = s:insert{100, 100} -- send c3 to read view
c3("s:get({100, 100})") -- none
c4("s:get({100, 100})") -- {100, 100}
gap_lock_count() -- 6; c4:[{100, 100}] is merged with c4:[-inf, {100, 100})
_ = s:insert{100, 99} -- send c4 to read view
c4("s:get({100, 99})") -- none
gap_lock_count() -- 6
c1:commit()
c2:commit()
c3:commit()
c4:commit()

s:drop()
----------------------------------------------------------------
-- gh-2534: Iterator over a secondary index doesn't double track
-- results in the primary index.
----------------------------------------------------------------
s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk', {parts = {1, 'unsigned'}})
_ = s:create_index('sk', {parts = {2, 'unsigned'}})
for i = 1, 100 do s:insert{i, i} end
box.begin()
gap_lock_count() -- 0
_ = s.index.sk:select({}, {limit = 50})
gap_lock_count() -- 1
for i = 1, 100 do s.index.sk:get(i) end
gap_lock_count() -- 51
_ = s.index.sk:select()
gap_lock_count() -- 1
box.commit()
gap_lock_count() -- 0
s:drop()

gap_lock_count = nil
----------------------------------------------------------------
-- Randomized stress test
--
-- The idea behind the test is simple: execute several random
-- selects from a bunch of transactions, then insert a random
-- value to the space and check that only those transactions
-- that would actually read the new value were sent to read
-- view.
----------------------------------------------------------------
test_run:cmd("setopt delimiter ';'")

seed = os.time();
math.randomseed(seed);

INDEX_COUNT = 3;
TUPLE_COUNT = 100;
TX_COUNT = 20;
SELECTS_PER_TX = 5;
PAYLOAD_FIELD = INDEX_COUNT * 2 + 1;
MAX_VAL = {[1] = 15, [2] = 10, [3] = 5};
assert(#MAX_VAL == INDEX_COUNT);

s = box.schema.space.create('test', {engine = 'vinyl'});
for i = 1, INDEX_COUNT do
    s:create_index('i' .. i, {unique = (i == 1),
                   parts = {i * 2 - 1, 'unsigned', i * 2, 'unsigned'}})
end;

function gen_tuple(payload)
    local t = {}
    for i = 1, INDEX_COUNT do
        t[i * 2 - 1] = math.random(MAX_VAL[i])
        t[i * 2] = math.random(MAX_VAL[i])
    end
    table.insert(t, payload)
    return t
end;

function cmp_tuple(t1, t2)
    for i = 1, PAYLOAD_FIELD do
        if t1[i] ~= t2[i] then
            return t1[i] > t2[i] and 1 or -1
        end
    end
    return 0
end;

function gen_select()
    local index = math.random(INDEX_COUNT)
    local key = {}
    if math.random(100) > 10 then
        key[1] = math.random(MAX_VAL[index])
        if math.random(100) > 50 then
            key[2] = math.random(MAX_VAL[index])
        end
    end
    local iterator_types = {'EQ', 'REQ', 'LE', 'LT', 'GE', 'GT'}
    local dir = iterator_types[math.random(#iterator_types)]
    local limit = math.random(TUPLE_COUNT / 4)
    return string.format(
        "s.index['i%d']:select(%s, {iterator = '%s', limit = %d})",
        index, '{' .. table.concat(key, ', ') .. '}', dir, limit)
end;

for i = 1, TUPLE_COUNT do
    s:replace(gen_tuple())
end;

tx_list = {};
for i = 1, TX_COUNT do
    local tx = {}
    tx.conn = txn_proxy.new()
    tx.conn:begin()
    tx.selects = {}
    for j = 1, SELECTS_PER_TX do
        local cmd = gen_select()
        local result = tx.conn(cmd)[1]
        setmetatable(result, nil)
        tx.selects[j] = {cmd = cmd, result = result}
    end
    tx_list[i] = tx
end;

conflict = s:replace(gen_tuple('new'));

for i = 1, TX_COUNT do
    local tx = tx_list[i]
    tx.should_abort = false
    for j = 1, SELECTS_PER_TX do
        local sel = tx.selects[j]
        local result = loadstring('return ' .. sel.cmd)()
        if #result == #sel.result then
            for k, v in ipairs(result) do
                if cmp_tuple(v, sel.result[k]) ~= 0 then
                    tx.should_abort = true
                    break
                end
            end
        else
            tx.should_abort = true
        end
    end
end;

invalid = {};
for i = 1, TX_COUNT do
    local tx = tx_list[i]
    local v = tx.conn(string.format("s:get({%d, %d})",
                      conflict[1], conflict[2]))[1]
    local was_aborted = false
    if v == nil or v[PAYLOAD_FIELD] == nil then
        was_aborted = true
    end
    if tx.should_abort ~= was_aborted then
        table.insert(invalid, tx)
    end
    tx.conn:commit()
    tx.conn = nil
end;

#invalid == 0 or {seed = seed, conflict = conflict, invalid = invalid};

s:drop();

test_run:cmd("setopt delimiter ''");
----------------------------------------------------------------

c = nil
c1 = nil
c2 = nil
c3 = nil
c4 = nil
c5 = nil
c6 = nil
