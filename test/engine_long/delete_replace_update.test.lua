el_mod = require("suite")

engine_name = 'memtx'
iterations = 100000

math.randomseed(1)
el_mod.delete_replace_update(engine_name, iterations)

math.randomseed(2)
el_mod.delete_replace_update(engine_name, iterations)

math.randomseed(3)
el_mod.delete_replace_update(engine_name, iterations)

math.randomseed(4)
el_mod.delete_replace_update(engine_name, iterations)

math.randomseed(5)
el_mod.delete_replace_update(engine_name, iterations)
