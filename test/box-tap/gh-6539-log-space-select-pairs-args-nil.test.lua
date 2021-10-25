#!/usr/bin/env tarantool

local tap = require('tap')
local test = tap.test('gh-6539-log-space-select-pairs-args-nil')
test:plan(7)
local inspector = require('test_run').new()
local fio = require('fio')

local expected_log_entry = 'C> empty or nil :%a+ call on user space with id=%d+:'
local log_file_name = 'test.log'
local grep_log_args = {nil, expected_log_entry, 512, {filename = log_file_name}}

box.cfg{log = log_file_name}

box.space._space:select()
box.space._space:pairs()
test:ok(inspector:grep_log(unpack(grep_log_args)) == nil,
        'check that log does not contain a critical entry about empty ' ..
        ':select and :pairs on a system space')

local s = box.schema.space.create('test_vinyl', {engine='vinyl'})
s:create_index('primary')

s:select{}
s:pairs{}
s:select(nil, {})
s:pairs(nil, {})
test:ok(inspector:grep_log(unpack(grep_log_args)) == nil,
        'check that log does not contain a critical entry about non-empty ' ..
        ':select and :pairs call on a vinyl user space')

s:select()
test:ok(inspector:grep_log(unpack(grep_log_args)) ~= nil,
        'check that log contains a critical entry about an empty ' ..
        ':select call on a vinyl user space')
fio.truncate(log_file_name)

s:pairs()
test:ok(inspector:grep_log(unpack(grep_log_args)) ~= nil,
        'check that log contains a critical entry about an empty ' ..
        ':pairs call on a vinyl user space')
fio.truncate(log_file_name)

s = box.schema.space.create('test_memtx')
s:create_index('primary')

s:select{}
s:pairs{}
s:select(nil, {})
s:pairs(nil, {})
test:ok(inspector:grep_log(unpack(grep_log_args)) == nil,
        'check that log does not contain a critical entry about non-empty ' ..
        ':select and :pairs call on a memtx user space')

s:select()
test:ok(inspector:grep_log(unpack(grep_log_args)) ~= nil,
        'check that log contains a critical entry about an empty ' ..
        ':select call on a memtx user space')
fio.truncate(log_file_name)

s:pairs()
test:ok(inspector:grep_log(unpack(grep_log_args)) ~= nil,
        'check that log contains a critical entry about an empty ' ..
        ':pairs call on a memtx user space')

os.exit(test:check() and 0 or 1)
