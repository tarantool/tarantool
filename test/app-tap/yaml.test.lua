#!/usr/bin/env tarantool

package.path = "lua/?.lua;"..package.path

local tap = require('tap')
local common = require('serializer_test')

local function is_map(s)
    return s:match("---[\n ]%w+%:") or s:match("---[\n ]{%w+%:")
end

local function is_array(s)
    return s:match("---[\n ]%[") or s:match("---[\n ]- ");
end

local function test_compact(test, s)
    test:plan(9)

    local ss = s.new()
    ss.cfg{encode_load_metatables = true, decode_save_metatables = true}

    test:is(ss.encode({10, 15, 20}), "---\n- 10\n- 15\n- 20\n...\n",
        "block array")
    test:is(ss.encode(setmetatable({10, 15, 20}, { __serialize="array"})),
        "---\n- 10\n- 15\n- 20\n...\n", "block array")
    test:is(ss.encode(setmetatable({10, 15, 20}, { __serialize="sequence"})),
        "---\n- 10\n- 15\n- 20\n...\n", "block array")
    test:is(ss.encode({setmetatable({10, 15, 20}, { __serialize="seq"})}),
        "---\n- [10, 15, 20]\n...\n", "flow array")
    test:is(getmetatable(ss.decode(ss.encode({10, 15, 20}))).__serialize, "seq",
        "decoded __serialize is seq")

    test:is(ss.encode({k = 'v'}), "---\nk: v\n...\n", "block map")
    test:is(ss.encode(setmetatable({k = 'v'}, { __serialize="mapping"})),
        "---\nk: v\n...\n", "block map")
    test:is(ss.encode({setmetatable({k = 'v'}, { __serialize="map"})}),
        "---\n- {'k': 'v'}\n...\n", "flow map")
    test:is(getmetatable(ss.decode(ss.encode({k = 'v'}))).__serialize, "map",
        "decoded __serialize is map")
end

local function test_output(test, s)
    test:plan(17)
    test:is(s.encode({true}), '---\n- true\n...\n', "encode for true")
    test:is(s.decode("---\nyes\n..."), true, "decode for 'yes'")
    test:is(s.encode({false}), '---\n- false\n...\n', "encode for false")
    test:is(s.decode("---\nno\n..."), false, "decode for 'no'")
    test:is(s.encode({s.NULL}), '---\n- null\n...\n', "encode for nil")
    test:is(s.decode("---\n~\n..."), s.NULL, "decode for ~")
    test:is(s.encode("\x80\x92\xe8s\x16"), '--- !!binary gJLocxY=\n...\n',
        "encode for binary")
    test:is(s.encode("\x08\x5c\xc2\x80\x12\x2f"), '--- !!binary CFzCgBIv\n...\n',
        "encode for binary (2) - gh-354")
    test:is(s.encode("\xe0\x82\x85\x00"), '--- !!binary 4IKFAA==\n...\n',
        "encode for binary (3) - gh-1302")
    -- gh-4090: some printable unicode characters displayed as byte sequences.
    -- The following tests ensures that various 4-byte encoded unicode characters
    -- displayed as expected.
    test:is(s.encode("\xF0\x9F\x86\x98"), '--- 🆘\n...\n', "encode - gh-4090 (1)")
    test:is(s.encode("\xF0\x9F\x84\xBD"), '--- 🄽\n...\n', "encode - gh-4090 (2)")
    test:is(s.encode("\xF0\x9F\x85\xA9"), '--- 🅩\n...\n', "encode - gh-4090 (3)")
    test:is(s.encode("\xF0\x9F\x87\xA6"), '--- 🇦\n...\n', "encode - gh-4090 (4)")
    test:is(s.encode("\xF0\x9F\x88\xB2"), '--- 🈲\n...\n', "encode - gh-4090 (5)")
    -- gh-883: console can hang tarantool process
    local t = {}
    for i=0x8000,0xffff,1 do
        table.insert(t, require('pickle').pack( 'i', i ));
    end
    local _, count = string.gsub(s.encode(t), "!!binary", "")
    test:is(count, 30880, "encode for binary (4) - gh-883")
    test:is(s.encode("фЫр!"), '--- фЫр!\n...\n',
        "encode for utf-8")

    test:is(s.encode("Tutorial -- Header\n====\n\nText"),
        "--- |-\n  Tutorial -- Header\n  ====\n\n  Text\n...\n", "tutorial string");
end

