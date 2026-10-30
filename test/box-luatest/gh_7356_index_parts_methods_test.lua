local t = require('luatest')
local g = t.group('gh-7356')

g.before_all(function(cg)
    local server = require('luatest.server')
    cg.server = server:new({alias = 'gh_7356'})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

g.after_each(function(cg)
    cg.server:exec(function()
        if box.space.test then box.space.test:drop() end
    end)
end)

-- Test index_object.parts:extract_key(tuple)
g.test_extract_key = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local pk = s:create_index('pk')
        s:insert{1, 99.5, 'X', nil, {'a', 'b'}}
        local sk = s:create_index('sk', {parts = {3, 'string', 1, 'unsigned'}})
        local k = sk.parts:extract_key(pk:get{1})
        t.assert_equals(k, {'X', 1})

        t.assert_error_msg_content_equals(
            'Usage: index.parts:extract_key(tuple)',
            sk.parts.extract_key)
        t.assert_error_msg_content_equals(
            'Usage: index.parts:extract_key(tuple)',
            sk.parts.extract_key, sk.parts)
        t.assert_error_msg_content_equals(
            'A tuple or a table expected, got number',
            sk.parts.extract_key, sk.parts, 0)
        t.assert_error_msg_content_equals(
            'Tuple field [3] required by space format is missing',
            sk.parts.extract_key, sk.parts, {0})

        local mk = s:create_index('mk', {parts = {{path = '[*]', field = 5}}})
        t.assert_error_msg_content_equals(
            'multikey path is unsupported',
            mk.parts.extract_key, mk.parts, pk:get{1})

        -- Check that extract_key() method is recreated with the correct key_def
        -- object after alter().
        sk:alter({parts = {1, 'unsigned', 2, 'double'}})
        t.assert_equals(sk.parts:extract_key(pk:get{1}), {1, 99.5})
    end)
end

-- Test index_object.parts:validate_key(key)
g.test_validate_key = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local pk = s:create_index('pk', {parts = {4, 'unsigned', 2, 'string'}})
        local errmsg

        t.assert_error_msg_content_equals(
            'Usage: index.parts:validate_key(key)',
            pk.parts.validate_key)
        t.assert_error_msg_content_equals(
            'Usage: index.parts:validate_key(key)',
            pk.parts.validate_key, pk.parts)
        t.assert_error_msg_content_equals(
            'A tuple or a table expected, got number',
            pk.parts.validate_key, pk.parts, 0)

        errmsg = "Supplied key type of part 0 does not match " ..
                 "index part type: expected unsigned"
        t.assert_error_msg_content_equals(
            errmsg, pk.parts.validate_key, pk.parts, {'a', 'b'})
        t.assert_error_msg_content_equals(
            errmsg, pk.parts.validate_key, pk.parts, box.tuple.new({'a', 'b'}))

        errmsg = "Supplied key type of part 1 does not match " ..
                 "index part type: expected string"
        t.assert_error_msg_equals(
            errmsg, pk.parts.validate_key, pk.parts, {1, 2})
        t.assert_error_msg_equals(
            errmsg, pk.parts.validate_key, pk.parts, box.tuple.new({1, 2}))

        errmsg = "Invalid key part count (expected [0..2], got 3)"
        t.assert_error_msg_equals(
            errmsg, pk.parts.validate_key, pk.parts, {1, 'a', 1})
        t.assert_error_msg_equals(
            errmsg, pk.parts.validate_key, pk.parts, box.tuple.new({1, 'a', 1}))

        pk.parts:validate_key({})
        pk.parts:validate_key({1})
        pk.parts:validate_key({1, 'a'})
        pk.parts:validate_key(box.tuple.new({}))
        pk.parts:validate_key(box.tuple.new({1}))
        pk.parts:validate_key(box.tuple.new({1, 'a'}))
    end)
end

