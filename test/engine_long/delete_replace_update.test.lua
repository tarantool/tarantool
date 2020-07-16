delete_replace_update = require('suite').delete_replace_update
engine_name = 'memtx'
iterations = 100000

math.randomseed(1)
delete_replace_update(engine_name, iterations)

math.randomseed(2)
delete_replace_update(engine_name, iterations)

math.randomseed(3)
delete_replace_update(engine_name, iterations)

math.randomseed(4)
delete_replace_update(engine_name, iterations)

math.randomseed(5)
delete_replace_update(engine_name, iterations)
