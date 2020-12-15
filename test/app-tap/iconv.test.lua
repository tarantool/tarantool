#!/usr/bin/env tarantool

local tap   = require('tap')
local iconv = require('iconv')

local test = tap.test("iconv")
test:plan(11)

local simple_str  = 'ascii string'
local cyrillic_str = 'русский текст'

local c_ascii_8 = iconv.new('ASCII', 'UTF-8')
local c_8_ascii = iconv.new('UTF-8', 'ASCII')

test:is(c_ascii_8(simple_str), simple_str, 'check ascii->utf8 on simple string')
test:is(c_8_ascii(simple_str), simple_str, 'check utf8->ascii on simple string')

local c16be_8 = iconv.new('UTF-16BE', 'UTF-8')
local c8_16be = iconv.new('UTF-8', 'UTF-16BE')
test:is(c16be_8(c8_16be(simple_str)),  simple_str,
        'UTF conversion with ascii string')
test:is(c8_16be(c16be_8(cyrillic_str)), cyrillic_str,
        'UTF conversion with non-ascii symbols')

local c16_16be = iconv.new('UTF-16', 'UTF-16BE')
local c1251_16 = iconv.new('WINDOWS-1251', 'UTF-16')
local c8_1251  = iconv.new('UTF-8', 'WINDOWS-1251')

test:is(c8_16be(c16be_8(cyrillic_str)), cyrillic_str,
        'UTF conversion with non-ascii symbols')

-- test complex converting path
test:is(c8_1251(c1251_16(c16_16be(c16be_8(cyrillic_str)))), cyrillic_str,
        'complex multi-format conversion')

-- test huge string
local huge_str = string.rep(cyrillic_str, 50)

test:is(c16be_8(c8_16be(huge_str)), huge_str, "huge string")

local stat, err = pcall(iconv.new, 'NOT EXISTS', 'UTF-8')
test:is(stat, false, 'error was thrown on bad encoding')
test:ok(err:match('Invalid') ~= nil, 'correct error')

local stat, err = pcall(c_ascii_8, cyrillic_str)
test:is(stat, false, 'error was thrown on sequence')
test:ok(err:match('Incomplete multibyte sequence') ~= nil, 'correct error')

os.exit(test:check() == true and 0 or 1)
