#!/usr/bin/env tarantool

local tap = require('tap')
local buffer = require('buffer')
local msgpackffi = require('msgpackffi')
local digest = require('digest')
local key_def_lib = require('key_def')
local merger = require('merger')
local fiber = require('fiber')
local utf8 = require('utf8')
local ffi = require('ffi')
local fun = require('fun')

-- A chunk size for table and buffer sources. A chunk size for
-- tuple source is always 1.
local FETCH_BLOCK_SIZE = 10

local function merger_new_usage(param)
    local msg = 'merger.new(key_def, ' ..
        '{source, source, ...}[, {' ..
        'reverse = <boolean> or <nil>}])'
    if not param then
        return ('Bad params, use: %s'):format(msg)
    else
        return ('Bad param "%s", use: %s'):format(param, msg)
    end
end

local function merger_select_usage(param)
    local msg = 'merge_source:select([{' ..
                'buffer = <cdata<struct ibuf>> or <nil>, ' ..
                'limit = <number> or <nil>}])'
    if not param then
        return ('Bad params, use: %s'):format(msg)
    else
        return ('Bad param "%s", use: %s'):format(param, msg)
    end
end

-- Get buffer with data encoded without last 'trunc' bytes.
local function truncated_msgpack_buffer(data, trunc)
    local data = msgpackffi.encode(data)
    data = data:sub(1, data:len() - trunc)
    local len = data:len()
    local buf = buffer.ibuf()
    -- Ensure we have enough buffer to write len + trunc bytes.
    buf:reserve(len + trunc)
    local p = buf:alloc(len)
    -- Ensure len bytes follows with trunc zero bytes.
    ffi.copy(p, data .. string.rep('\0', trunc), len + trunc)
    return buf
end

local function truncated_msgpack_source(data, trunc)
    local buf = truncated_msgpack_buffer(data, trunc)
    return merger.new_source_frombuffer(buf)
end

local bad_source_new_calls = {
    {
        'Bad fetch iterator',
        funcs = {'new_buffer_source', 'new_table_source',
                 'new_tuple_source'},
        params = {1},
        exp_err = '^Usage: merger%.[a-z_]+%(gen, param, state%)$',
    },
    {
        'Bad chunk type',
        funcs = {'new_source_frombuffer', 'new_source_fromtable'},
        params = {1},
        exp_err = '^Usage: merger%.[a-z_]+%(<.+>%)$',
    },
    {
        'Bad buffer chunk',
        funcs = {'new_source_frombuffer'},
        params = {ffi.new('char *')},
        exp_err = '^Usage: merger%.[a-z_]+%(<cdata<struct ibuf>>%)$',
    },
}

local bad_chunks = {
    {
        'Bad buffer source chunk (not cdata)',
        func = 'new_buffer_source',
        chunk = 1,
        exp_err = 'Expected <state>, <buffer>',
    },
    {
        'Bad buffer source chunk (wrong ctype)',
        func = 'new_buffer_source',
        chunk = ffi.new('char *'),
        exp_err = 'Expected <state>, <buffer>',
    },
    {
        'Bad table source chunk',
        func = 'new_table_source',
        chunk = 1,
        exp_err = 'Expected <state>, <table>',
    },
    {
        'Bad tuple source chunk (not cdata)',
        func = 'new_tuple_source',
        chunk = 1,
        exp_err = 'A tuple or a table expected, got number',
    },
    {
        'Bad tuple source chunk (wrong ctype)',
        func = 'new_tuple_source',
        chunk = ffi.new('char *'),
        exp_err = 'A tuple or a table expected, got cdata',
    },
}

local bad_merger_new_calls = {
    {
        'Bad opts',
        sources = {},
        opts = 1,
        exp_err = merger_new_usage(nil),
    },
    {
        'Bad opts.reverse',
        sources = {},
        opts = {reverse = 1},
        exp_err = merger_new_usage('reverse'),
    },
}