-- Test index_object.parts:validate_full_key(key)
g.test_validate_full_key = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local pk = s:create_index('pk', {parts = {4, 'unsigned', 2, 'string'}})
        local errmsg

        t.assert_error_msg_content_equals(
            'Usage: index.parts:validate_full_key(key)',
            pk.parts.validate_full_key)
        t.assert_error_msg_content_equals(
            'Usage: index.parts:validate_full_key(key)',
            pk.parts.validate_full_key, pk.parts)
        t.assert_error_msg_content_equals(
            'A tuple or a table expected, got number',
            pk.parts.validate_full_key, pk.parts, 0)

        errmsg = "Supplied key type of part 0 does not match " ..
                 "index part type: expected unsigned"
        t.assert_error_msg_content_equals(errmsg, pk.parts.validate_full_key,
                                          pk.parts, {'a', 'b'})
        t.assert_error_msg_content_equals(errmsg, pk.parts.validate_full_key,
                                          pk.parts, box.tuple.new({'a', 'b'}))

        errmsg = "Supplied key type of part 1 does not match " ..
                 "index part type: expected string"
        t.assert_error_msg_equals(
            errmsg, pk.parts.validate_full_key, pk.parts, {1, 2})
        t.assert_error_msg_equals(
            errmsg, pk.parts.validate_full_key, pk.parts, box.tuple.new({1, 2}))

        errmsg = "Invalid key part count in an exact match (expected 2, got 1)"
        t.assert_error_msg_equals(
            errmsg, pk.parts.validate_full_key, pk.parts, {1})
        t.assert_error_msg_equals(
            errmsg, pk.parts.validate_full_key, pk.parts, box.tuple.new({1}))

        errmsg = "Invalid key part count in an exact match (expected 2, got 3)"
        t.assert_error_msg_equals(errmsg, pk.parts.validate_full_key, pk.parts,
                                  {1, 'a', 1})
        t.assert_error_msg_equals(errmsg, pk.parts.validate_full_key, pk.parts,
                                  box.tuple.new({1, 'a', 1}))

        pk.parts:validate_full_key({1, 'a'})
        pk.parts:validate_full_key(box.tuple.new({1, 'a'}))
    end)
end

-- Test index_object.parts:validate_tuple(tuple)
g.test_validate_tuple = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local pk = s:create_index('pk', {parts = {4, 'unsigned', 2, 'string'}})
        local errmsg

        t.assert_error_msg_content_equals(
            'Usage: index.parts:validate_tuple(tuple)',
            pk.parts.validate_tuple)
        t.assert_error_msg_content_equals(
            'Usage: index.parts:validate_tuple(tuple)',
            pk.parts.validate_tuple, pk.parts)
        t.assert_error_msg_content_equals(
            'A tuple or a table expected, got number',
            pk.parts.validate_tuple, pk.parts, 0)

        errmsg = "Supplied key type of part 0 does not match " ..
                 "index part type: expected unsigned"
        t.assert_error_msg_content_equals(
            errmsg, pk.parts.validate_tuple, pk.parts,
            {box.NULL, 'a', box.NULL, 'b'})
        t.assert_error_msg_content_equals(
            errmsg, pk.parts.validate_tuple, pk.parts,
            box.tuple.new({box.NULL, 'a', box.NULL, 'b'}))

        errmsg = "Supplied key type of part 1 does not match " ..
                 "index part type: expected string"
        t.assert_error_msg_equals(
            errmsg, pk.parts.validate_tuple, pk.parts,
            {box.NULL, 1, box.NULL, 2})
        t.assert_error_msg_equals(
            errmsg, pk.parts.validate_tuple, pk.parts,
            box.tuple.new({box.NULL, 1, box.NULL, 2}))

        errmsg = "Tuple field [4] required by space format is missing"
        t.assert_error_msg_equals(errmsg, pk.parts.validate_tuple, pk.parts,
                                  {1, 2})
        t.assert_error_msg_equals(errmsg, pk.parts.validate_tuple, pk.parts,
                                  box.tuple.new({1, 2}))

        pk.parts:validate_tuple({box.NULL, 'a', box.NULL, 1})
        pk.parts:validate_tuple(box.tuple.new({box.NULL, 'a', box.NULL, 1}))
    end)
end

