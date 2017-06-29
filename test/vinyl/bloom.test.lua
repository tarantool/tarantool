#!/usr/bin/env tarantool

test_run = require('test_run').new()

s = box.schema.space.create('test', {engine = 'vinyl'})
_ = s:create_index('pk')

reflects = 0
function cur_reflects() return box.space.test.index.pk:info().disk.iterator.bloom.hit end
function new_reflects() local o = reflects reflects = cur_reflects() return reflects - o end
seeks = 0
function cur_seeks() return box.space.test.index.pk:info().disk.iterator.lookup end
function new_seeks() local o = seeks seeks = cur_seeks() return seeks - o end

for i = 1,1000 do s:replace{i} end
box.snapshot()
_ = new_reflects()
_ = new_seeks()

for i = 1,1000 do s:select{i} end
new_reflects() == 0
new_seeks() == 1000

for i = 1001,2000 do s:select{i} end
new_reflects() > 980
new_seeks() < 20

test_run:cmd('restart server default')

s = box.space.test

reflects = 0
function cur_reflects() return box.space.test.index.pk:info().disk.iterator.bloom.hit end
function new_reflects() local o = reflects reflects = cur_reflects() return reflects - o end
seeks = 0
function cur_seeks() return box.space.test.index.pk:info().disk.iterator.lookup end
function new_seeks() local o = seeks seeks = cur_seeks() return seeks - o end

_ = new_reflects()
_ = new_seeks()

for i = 1,1000 do s:select{i} end
new_reflects() == 0
new_seeks() == 1000

for i = 1001,2000 do s:select{i} end
new_reflects() > 980
new_seeks() < 20

s:drop()