local bad_merger_select_calls = {
    {
        'Wrong source of table type',
        sources = {merger.new_source_fromtable({1})},
        opts = nil,
        exp_err = 'A tuple or a table expected, got number',
    },
    {
        'Bad msgpack source: wrong length of the tuples array',
        -- Remove the last tuple from msgpack data, but keep old
        -- tuples array size.
        sources = {
            truncated_msgpack_source({{''}, {''}, {''}}, 2),
        },
        opts = {},
        exp_err = 'Unexpected msgpack buffer end',
    },
    {
        'Bad msgpack source: wrong length of a tuple',
        -- Remove half of the last tuple, but keep old tuple size.
        sources = {
            truncated_msgpack_source({{''}, {''}, {''}}, 1),
        },
        opts = {},
        exp_err = 'Unexpected msgpack buffer end',
    },
    {
        'Bad opts.buffer (wrong type)',
        sources = {},
        opts = {buffer = 1},
        exp_err = merger_select_usage('buffer'),
    },
    {
        'Bad opts.buffer (wrong cdata type)',
        sources = {},
        opts = {buffer = ffi.new('char *')},
        exp_err = merger_select_usage('buffer'),
    },
    {
        'Bad opts.limit (wrong type)',
        sources = {},
        opts = {limit = 'hello'},
        exp_err = merger_select_usage('limit'),
    }
}

local schemas = {
    {
        name = 'small_unsigned',
        parts = {
            {
                fieldno = 2,
                type = 'unsigned',
            }
        },
        gen_tuple = function(tupleno)
            return {'id_' .. tostring(tupleno), tupleno}
        end,
    },
    -- Test with N-1 equal parts and Nth different.
    {
        name = 'many_parts',
        parts = (function()
            local parts = {}
            for i = 1, 16 do
                parts[i] = {
                    fieldno = i,
                    type = 'unsigned',
                }
            end
            return parts
        end)(),
        gen_tuple = function(tupleno)
            local tuple = {}
            -- 15 constant parts
            for i = 1, 15 do
                tuple[i] = i
            end
            -- 16th part is varying
            tuple[16] = tupleno
            return tuple
        end,
        -- reduce tuple count to decrease test run time
        tuple_count = 16,
    },
    -- Test null value in nullable field of an index.
    {
        name = 'nullable',
        parts = {
            {
                fieldno = 1,
                type = 'unsigned',
            },
            {
                fieldno = 2,
                type = 'string',
                is_nullable = true,
            },
        },
        gen_tuple = function(i)
            if i % 1 == 1 then
                return {0, tostring(i)}
            else
                return {0, box.NULL}
            end
        end,
    },
    -- Test index part with 'collation_id' option (as in net.box's
    -- response).
    {
        name = 'collation_id',
        parts = {
            {
                fieldno = 1,
                type = 'string',
                collation_id = 2, -- unicode_ci
            },
        },
        gen_tuple = function(i)
            local letters = {'a', 'b', 'c', 'A', 'B', 'C'}
            if i <= #letters then
                return {letters[i]}
            else
                return {''}
            end
        end,
    },
    -- Test index part with 'collation' option (as in local index
    -- parts).
    {
        name = 'collation',
        parts = {
            {
                fieldno = 1,
                type = 'string',
                collation = 'unicode_ci',
            },
        },
        gen_tuple = function(i)
            local letters = {'a', 'b', 'c', 'A', 'B', 'C'}
            if i <= #letters then
                return {letters[i]}
            else
                return {''}
            end
        end,
    },
}

local function is_unicode_ci_part(part)
    return part.collation_id == 2 or part.collation == 'unicode_ci'
end

local function tuple_comparator(a, b, parts)
    for _, part in ipairs(parts) do
        local fieldno = part.fieldno
        if a[fieldno] ~= b[fieldno] then
            if a[fieldno] == nil then
                return -1
            end
            if b[fieldno] == nil then
                return 1
            end
            if is_unicode_ci_part(part) then
                return utf8.casecmp(a[fieldno], b[fieldno])
            end
            return a[fieldno] < b[fieldno] and -1 or 1
        end
    end

    return 0
end

local function sort_tuples(tuples, parts, opts)
    local function tuple_comparator_wrapper(a, b)
        local cmp = tuple_comparator(a, b, parts)
        if cmp < 0 then
            return not opts.reverse
        elseif cmp > 0 then
            return opts.reverse
        else
            return false
        end
    end

    table.sort(tuples, tuple_comparator_wrapper)
end

local function lowercase_unicode_ci_fields(tuples, parts)
    for i = 1, #tuples do
        local tuple = tuples[i]
        for _, part in ipairs(parts) do
            if is_unicode_ci_part(part) then
                -- Workaround #3709.
                if tuple[part.fieldno]:len() > 0 then
                    tuple[part.fieldno] = utf8.lower(tuple[part.fieldno])
                end
            end
        end
    end
end