-- Test index_object.parts:compare(tuple_a, tuple_b)
g.test_compare = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local i = s:create_index('i', {parts = {
            {field = 3, type = 'string', collation = 'unicode_ci'},
            {field = 1, type = 'unsigned'}
        }})
        local tuple_a = {1, 99.5, 'x', nil, {'a', 'b'}}
        local tuple_b = {1, 99.5, 'X', nil, {'a', 'b'}}
        local tuple_c = {2, 99.5, 'X', nil, {'a', 'b'}}
        t.assert_equals(i.parts:compare(tuple_a, tuple_b), 0)
        t.assert_equals(i.parts:compare(tuple_a, tuple_c), -1)
        t.assert_equals(i.parts.compare(nil, tuple_c, tuple_a), 1)

        t.assert_error_msg_content_equals(
            'Usage: index.parts:compare(tuple_a, tuple_b)',
            i.parts.compare)
        t.assert_error_msg_content_equals(
            'Usage: index.parts:compare(tuple_a, tuple_b)',
            i.parts.compare, i.parts, tuple_a)
        t.assert_error_msg_content_equals(
            'A tuple or a table expected, got cdata',
            i.parts.compare, i.parts, tuple_a, box.NULL)
        t.assert_error_msg_content_equals(
            'Supplied key type of part 0 does not match index part type: ' ..
            'expected string', i.parts.compare, nil, {0, [3]=0}, tuple_b)

        local mk = s:create_index('mk', {parts = {{path = '[*]', field = 5}}})
        t.assert_error_msg_content_equals(
            'multikey path is unsupported',
            mk.parts.compare, mk.parts, tuple_a, tuple_b)
    end)
end

-- Test index_object.parts:compare_with_key(tuple_a, tuple_b)
g.test_compare_with_key = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local i = s:create_index('i', {parts = {
            {field = 3, type = 'string', collation = 'unicode_ci'},
            {field = 1, type = 'unsigned'}
        }})
        local tuple = {1, 99.5, 'x', nil, {'a', 'b'}}
        t.assert_equals(i.parts:compare_with_key(tuple, {'x', 1}), 0)
        t.assert_equals(i.parts:compare_with_key(tuple, {'X', 1}), 0)
        t.assert_equals(i.parts:compare_with_key(tuple, {'X', 2}), -1)

        t.assert_error_msg_content_equals(
            'Usage: index.parts:compare_with_key(tuple, key)',
            i.parts.compare_with_key)
        t.assert_error_msg_content_equals(
            'Usage: index.parts:compare_with_key(tuple, key)',
            i.parts.compare_with_key, box.NULL)
        t.assert_error_msg_content_equals(
            'Usage: index.parts:compare_with_key(tuple, key)',
            i.parts.compare_with_key, box.NULL, {0, nil, ''})
        t.assert_error_msg_content_equals(
            'Supplied key type of part 1 does not match index part type: ' ..
            'expected unsigned',
            i.parts.compare_with_key, box.NULL, {0, nil, ''}, {'', ''})

        local mk = s:create_index('mk', {parts = {{path = '[*]', field = 5}}})
        t.assert_error_msg_content_equals(
            'multikey path is unsupported',
            mk.parts.compare_with_key, mk.parts, tuple, {'x', 1})
    end)
end

