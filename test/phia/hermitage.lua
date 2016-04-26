
--
-- hermitage: Testing transaction isolation levels.
-- github.com/ept/hermitage
-- github.com/pmwkaa/phia/blob/master/test/functional/hermitage.test.c
--
-- Testing Phia transactional isolation in Tarantool.
--  

run    = false
active = 0
space  = nil
index  = nil
T1     = nil
T2     = nil
T3     = nil
result = nil

local function stmt(channel, stmt)
	channel:put(stmt)
	while result:is_empty() do
		fiber.yield()
	end
	result:get(0)
end

function T(c)
	active = active + 1
	while run do
		if not c:is_empty() then
			local stmt = loadstring(c:get(0))
			stmt()
			result:put(true)
		end
		fiber.yield()
	end
	active = active - 1
end

function start_test(c)
	space = box.schema.space.create('tester', {engine='phia'})
	index = space:create_index('primary', {type = 'tree', parts = {1, 'num'}})
	space:replace{1, 10}
	space:replace{2, 20}
	T1 = fiber.channel(16)
	T2 = fiber.channel(16)
	T3 = fiber.channel(16)
	result = fiber.channel(16)
	active = 0
	run = true
	fiber.create(T, T1)
	fiber.create(T, T2)
	fiber.create(T, T3)
end

function end_test()
	run = false
	while active > 0 do fiber.yield() end
	space:drop()
	space = nil
	T1 = nil
	T2 = nil
	T3 = nil
	result = nil
end

---------------

function begin() box.begin() end
function commit(result) assert( pcall(box.commit) == result ) end
function rollback() box.rollback() end
function set(k, v) space:replace{k, v} end
function delete(k) space:delete{k} end
function get(k, value_to_check)
	if value_to_check == nil then
		assert( space:get{k} == nil )
		return
	end
	assert( space:get{k}[2] == value_to_check )
end

---------------

fiber = require('fiber');

function hermitage_g0()
	print("hermitage g0");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	stmt(T1, "set(1, 11)")
	stmt(T2, "set(1, 12)")
	stmt(T1, "set(2, 21)")
	stmt(T1, "commit(true)")
	stmt(T2, "set(2, 22)")
	stmt(T2, "commit(false)") -- conflict
	assert( space:get{1}[2] == 11 )
	assert( space:get{2}[2] == 21 )
	end_test()
end

function hermitage_g1a()
	print("hermitage g1a");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	stmt(T1, "set(1, 101)")
	stmt(T2, "set(1, 10)")
	stmt(T1, "rollback()")
	stmt(T2, "get(1, 10)") -- 10
	stmt(T2, "commit(true)")
	end_test()
end

function hermitage_g1b()
	print("hermitage g1b");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	stmt(T1, "set(1, 101)")
	stmt(T2, "get(1, 10)") -- 10
	stmt(T1, "set(1, 11)")
	stmt(T1, "commit(true)")
	stmt(T2, "get(1, 10)") -- 10
	stmt(T2, "commit(true)") -- ok
	end_test()
end

function hermitage_g1c()
	print("hermitage g1c");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	stmt(T1, "set(1, 11)")
	stmt(T2, "set(2, 22)")
	stmt(T1, "get(2, 20)") -- 20
	stmt(T2, "get(1, 10)") -- 10
	stmt(T1, "commit(true)") -- ok
	stmt(T2, "commit(true)") -- ok
	end_test()
end

function hermitage_otv()
	print("hermitage otv");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	stmt(T3, "begin()")
	stmt(T1, "set(1, 11)")
	stmt(T1, "set(2, 19)")
	stmt(T2, "set(1, 12)")
	stmt(T1, "commit(true)") -- ok
	stmt(T3, "get(1, 11)") -- created on first stmt (different from phia)
	stmt(T2, "set(2, 18)") 
	stmt(T3, "get(2, 19)") -- created on first stmt (different from phia)
	stmt(T2, "commit(false)") -- conflict
	stmt(T3, "get(2, 19)") --
	stmt(T3, "get(1, 11)") --
	stmt(T3, "commit(true)")
	end_test()
end