local function fetch_source_gen(param, state)
    local input_type = param.input_type
    local tuples = param.tuples
    local last_pos = state.last_pos
    local fetch_block_size = FETCH_BLOCK_SIZE
    -- A chunk size is always 1 for a tuple source.
    if input_type == 'tuple' then
        fetch_block_size = 1
    end
    local data = fun.iter(tuples):drop(last_pos):take(
        fetch_block_size):totable()
    if #data == 0 then
        return
    end
    local new_state = {last_pos = last_pos + #data}
    if input_type == 'table' then
        return new_state, data
    elseif input_type == 'buffer' then
        local buf = buffer.ibuf()
        msgpackffi.internal.encode_r(buf, data, 0)
        return new_state, buf
    elseif input_type == 'tuple' then
        assert(#data <= 1)
        if #data == 0 then return end
        return new_state, data[1]
    else
        assert(false)
    end
end

local function fetch_source_iterator(input_type, tuples)
    local param = {
        input_type = input_type,
        tuples = tuples,
    }
    local state = {
        last_pos = 0,
    }
    return fetch_source_gen, param, state
end

local function prepare_data(schema, tuple_count, source_count, opts)
    local opts = opts or {}
    local input_type = opts.input_type
    local use_table_as_tuple = opts.use_table_as_tuple
    local use_fetch_source = opts.use_fetch_source

    local tuples = {}
    local exp_result = {}

    -- Ensure empty sources are empty table and not nil.
    for i = 1, source_count do
        if tuples[i] == nil then
            tuples[i] = {}
        end
    end

    -- Prepare N tables with tuples as input for merger.
    for i = 1, tuple_count do
        -- [1, source_count]
        local guava = digest.guava(i, source_count) + 1
        local tuple = schema.gen_tuple(i)
        table.insert(exp_result, tuple)
        if not use_table_as_tuple then
            assert(input_type ~= 'buffer')
            tuple = box.tuple.new(tuple)
        end
        table.insert(tuples[guava], tuple)
    end

    -- Sort tuples within each source.
    for _, source_tuples in pairs(tuples) do
        sort_tuples(source_tuples, schema.parts, opts)
    end

    -- Sort expected result.
    sort_tuples(exp_result, schema.parts, opts)

    -- Fill sources.
    local sources
    if use_fetch_source then
        sources = {}
        for i = 1, source_count do
            local func = ('new_%s_source'):format(input_type)
            sources[i] = merger[func](fetch_source_iterator(input_type,
                tuples[i]))
        end
    elseif input_type == 'table' then
        -- Imitate netbox's select w/o {buffer = ...}.
        sources = {}
        for i = 1, source_count do
            sources[i] = merger.new_source_fromtable(tuples[i])
        end
    elseif input_type == 'buffer' then
        -- Imitate netbox's select with {buffer = ...}.
        sources = {}
        for i = 1, source_count do
            local buf = buffer.ibuf()
            sources[i] = merger.new_source_frombuffer(buf)
            msgpackffi.internal.encode_r(buf, tuples[i], 0)
        end
    elseif input_type == 'tuple' then
        assert(false)
    else
        assert(false)
    end

    return sources, exp_result
end

local function test_case_opts_str(opts)
    local params = {}

    if opts.input_type then
        table.insert(params, 'input_type: ' .. opts.input_type)
    end

    if opts.output_type then
        table.insert(params, 'output_type: ' .. opts.output_type)
    end

    if opts.reverse then
        table.insert(params, 'reverse')
    end

    if opts.use_table_as_tuple then
        table.insert(params, 'use_table_as_tuple')
    end

    if opts.use_fetch_source then
        table.insert(params, 'use_fetch_source')
    end

    if next(params) == nil then
        return ''
    end

    return (' (%s)'):format(table.concat(params, ', '))
end

local function run_merger(test, schema, tuple_count, source_count, opts)
    fiber.yield()

    local opts = opts or {}

    -- Prepare data.
    local sources, exp_result = prepare_data(schema, tuple_count, source_count,
                                             opts)

    -- Create a merger instance.
    local merger_inst = merger.new(schema.key_def, sources,
        {reverse = opts.reverse})

    local res

    -- Run merger and prepare output for compare.
    if opts.output_type == 'table' then
        -- Table output.
        res = merger_inst:select()
    elseif opts.output_type == 'buffer' then
        -- Buffer output.
        local output_buffer = buffer.ibuf()
        merger_inst:select({buffer = output_buffer})
        res = msgpackffi.decode(output_buffer.rpos)
    else
        -- Tuple output.
        assert(opts.output_type == 'tuple')
        res = merger_inst:pairs():totable()
    end

    -- A bit more postprocessing to compare.
    for i = 1, #res do
        if type(res[i]) ~= 'table' then
            res[i] = res[i]:totable()
        end
    end

    -- unicode_ci does not differentiate btw 'A' and 'a', so the
    -- order is arbitrary. We transform fields with unicode_ci
    -- collation in parts to lower case before comparing.
    lowercase_unicode_ci_fields(res, schema.parts)
    lowercase_unicode_ci_fields(exp_result, schema.parts)

    test:is_deeply(res, exp_result,
        ('check order on %3d tuples in %4d sources%s')
        :format(tuple_count, source_count, test_case_opts_str(opts)))
end

local function run_case(test, schema, opts)
    local opts = opts or {}

    local case_name = ('testing on schema %s%s'):format(
        schema.name, test_case_opts_str(opts))
    local tuple_count = schema.tuple_count or 100

    local input_type = opts.input_type
    local use_table_as_tuple = opts.use_table_as_tuple
    local use_fetch_source = opts.use_fetch_source

    -- Skip meaningless flags combinations.
    if input_type == 'buffer' and not use_table_as_tuple then
        return
    end
    if input_type == 'tuple' and not use_fetch_source then
        return
    end

    test:test(case_name, function(test)
        test:plan(4)

        -- Check with small buffer count.
        run_merger(test, schema, tuple_count, 1, opts)
        run_merger(test, schema, tuple_count, 2, opts)
        run_merger(test, schema, tuple_count, 5, opts)

        -- Check more buffers then tuple count.
        run_merger(test, schema, tuple_count, 128, opts)
    end)
end

local test = tap.test('merger')
test:plan(#bad_source_new_calls + #bad_chunks + #bad_merger_new_calls +
    #bad_merger_select_calls + 6 + #schemas * 48)

-- For collations.
box.cfg{}

for _, case in ipairs(bad_source_new_calls) do
    test:test(case[1], function(test)
        local funcs = case.funcs
        test:plan(#funcs)
        for _, func in ipairs(funcs) do
            local ok, err = pcall(merger[func], unpack(case.params))
            test:ok(ok == false and err:match(case.exp_err), func)
        end
    end)
end

for _, case in ipairs(bad_chunks) do
    local source = merger[case.func](function(_, state)
        return state, case.chunk
    end, {}, {})
    local ok, err = pcall(function()
        return source:pairs():take(1):totable()
    end)
    test:ok(ok == false and err:match(case.exp_err), case[1])
end

-- Create the key_def for the test cases below.
local key_def = key_def_lib.new({{
    fieldno = 1,
    type = 'string',
}})

-- Bad merger.new() calls.
for _, case in ipairs(bad_merger_new_calls) do
    local ok, err = pcall(merger.new, key_def, case.sources, case.opts)
    err = tostring(err) -- cdata -> string
    test:is_deeply({ok, err}, {false, case.exp_err}, case[1])
end

-- Bad source or/and opts parameters for merger's methods.
for _, case in ipairs(bad_merger_select_calls) do
    local merger_inst = merger.new(key_def, case.sources)
    local ok, err = pcall(merger_inst.select, merger_inst, case.opts)
    err = tostring(err) -- cdata -> string
    test:is_deeply({ok, err}, {false, case.exp_err}, case[1])
end

-- Create a key_def for each schema.
for _, schema in ipairs(schemas) do
    schema.key_def = key_def_lib.new(schema.parts)
end

test:test('use a source in two mergers', function(test)
    test:plan(5)

    local data = {{'a'}, {'b'}, {'c'}}
    local source = merger.new_source_fromtable(data)
    local i1 = merger.new(key_def, {source}):pairs()
    local i2 = merger.new(key_def, {source}):pairs()

    local t1 = i1:head():totable()
    test:is_deeply(t1, data[1], 'tuple 1 from merger 1')

    local t3 = i2:head():totable()
    test:is_deeply(t3, data[3], 'tuple 3 from merger 2')

    local t2 = i1:head():totable()
    test:is_deeply(t2, data[2], 'tuple 2 from merger 1')

    test:ok(i1:is_null(), 'merger 1 ends')
    test:ok(i2:is_null(), 'merger 2 ends')
end)

local function reusable_source_gen(param)
    local chunks = param.chunks
    local idx = param.idx or 1

    if idx > table.maxn(chunks) then
        return
    end

    local chunk = chunks[idx]
    param.idx = idx + 1

    if chunk == nil then
        return
    end
    return box.NULL, chunk
end

local function verify_reusable_source(test, source)
    test:plan(3)

    local exp = {{1}, {2}}
    local res = source:pairs():map(box.tuple.totable):totable()
    test:is_deeply(res, exp, '1st use')

    local exp = {{3}, {4}, {5}}
    local res = source:pairs():map(box.tuple.totable):totable()
    test:is_deeply(res, exp, '2nd use')

    local exp = {}
    local res = source:pairs():map(box.tuple.totable):totable()
    test:is_deeply(res, exp, 'end')
end

test:test('reuse a tuple source', function(test)
    local tuples = {{1}, {2}, nil, {3}, {4}, {5}}
    local source = merger.new_tuple_source(reusable_source_gen,
        {chunks = tuples})
    verify_reusable_source(test, source)
end)

test:test('reuse a table source', function(test)
    local chunks = {{{1}}, {{2}}, {}, nil, {{3}}, {{4}}, {}, {{5}}}
    local source = merger.new_table_source(reusable_source_gen,
        {chunks = chunks})
    verify_reusable_source(test, source)
end)

test:test('reuse a buffer source', function(test)
    local chunks_tbl = {{{1}}, {{2}}, {}, nil, {{3}}, {{4}}, {}, {{5}}}
    local chunks = {}
    for i = 1, table.maxn(chunks_tbl) do
        if chunks_tbl[i] == nil then
            chunks[i] = nil
        else
            chunks[i] = buffer.ibuf()
            msgpackffi.internal.encode_r(chunks[i], chunks_tbl[i], 0)
        end
    end
    local source = merger.new_buffer_source(reusable_source_gen,
        {chunks = chunks})
    verify_reusable_source(test, source)
end)

test:test('use limit', function(test)
    test:plan(6)

    local data = {{'a'}, {'b'}}

    local source = merger.new_source_fromtable(data)
    local m = merger.new(key_def, {source})
    local res = m:select({limit = 0})
    test:is(#res, 0, 'table output with limit 0')

    local source = merger.new_source_fromtable(data)
    local m = merger.new(key_def, {source})
    local res = m:select({limit = 1})
    test:is(#res, 1, 'table output with limit 1')
    test:is_deeply(res[1]:totable(), data[1], 'tuple content')

    local source = merger.new_source_fromtable(data)
    local m = merger.new(key_def, {source})
    local output_buffer = buffer.ibuf()
    m:select({buffer = output_buffer, limit = 0})
    local res = msgpackffi.decode(output_buffer.rpos)
    test:is(#res, 0, 'buffer output with limit 0')

    local source = merger.new_source_fromtable(data)
    local m = merger.new(key_def, {source})
    output_buffer:recycle()
    m:select({buffer = output_buffer, limit = 1})
    local res = msgpackffi.decode(output_buffer.rpos)
    test:is(#res, 1, 'buffer output with limit 1')
    test:is_deeply(res[1], data[1], 'tuple content')
end)

test:test('cascade mergers', function(test)
    test:plan(2)

    local data = {{'a'}, {'b'}}

    local source = merger.new_source_fromtable(data)
    local m1 = merger.new(key_def, {source})
    local m2 = merger.new(key_def, {m1})

    local res = m2:pairs():map(box.tuple.totable):totable()
    test:is_deeply(res, data, 'same key_def')

    local key_def_unicode = key_def_lib.new({{
        fieldno = 1,
        type = 'string',
        collation = 'unicode',
    }})

    local source = merger.new_source_fromtable(data)
    local m1 = merger.new(key_def, {source})
    local m2 = merger.new(key_def_unicode, {m1})

    local res = m2:pairs():map(box.tuple.totable):totable()
    test:is_deeply(res, data, 'different key_defs')
end)

-- Merging cases.
for _, input_type in ipairs({'buffer', 'table', 'tuple'}) do
    for _, output_type in ipairs({'buffer', 'table', 'tuple'}) do
        for _, reverse in ipairs({false, true}) do
            for _, use_table_as_tuple in ipairs({false, true}) do
                for _, use_fetch_source in ipairs({false, true}) do
                    for _, schema in ipairs(schemas) do
                        run_case(test, schema, {
                            input_type = input_type,
                            output_type = output_type,
                            reverse = reverse,
                            use_table_as_tuple = use_table_as_tuple,
                            use_fetch_source = use_fetch_source,
                        })
                    end
                end
            end
        end
    end
end

os.exit(test:check() and 0 or 1)