-- Test index_object.parts:compare_keys(key_a, key_b)
g.test_compare_keys = function(cg)
    cg.server:exec(function()
        local json = require('json')

        local s = box.schema.space.create('test')
        local pk = s:create_index('pk', {parts = {4, 'unsigned', 2, 'string'}})
        local errmsg

        t.assert_error_msg_equals(
            "Usage: index.parts:compare_keys(key_a, key_b)",
            pk.parts.compare_keys)
        t.assert_error_msg_equals(
            "Usage: index.parts:compare_keys(key_a, key_b)",
            pk.parts.compare_keys, pk.parts)
        t.assert_error_msg_equals(
            "Usage: index.parts:compare_keys(key_a, key_b)",
            pk.parts.compare_keys, pk.parts, {})
        t.assert_error_msg_equals(
            "A tuple or a table expected, got number",
            pk.parts.compare_keys, pk.parts, 0, {})
        t.assert_error_msg_equals(
            "A tuple or a table expected, got number",
            pk.parts.compare_keys, pk.parts, {}, 0)

        errmsg = "Supplied key type of part 0 does not match " ..
                 "index part type: expected unsigned"
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  {'a', 'b'}, {})
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  box.tuple.new({'a', 'b'}), {})
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  {}, {'a', 'b'})
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  {}, box.tuple.new({'a', 'b'}))

        errmsg = "Supplied key type of part 1 does not match " ..
                 "index part type: expected string"
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  {1, 2}, {})
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  box.tuple.new({1, 2}), {})
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  {}, {1, 2})
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  {}, box.tuple.new({1, 2}))

        errmsg = "Invalid key part count (expected [0..2], got 3)"
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  {1, 'a', 1}, {})
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  box.tuple.new({1, 'a', 1}), {})
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  {}, {1, 'a', 1})
        t.assert_error_msg_equals(errmsg, pk.parts.compare_keys, pk.parts,
                                  {}, box.tuple.new({1, 'a', 1}))

        for _, v in ipairs({
            {{}, {}, 0},
            {{}, {1}, 0},
            {{}, {1, 'a'}, 0},
            {{1}, {}, 0},
            {{1}, {1}, 0},
            {{1}, {1, 'a'}, 0},
            {{1, 'a'}, {}, 0},
            {{1, 'a'}, {1}, 0},
            {{1, 'a'}, {1, 'a'}, 0},
            {{2}, {1}, 1},
            {{2}, {1, 'a'}, 1},
            {{2}, {3}, -1},
            {{2}, {3, 'a'}, -1},
            {{2, 'b'}, {1}, 1},
            {{2, 'b'}, {1, 'a'}, 1},
            {{2, 'b'}, {2, 'a'}, 1},
            {{2, 'b'}, {3}, -1},
            {{2, 'b'}, {3, 'a'}, -1},
            {{2, 'b'}, {2, 'c'}, -1},
        }) do
            local key_a, key_b, ret = unpack(v)
            local msg = string.format('compare(%s, %s)',
                                      json.encode(key_a), json.encode(key_b))
            t.assert_equals(pk.parts:compare_keys(key_a, key_b),
                            ret, msg)
            t.assert_equals(pk.parts:compare_keys(box.tuple.new(key_a), key_b),
                            ret, msg)
            t.assert_equals(pk.parts:compare_keys(key_a, box.tuple.new(key_b)),
                            ret, msg)
            t.assert_equals(pk.parts:compare_keys(box.tuple.new(key_a),
                                                  box.tuple.new(key_b)),
                            ret, msg)
        end
    end)
end

-- Test index_object.parts:merge(second_index_parts)
g.test_merge = function(cg)
    cg.server:exec(function()
        local s = box.schema.space.create('test')
        local i = s:create_index('i', {parts = {{type = 'string', field = 3},
                                                {type = 'scalar', field = 2}}})
        local j = s:create_index('j', {parts = {{type = 'unsigned', field = 1},
                                                {type = 'string', field = 3}}})
        local exp = {{fieldno = 3, type = 'string', is_nullable = false,
                      exclude_null = false, sort_order = 'asc'},
                     {fieldno = 2, type = 'scalar', is_nullable = false,
                      exclude_null = false, sort_order = 'asc'},
                     {fieldno = 1, type = 'unsigned', is_nullable = false,
                      exclude_null = false, sort_order = 'asc'}}
        t.assert_equals(i.parts:merge(j.parts):totable(), exp)

        t.assert_error_msg_content_equals(
            'Usage: index.parts:merge(second_index_parts)',
            i.parts.merge)
        t.assert_error_msg_content_equals(
            'Usage: index.parts:merge(second_index_parts)',
            i.parts.merge, i.parts)
        t.assert_error_msg_content_equals(
            'Usage: index.parts:merge(second_index_parts)',
            i.parts.merge, i.parts, j.parts, box.NULL)
        t.assert_error_msg_content_equals(
            "Can't create key_def from the second index.parts",
            i.parts.merge, i.parts, 100)
        t.assert_error_msg_content_equals(
            "Can't create key_def from the second index.parts",
            i.parts.merge, i.parts, {{type = 'scalar'}})

        local mk = s:create_index('mk', {parts = {{path = '[*]', field = 5}}})
        t.assert_error_msg_content_equals(
            'multikey path is unsupported',
            mk.parts.merge, mk.parts, j.parts)
        t.assert_error_msg_content_equals(
            "Can't create key_def from the second index.parts",
            i.parts.merge, i.parts, mk.parts)
    end)
end
