test_run = require('test_run')
delete_insert = require('suite').delete_insert
inspector = test_run.new()
engine = inspector:get_cfg('engine')
iterations = 100
math.randomseed(1)
delete_insert(engine, iterations)