function hermitage_pmp()
	print("hermitage pmp");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	-- select * from test where value = 30
	local t = {}
	for state, v in index:pairs({}, {iterator = 'GE'}) do table.insert(t, {v[1], v[2]}) end
	stmt(T2, "set(3, 30)")
	stmt(T2, "commit(true)") -- ok
	stmt(T1, "get(1, 10)") -- 10
	stmt(T1, "get(2, 20)") -- 20
	stmt(T1, "get(3, 30)") -- 30 is visible because T1 first stmt after T2 commit (different for phia)
	stmt(T1, "commit(true)") -- ok
	end_test()
end

function hermitage_pmp_write()
	print("hermitage pmp write");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	stmt(T1, "set(1, 20)")
	stmt(T1, "set(2, 30)")
	stmt(T2, "get(1, 10)") -- 10
	stmt(T2, "get(2, 20)") -- 20
	stmt(T2, "delete(2)")
	stmt(T1, "commit(true)") -- ok
	stmt(T2, "get(1, 10)") -- 10
	stmt(T2, "commit(false)") -- conflict
	assert( space:get{1}[2] == 20 )
	assert( space:get{2}[2] == 30 )
	end_test()
end

function hermitage_p4()
	print("hermitage p4");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	stmt(T1, "get(1, 10)")
	stmt(T2, "get(1, 10)")
	stmt(T1, "set(1, 11)")
	stmt(T2, "set(1, 11)")
	stmt(T1, "commit(true)") -- ok
	stmt(T2, "commit(false)") -- conflict
	end_test()
end

function hermitage_g_single()
	print("hermitage g single");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	stmt(T1, "get(1, 10)") -- 10
	stmt(T2, "get(1, 10)") -- 10
	stmt(T2, "get(2, 20)") -- 20
	stmt(T2, "set(1, 12)")
	stmt(T2, "set(2, 18)")
	stmt(T2, "commit(true)") -- ok
	stmt(T1, "get(2, 20)")
	stmt(T1, "commit(true)") -- ok
	end_test()
end

function hermitage_g2_item()
	print("hermitage g2 item");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	stmt(T1, "get(1, 10)") -- 10
	stmt(T1, "get(2, 20)") -- 20
	stmt(T2, "get(1, 10)") -- 10
	stmt(T2, "get(2, 20)") -- 20
	stmt(T1, "set(1, 11)")
	stmt(T2, "set(1, 21)")
	stmt(T1, "commit(true)") -- ok
	stmt(T2, "commit(false)") -- conflict
	end_test()
end

function hermitage_g2()
	print("hermitage g2");
	start_test()
	stmt(T1, "begin()")
	stmt(T2, "begin()")
	-- select * from test where value % 3 = 0
	stmt(T1, "get(1, 10)") -- 10
	stmt(T1, "get(2, 20)") -- 20
	stmt(T2, "get(1, 10)") -- 10
	stmt(T2, "get(2, 20)") -- 20
	stmt(T1, "set(3, 30)")
	stmt(T2, "set(4, 42)")
	stmt(T1, "commit(true)") -- ok
	stmt(T2, "commit(false)") -- conflict
	end_test()
end

function hermitage_g2_two_edges0()
	print("hermitage g2 two edges 0");
	start_test()
	stmt(T1, "begin()")
	stmt(T1, "get(1, 10)") -- 10
	stmt(T1, "get(2, 20)") -- 20
	stmt(T2, "begin()")
	stmt(T2, "set(2, 25)")
	stmt(T2, "commit(true)") -- ok
	stmt(T3, "begin()")
	stmt(T3, "get(1, 10)") -- 10
	stmt(T3, "get(2, 25)") -- 25
	stmt(T3, "commit(true)") -- ok
	stmt(T1, "set(1, 0)")
	stmt(T1, "commit(false)")
	end_test()
end

function hermitage_g2_two_edges1()
	print("hermitage g2 two edges 1");
	start_test()
	stmt(T1, "begin()")
	stmt(T1, "get(1, 10)") -- 10
	stmt(T1, "get(2, 20)") -- 20
	stmt(T2, "begin()")
	stmt(T2, "set(2, 25)")
	stmt(T2, "commit(true)") -- ok
	stmt(T3, "begin()")
	stmt(T3, "get(1, 10)") -- 10
	stmt(T3, "get(2, 25)") -- 25
	stmt(T3, "commit(true)") -- ok
	-- stmt(T1, "set(1, 0)")
	stmt(T1, "commit(true)")
	end_test()
end