local function test_tagged(test, s)
    test:plan(17)
    --
    -- Test encoding tags.
    --
    local prefix = 'tag:tarantool.io/push,2018'
    local _, err = pcall(s.encode, 200, {tag_handle = true, tag_prefix = 100})
    test:isnt(err:find('Usage'), nil, "encode usage")
    _, err = pcall(s.encode, 100, {tag_handle = 'handle'})
    test:isnt(err:find('Usage'), nil, "encode usage, no prefix")
    _, err = pcall(s.encode, 100, {tag_prefix = 'prefix'})
    test:isnt(err:find('Usage'), nil, "encode usage, no handle")
    local ret
    ret, err = s.encode(300, {tag_handle = '!push', tag_prefix = prefix})
    test:is(ret, nil, 'non-usage and non-oom errors do not raise')
    test:is(err, "tag handle must end with '!'", "encode usage")
    ret = s.encode(300, {tag_handle = '!push!', tag_prefix = prefix})
    test:is(ret, "%TAG !push! "..prefix.."\n--- 300\n...\n", "encode usage")
    ret = s.encode({a = 100, b = 200}, {tag_handle = '!print!', tag_prefix = prefix})
    test:is(ret, "%TAG !print! tag:tarantool.io/push,2018\n---\na: 100\nb: 200\n...\n", 'encode usage')
    --
    -- Test decoding tags.
    --
    _, err = pcall(s.decode)
    test:isnt(err:find('Usage'), nil, "decode usage")
    _, err = pcall(s.decode, false)
    test:isnt(err:find('Usage'), nil, "decode usage")
    local handle, prefix = s.decode(ret, {tag_only = true})
    test:is(handle, "!print!", "handle is decoded ok")
    test:is(prefix, "tag:tarantool.io/push,2018", "prefix is decoded ok")
    local several_tags =
[[%TAG !tag1! tag:tarantool.io/tag1,2018
%TAG !tag2! tag:tarantool.io/tag2,2018
---
- 100
...
]]
    local ok, err = s.decode(several_tags, {tag_only = true})
    test:is(ok, nil, "can not decode multiple tags")
    test:is(err, "can not decode multiple tags", "same")
    local no_tags = s.encode(100)
    handle, prefix = s.decode(no_tags, {tag_only = true})
    test:is(handle, nil, "no tag - no handle")
    test:is(prefix, nil, "no tag - no prefix")
    local several_documents =
[[
%TAG !tag1! tag:tarantool.io/tag1,2018
--- 1
...

%TAG !tag2! tag:tarantool.io/tag2,2018
--- 2
...

]]
    handle, prefix = s.decode(several_documents, {tag_only = true})
    test:is(handle, "!tag1!", "tag handle on multiple documents")
    test:is(prefix, "tag:tarantool.io/tag1,2018",
            "tag prefix on multiple documents")
end

local function test_api(test, s)
    local encode_usage = 'Usage: encode(object, {tag_prefix = <string>, ' ..
        'tag_handle = <string>})'
    local decode_usage = 'Usage: yaml.decode(document, [{tag_only = boolean}])'

    local cases = {
        {
            'encode: no args',
            func = s.encode,
            args = {},
            exp_err = encode_usage,
        },
        {
            'encode: wrong opts',
            func = s.encode,
            args = {{}, 1},
            exp_err = encode_usage,
        },
        {
            'encode: wrong tag_prefix',
            func = s.encode,
            args = {{}, {tag_prefix = 1}},
            exp_err = encode_usage,
        },
        {
            'encode: wrong tag_handle',
            func = s.encode,
            args = {{}, {tag_handle = 1}},
            exp_err = encode_usage,
        },
        {
            'encode: nil opts',
            func = s.encode,
            args = {{}, nil},
            args_len = 2,
            exp_err = nil,
        },
        {
            'decode: no args',
            func = s.decode,
            args = {},
            exp_err = decode_usage,
        },
        {
            'decode: wrong input',
            func = s.decode,
            args = {true},
            exp_err = decode_usage,
        },
        {
            'decode: wrong opts',
            func = s.decode,
            args = {'', 1},
            exp_err = decode_usage,
        },
        {
            'decode: nil opts',
            func = s.decode,
            args = {'', nil},
            args_len = 2,
            exp_err = nil,
        },
    }

    test:plan(#cases)

    for _, case in ipairs(cases) do
        local args_len = case.args_len or table.maxn(case.args)
        local ok, err = pcall(case.func, unpack(case.args, 1, args_len))
        if case.exp_err == nil then
            test:ok(ok, case[1])
        else
            test:is_deeply({ok, err}, {false, case.exp_err}, case[1])
        end
    end
end

tap.test("yaml", function(test)
    local serializer = require('yaml')
    test:plan(12)
    test:test("unsigned", common.test_unsigned, serializer)
    test:test("signed", common.test_signed, serializer)
    test:test("double", common.test_double, serializer)
    test:test("boolean", common.test_boolean, serializer)
    test:test("string", common.test_string, serializer)
    test:test("nil", common.test_nil, serializer)
    test:test("table", common.test_table, serializer, is_array, is_map)
    test:test("ucdata", common.test_ucdata, serializer)
    test:test("compact", test_compact, serializer)
    test:test("output", test_output, serializer)
    test:test("tagged", test_tagged, serializer)
    test:test("api", test_api, serializer)
end)
