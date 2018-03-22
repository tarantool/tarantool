#!/usr/bin/env tarantool
--
-- pr-3204: expose space_mt, index_mt into box.schema.
--

local tap = require('tap')
local test = tap.test('schema_mt')

test:plan(7)

box.cfg{
  log="tarantool.log",
}

local sp1 = box.schema.space.create('test')
local sp2 = box.schema.space.create('test2')

test:is(
  getmetatable(sp1),
  getmetatable(sp2),
  'all spaces use the same metatable'
)

local idx1 = sp1:create_index('primary')
local idx2 = sp2:create_index('primary')

test:isnt(
  getmetatable(idx1),
  getmetatable(idx2),
  'all indexes have their own metatable'
)

test:is(
  idx1.get,
  idx2.get,
  'memtx indexes share read methods'
)

local sp3 = box.schema.space.create('test3', {engine='vinyl'})
local sp4 = box.schema.space.create('test4', {engine='vinyl'})

local idx3 = sp3:create_index('primary')
local idx4 = sp4:create_index('primary')

test:is(
  idx3.get,
  idx4.get,
  'vinyl indexes share read methods'
)

test:isnt(
  idx1.get,
  idx3.get,
  'memtx and vinyl indexes have separate read methods'
)

function box.schema.space_mt.foo() end

test:is(
  sp1.foo,
  box.schema.space_mt.foo,
  'box.schema.space_mt is mutable'
)

function box.schema.index_mt.foo() end

test:is(
  idx1.foo,
  box.schema.index_mt.foo,
  'box.schema.index_mt is mutable'
)

test:check()
os.exit(0)
