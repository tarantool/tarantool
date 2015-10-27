--------------------------------------------------------------------------------
-- # luafun integration
--------------------------------------------------------------------------------

space = box.schema.space.create('tweedledum')
index = space:create_index('primary', { type = 'hash' })
for i = 1,5,1 do space:replace({i, i}) end

fun = require('fun')
env = require('test_run')
test_run = env.new()

-- print all methods from metatable
methods = fun.iter(getmetatable(fun.range(5)).__index):totable()
table.sort(methods)
methods

-- iter on arrays
fun.iter({1, 2, 3}):totable()
fun.iter({2, 4, 6, 8}):all(function(x) return x % 2 == 1 end)

-- iter on hashes
fun.iter({a = 1, b = 2, c = 3}):tomap()

-- iter on tuple
fun.iter(box.tuple.new({1, 2, 3}):pairs()):totable()

-- iter on space (using __ipairs)
function pred(t) return t[1] % 2 == 0 end
fun.iter(space):totable()
fun.iter(space:pairs()):totable()
space:pairs():filter(pred):drop(2):take(3):totable()

-- iter on index (using __ipairs)
fun.iter(space.index[0]):totable()
fun.iter(space.index[0]:pairs()):totable()
space.index[0]:pairs():drop(2):take(3):totable()

-- test global functions
test_run:cmd("setopt delimiter ';'")
fun.reduce(function(acc, val) return acc + val end, 0,
    fun.filter(function(x) return x % 11 == 0 end,
    fun.map(function(x) return 2 * x end, fun.range(1000))));

test_run:cmd("setopt delimiter ''");

t = {}
fun.foreach(function(x) table.insert(t, x) end, "abcde")
t

space:drop()
