-- Verify content of the initial snapshot.

local uuid = require('uuid')
local t = require('luatest')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

-- Collect data from system spaces once and check it in separate test cases.
--
-- NB: luatest.serveris not used, because it writes to the _user system space to
-- grant a test user a permission to execute code (so :exec() works). Use
-- justrun instead.
g.before_all(function(cg)
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'main.lua', string.dump(function()
        local json = require('json')

        box.cfg()

        for _, space in ipairs({
            box.space._schema,
            box.space._cluster,
            box.space._space,
            box.space._index,
            box.space._user,
            box.space._func,
            box.space._priv,
        }) do
            print(json.encode({space.name, space:select()}))
        end

        os.exit()
    end))
    local res = justrun.tarantool(dir, {}, {'main.lua'})
    assert(res.exit_code == 0)

    cg.space = {}
    for _, v in ipairs(res.stdout) do
        cg.space[v[1]] = v[2]
    end
end)

g.after_all(function(cg)
    cg.space = nil
end)

local function is_uuid_str(x)
    return type(x) == 'string' and uuid.fromstr(x) ~= nil
end

local function is_datetime_str(x)
    local re = '^%d%d%d%d%-%d%d%-%d%d %d%d:%d%d:%d%d$'
    return type(x) == 'string' and x:match(re) ~= nil
end

-- Replace placeholders recursively.
local function replace_placeholders(data, pattern)
    if type(data) == 'table' and type(pattern) == 'table' then
        local res = table.copy(data)
        for k, v in pairs(pattern) do
            res[k] = replace_placeholders(data[k], v)
        end
        return res
    end

    if pattern == '<uuid_str>' and is_uuid_str(data) then
        return '<uuid_str>'
    elseif pattern == '<datetime_str>' and is_datetime_str(data) then
        return '<datetime_str>'
    end

    return data
end

local function assert_equals_with_placeholders(result, expected)
    local masked_result = replace_placeholders(result, expected)
    t.assert_equals(masked_result, expected)
end

g.test_schema = function(cg)
    assert_equals_with_placeholders(cg.space._schema, {
        {'replicaset_uuid', '<uuid_str>'},
        {'version', 3, 8, 0},
    })
end

g.test_cluster = function(cg)
    assert_equals_with_placeholders(cg.space._cluster, {
        {1, '<uuid_str>'},
    })
end

g.test_space = function(cg)
    local ID_FIELD = 1
    local NAME_FIELD = 3

    local function space_id_to_name()
        local res = {}
        for _, space_def in ipairs(cg.space._space) do
            res[space_def[ID_FIELD]] = space_def[NAME_FIELD]
        end
        return res
    end

    -- Track find_entry() calls to do self-check (see the end of the test case).
    -- Save id-to-name mapping here to ease to compare with space_id_to_name().
    local find_entry_calls = {}

    -- Find _space entry by space name.
    local function find_entry(space_name)
        for _, space_def in ipairs(cg.space._space) do
            if space_def[NAME_FIELD] == space_name then
                find_entry_calls[space_def[ID_FIELD]] = space_name
                return space_def
            end
        end
        error(('Unable to find _space entry for %q'):format(space_name))
    end

    -- Verify that there are no unexpected entries and also check the
    -- id-to-name mapping.
    t.assert_equals(space_id_to_name(), {
        [257] = '_vinyl_deferred_delete',
        [272] = '_schema',
        [276] = '_collation',
        [277] = '_vcollation',
        [280] = '_space',
        [281] = '_vspace',
        [284] = '_sequence',
        [285] = '_sequence_data',
        [286] = '_vsequence',
        [288] = '_index',
        [289] = '_vindex',
        [296] = '_func',
        [297] = '_vfunc',
        [304] = '_user',
        [305] = '_vuser',
        [312] = '_priv',
        [313] = '_vpriv',
        [320] = '_cluster',
        [328] = '_trigger',
        [330] = '_truncate',
        [340] = '_space_sequence',
        [341] = '_vspace_sequence',
        [356] = '_fk_constraint',
        [364] = '_ck_constraint',
        [372] = '_func_index',
        [380] = '_session_settings',
        [388] = '_gc_consumers',
        [396] = '_recovery_point',
    })

    -- Next assertions verify _space entries one by one.

    t.assert_equals(find_entry('_vinyl_deferred_delete'), {
        --[[ id ]] 257,
        --[[ owner ]] 1,
        --[[ name ]] '_vinyl_deferred_delete',
        --[[ engine ]] 'blackhole',
        --[[ field_count ]] 0,
        --[[ flags ]] {group_id = 1},
        --[[ format ]] {
            {type = 'unsigned', name = 'space_id'},
            {type = 'unsigned', name = 'lsn'},
            {type = 'array', name = 'tuple'},
        },
    })

    t.assert_equals(find_entry('_schema'), {
        --[[ id ]] 272,
        --[[ owner ]] 1,
        --[[ name ]] '_schema',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] {
            {type = 'string', name = 'key'},
            {type = 'any', name = 'value', is_nullable = true},
        },
    })

    local collation_space_format = {
        {type = 'unsigned', name = 'id'},
        {type = 'string', name = 'name'},
        {type = 'unsigned', name = 'owner'},
        {type = 'string', name = 'type'},
        {type = 'string', name = 'locale'},
        {type = 'map', name = 'opts'},
    }
    t.assert_equals(find_entry('_collation'), {
        --[[ id ]] 276,
        --[[ owner ]] 1,
        --[[ name ]] '_collation',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] collation_space_format,
    })
    t.assert_equals(find_entry('_vcollation'), {
        --[[ id ]] 277,
        --[[ owner ]] 1,
        --[[ name ]] '_vcollation',
        --[[ engine ]] 'sysview',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] collation_space_format,
    })

    local space_space_format = {
        {type = 'unsigned', name = 'id'},
        {type = 'unsigned', name = 'owner'},
        {type = 'string', name = 'name'},
        {type = 'string', name = 'engine'},
        {type = 'unsigned', name = 'field_count'},
        {type = 'map', name = 'flags'},
        {type = 'array', name = 'format'},
    }
    t.assert_equals(find_entry('_space'), {
        --[[ id ]] 280,
        --[[ owner ]] 1,
        --[[ name ]] '_space',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] space_space_format,
    })
    t.assert_equals(find_entry('_vspace'), {
        --[[ id ]] 281,
        --[[ owner ]] 1,
        --[[ name ]] '_vspace',
        --[[ engine ]] 'sysview',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] space_space_format,
    })

    local sequence_space_format = {
        {type = 'unsigned', name = 'id'},
        {type = 'unsigned', name = 'owner'},
        {type = 'string', name = 'name'},
        {type = 'integer', name = 'step'},
        {type = 'integer', name = 'min'},
        {type = 'integer', name = 'max'},
        {type = 'integer', name = 'start'},
        {type = 'integer', name = 'cache'},
        {type = 'boolean', name = 'cycle'},
    }
    t.assert_equals(find_entry('_sequence'), {
        --[[ id ]] 284,
        --[[ owner ]] 1,
        --[[ name ]] '_sequence',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] sequence_space_format,
    })
    t.assert_equals(find_entry('_sequence_data'), {
        --[[ id ]] 285,
        --[[ owner ]] 1,
        --[[ name ]] '_sequence_data',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] {
            {type = 'unsigned', name = 'id'},
            {type = 'integer', name = 'value'},
        },
    })
    t.assert_equals(find_entry('_vsequence'), {
        --[[ id ]] 286,
        --[[ owner ]] 1,
        --[[ name ]] '_vsequence',
        --[[ engine ]] 'sysview',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] sequence_space_format,
    })

    local index_space_format = {
        {type = 'unsigned', name = 'id'},
        {type = 'unsigned', name = 'iid'},
        {type = 'string', name = 'name'},
        {type = 'string', name = 'type'},
        {type = 'map', name = 'opts'},
        {type = 'array', name = 'parts'},
    }
    t.assert_equals(find_entry('_index'), {
        --[[ id ]] 288,
        --[[ owner ]] 1,
        --[[ name ]] '_index',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] index_space_format,
    })
    t.assert_equals(find_entry('_vindex'), {
        --[[ id ]] 289,
        --[[ owner ]] 1,
        --[[ name ]] '_vindex',
        --[[ engine ]] 'sysview',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] index_space_format,
    })

    local func_space_format = {
        {type = 'unsigned', name = 'id'},
        {type = 'unsigned', name = 'owner'},
        {type = 'string', name = 'name'},
        {type = 'unsigned', name = 'setuid'},
        {type = 'string', name = 'language'},
        {type = 'string', name = 'body'},
        {type = 'string', name = 'routine_type'},
        {type = 'array', name = 'param_list'},
        {type = 'string', name = 'returns'},
        {type = 'string', name = 'aggregate'},
        {type = 'string', name = 'sql_data_access'},
        {type = 'boolean', name = 'is_deterministic'},
        {type = 'boolean', name = 'is_sandboxed'},
        {type = 'boolean', name = 'is_null_call'},
        {type = 'array', name = 'exports'},
        {type = 'map', name = 'opts'},
        {type = 'string', name = 'comment'},
        {type = 'string', name = 'created'},
        {type = 'string', name = 'last_altered'},
        {type = 'array', name = 'trigger'},
    }
    t.assert_equals(find_entry('_func'), {
        --[[ id ]] 296,
        --[[ owner ]] 1,
        --[[ name ]] '_func',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] func_space_format,
    })
    t.assert_equals(find_entry('_vfunc'), {
        --[[ id ]] 297,
        --[[ owner ]] 1,
        --[[ name ]] '_vfunc',
        --[[ engine ]] 'sysview',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] func_space_format,
    })

    local user_space_format = {
        {type = 'unsigned', name = 'id'},
        {type = 'unsigned', name = 'owner'},
        {type = 'string', name = 'name'},
        {type = 'string', name = 'type'},
        {type = 'map', name = 'auth'},
        {type = 'array', name = 'auth_history'},
        {type = 'unsigned', name = 'last_modified'},
    }
    t.assert_equals(find_entry('_user'), {
        --[[ id ]] 304,
        --[[ owner ]] 1,
        --[[ name ]] '_user',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] user_space_format,
    })
    t.assert_equals(find_entry('_vuser'), {
        --[[ id ]] 305,
        --[[ owner ]] 1,
        --[[ name ]] '_vuser',
        --[[ engine ]] 'sysview',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] user_space_format,
    })

    local priv_space_format = {
        {type = 'unsigned', name = 'grantor'},
        {type = 'unsigned', name = 'grantee'},
        {type = 'string', name = 'object_type'},
        {type = 'scalar', name = 'object_id'},
        {type = 'unsigned', name = 'privilege'},
    }
    t.assert_equals(find_entry('_priv'), {
        --[[ id ]] 312,
        --[[ owner ]] 1,
        --[[ name ]] '_priv',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] priv_space_format,
    })
    t.assert_equals(find_entry('_vpriv'), {
        --[[ id ]] 313,
        --[[ owner ]] 1,
        --[[ name ]] '_vpriv',
        --[[ engine ]] 'sysview',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] priv_space_format,
    })

    t.assert_equals(find_entry('_cluster'), {
        --[[ id ]] 320,
        --[[ owner ]] 1,
        --[[ name ]] '_cluster',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] {
            {type = 'unsigned', name = 'id'},
            {type = 'string', name = 'uuid'},
            {type = 'string', name = 'name', is_nullable = true},
        },
    })

    t.assert_equals(find_entry('_trigger'), {
        --[[ id ]] 328,
        --[[ owner ]] 1,
        --[[ name ]] '_trigger',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] {
            {type = 'string', name = 'name'},
            {type = 'unsigned', name = 'space_id'},
            {type = 'map', name = 'opts'},
        },
    })

    t.assert_equals(find_entry('_truncate'), {
        --[[ id ]] 330,
        --[[ owner ]] 1,
        --[[ name ]] '_truncate',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] {
            {type = 'unsigned', name = 'id'},
            {type = 'unsigned', name = 'count'},
        },
    })

    local space_sequence_space_format = {
        {type = 'unsigned', name = 'id'},
        {type = 'unsigned', name = 'sequence_id'},
        {type = 'boolean', name = 'is_generated'},
        {type = 'unsigned', name = 'field'},
        {type = 'string', name = 'path'},
    }
    t.assert_equals(find_entry('_space_sequence'), {
        --[[ id ]] 340,
        --[[ owner ]] 1,
        --[[ name ]] '_space_sequence',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] space_sequence_space_format,
    })
    t.assert_equals(find_entry('_vspace_sequence'), {
        --[[ id ]] 341,
        --[[ owner ]] 1,
        --[[ name ]] '_vspace_sequence',
        --[[ engine ]] 'sysview',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] space_sequence_space_format,
    })

    t.assert_equals(find_entry('_fk_constraint'), {
        --[[ id ]] 356,
        --[[ owner ]] 1,
        --[[ name ]] '_fk_constraint',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] {
            {type = 'string', name = 'name'},
            {type = 'unsigned', name = 'child_id'},
            {type = 'unsigned', name = 'parent_id'},
            {type = 'boolean', name = 'is_deferred'},
            {type = 'string', name = 'match'},
            {type = 'string', name = 'on_delete'},
            {type = 'string', name = 'on_update'},
            {type = 'array', name = 'child_cols'},
            {type = 'array', name = 'parent_cols'},
        },
    })

    t.assert_equals(find_entry('_ck_constraint'), {
        --[[ id ]] 364,
        --[[ owner ]] 1,
        --[[ name ]] '_ck_constraint',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] {
            {type = 'unsigned', name = 'space_id'},
            {type = 'string', name = 'name'},
            {type = 'boolean', name = 'is_deferred'},
            {type = 'str', name = 'language'},
            {type = 'str', name = 'code'},
            {type = 'boolean', name = 'is_enabled'},
        },
    })

    t.assert_equals(find_entry('_func_index'), {
        --[[ id ]] 372,
        --[[ owner ]] 1,
        --[[ name ]] '_func_index',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] {
            {type = 'unsigned', name = 'space_id'},
            {type = 'unsigned', name = 'index_id'},
            {type = 'unsigned', name = 'func_id'},
        },
    })

    t.assert_equals(find_entry('_session_settings'), {
        --[[ id ]] 380,
        --[[ owner ]] 1,
        --[[ name ]] '_session_settings',
        --[[ engine ]] 'service',
        --[[ field_count ]] 2,
        --[[ flags ]] {temporary = true},
        --[[ format ]] {
            {type = 'string', name = 'name'},
            {type = 'any', name = 'value'},
        },
    })

    t.assert_equals(find_entry('_gc_consumers'), {
        --[[ id ]] 388,
        --[[ owner ]] 1,
        --[[ name ]] '_gc_consumers',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {group_id = 1},
        --[[ format ]] {
            {type = 'string', name = 'uuid'},
            {type = 'map', name = 'vclock'},
            {type = 'map', name = 'opts'}
        },
    })

    t.assert_equals(find_entry('_recovery_point'), {
        --[[ id ]] 396,
        --[[ owner ]] 1,
        --[[ name ]] '_recovery_point',
        --[[ engine ]] 'memtx',
        --[[ field_count ]] 0,
        --[[ flags ]] {},
        --[[ format ]] {
            {type = 'unsigned', name = 'timestamp'},
            {type = 'unsigned', name = 'replica_id'},
            {type = 'unsigned', name = 'lsn'},
            {type = 'map', name = 'opts'},
        },
    })

    -- Self-check: ensure that all the _space entries were checked.
    t.assert_equals(find_entry_calls, space_id_to_name())
end

g.test_index = function(cg)
    local SPACE_ID_FIELD = 1

    local function space_ids()
        local res = {}
        for _, index_def in ipairs(cg.space._index) do
            res[index_def[SPACE_ID_FIELD]] = true
        end
        return res
    end

    -- Track find_entry() calls to do self-check (see the end of the test case).
    local find_entries_calls = {}

    -- Find _index entries by space id.
    local function find_entries(space_id)
        find_entries_calls[space_id] = true

        local res = {}
        for _, index_def in ipairs(cg.space._index) do
            if index_def[SPACE_ID_FIELD] == space_id then
                table.insert(res, index_def)
            end
        end
        return res
    end

    -- Indexes of _schema system space.
    t.assert_equals(find_entries(272), {
        {
            --[[ id ]] 272,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{0, 'string'}}
        },
    })

    -- Indexes of _collation system space and _vcollation system view.
    local function expected_collation_entries(space_id)
        return {
            {
                --[[ id ]] space_id,
                --[[ iid ]] 0,
                --[[ name ]] 'primary',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = true},
                --[[ parts ]] {{0, 'unsigned'}},
            },
            {
                --[[ id ]] space_id,
                --[[ iid ]] 1,
                --[[ name ]] 'name',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = true},
                --[[ parts ]] {{1, 'string'}},
            },
        }
    end
    t.assert_equals(find_entries(276), expected_collation_entries(276))
    t.assert_equals(find_entries(277), expected_collation_entries(277))

    -- Indexes of _space system space and _vspace system view.
    local function expected_space_entries(space_id)
        return {
            {
                --[[ id ]] space_id,
                --[[ iid ]] 0,
                --[[ name ]] 'primary',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = true},
                --[[ parts ]] {{0, 'unsigned'}},
            },
            {
                --[[ id ]] space_id,
                --[[ iid ]] 1,
                --[[ name ]] 'owner',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = false},
                --[[ parts ]] {{1, 'unsigned'}},
            },
            {
                --[[ id ]] space_id,
                --[[ iid ]] 2,
                --[[ name ]] 'name',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = true},
                --[[ parts ]] {{2, 'string'}},
            },
        }
    end
    t.assert_equals(find_entries(280), expected_space_entries(280))
    t.assert_equals(find_entries(281), expected_space_entries(281))

    -- Indexes of _sequence system space, _sequence_data system space and
    -- _vsequence system view.
    local function expected_sequence_entries(space_id)
        return {
            {
                --[[ id ]] space_id,
                --[[ iid ]] 0,
                --[[ name ]] 'primary',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = true},
                --[[ parts ]] {{0, 'unsigned'}},
            },
            {
                --[[ id ]] space_id,
                --[[ iid ]] 1,
                --[[ name ]] 'owner',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = false},
                --[[ parts ]] {{1, 'unsigned'}},
            },
            {
                --[[ id ]] space_id,
                --[[ iid ]] 2,
                --[[ name ]] 'name',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = true},
                --[[ parts ]] {{2, 'string'}},
            },
        }
    end
    t.assert_equals(find_entries(284), expected_sequence_entries(284))
    t.assert_equals(find_entries(285), {
        {
            --[[ id ]] 285,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'hash',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{0, 'unsigned'}},
        },
    })
    t.assert_equals(find_entries(286), expected_sequence_entries(286))

    -- Indexes of _index system space and _vindex system view.
    local function expected_index_entries(space_id)
        return {
            {
                --[[ id ]] space_id,
                --[[ iid ]] 0,
                --[[ name ]] 'primary',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = true},
                --[[ parts ]] {{0, 'unsigned'}, {1, 'unsigned'}},
            },
            {
                --[[ id ]] space_id,
                --[[ iid ]] 2,
                --[[ name ]] 'name',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = true},
                --[[ parts ]] {{0, 'unsigned'}, {2, 'string'}},
            },
        }
    end
    t.assert_equals(find_entries(288), expected_index_entries(288))
    t.assert_equals(find_entries(289), expected_index_entries(289))

    -- Indexes of _func system space and _vfunc system view.
    --
    -- NB: The indexes declaration is the same as for _sequence (except space
    -- id). This is not a property we must retain, more like a coincidence.
    -- We use this fact in the test just to save typing.
    local expected_func_entries = expected_sequence_entries
    t.assert_equals(find_entries(296), expected_func_entries(296))
    t.assert_equals(find_entries(297), expected_func_entries(297))

    -- Indexes of _user system space and _vuser system view.
    --
    -- NB: The indexes declaration is the same as for _sequence (except space
    -- id). This is not a property we must retain, more like a coincidence.
    -- We use this fact in the test just to save typing.
    local expected_user_entries = expected_sequence_entries
    t.assert_equals(find_entries(304), expected_user_entries(304))
    t.assert_equals(find_entries(305), expected_user_entries(305))

    -- Indexes of _priv system space and _vpriv system view.
    local function expected_priv_entries(space_id)
        return {
            {
                --[[ id ]] space_id,
                --[[ iid ]] 0,
                --[[ name ]] 'primary',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = true},
                --[[ parts ]] {{1, 'unsigned'}, {2, 'string'}, {3, 'scalar'}},
            },
            {
                --[[ id ]] space_id,
                --[[ iid ]] 1,
                --[[ name ]] 'owner',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = false},
                --[[ parts ]] {{0, 'unsigned'}},
            },
            {
                --[[ id ]] space_id,
                --[[ iid ]] 2,
                --[[ name ]] 'object',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = false},
                --[[ parts ]] {{2, 'string'}, {3, 'scalar'}},
            },
        }
    end
    t.assert_equals(find_entries(312), expected_priv_entries(312))
    t.assert_equals(find_entries(313), expected_priv_entries(313))

    -- Indexes of _cluster system space.
    t.assert_equals(find_entries(320), {
        {
            --[[ id ]] 320,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{0, 'unsigned'}},
        },
        {
            --[[ id ]] 320,
            --[[ iid ]] 1,
            --[[ name ]] 'uuid',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{1, 'string'}},
        },
    })

    -- Indexes of _trigger system space.
    t.assert_equals(find_entries(328), {
        {
            --[[ id ]] 328,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{0, 'string'}},
        },
        {
            --[[ id ]] 328,
            --[[ iid ]] 1,
            --[[ name ]] 'space_id',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = false},
            --[[ parts ]] {{1, 'unsigned'}},
        },
    })

    -- Indexes of _truncate system space.
    t.assert_equals(find_entries(330), {
        {
            --[[ id ]] 330,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{0, 'unsigned'}},
        },
    })

    -- Indexes of _space_sequence system space and _vspace_sequence system view.
    local function expected_space_sequence_entries(space_id)
        return {
            {
                --[[ id ]] space_id,
                --[[ iid ]] 0,
                --[[ name ]] 'primary',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = true},
                --[[ parts ]] {{0, 'unsigned'}},
            },
            {
                --[[ id ]] space_id,
                --[[ iid ]] 1,
                --[[ name ]] 'sequence',
                --[[ type ]] 'tree',
                --[[ opts ]] {unique = false},
                --[[ parts ]] {{1, 'unsigned'}},
            },
        }
    end
    t.assert_equals(find_entries(340), expected_space_sequence_entries(340))
    t.assert_equals(find_entries(341), expected_space_sequence_entries(341))

    -- Indexes of _fk_constraint system space.
    t.assert_equals(find_entries(356), {
        {
            --[[ id ]] 356,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{0, 'string'}, {1, 'unsigned'}},
        },
        {
            --[[ id ]] 356,
            --[[ iid ]] 1,
            --[[ name ]] 'child_id',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = false},
            --[[ parts ]] {{1, 'unsigned'}},
        },
    })

    -- Indexes of _ck_constraint system space.
    t.assert_equals(find_entries(364), {
        {
            --[[ id ]] 364,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{0, 'unsigned'}, {1, 'string'}},
        },
    })

    -- Indexes of _func_index system space.
    t.assert_equals(find_entries(372), {
        {
            --[[ id ]] 372,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{0, 'unsigned'}, {1, 'unsigned'}},
        },
        {
            --[[ id ]] 372,
            --[[ iid ]] 1,
            --[[ name ]] 'fid',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = false},
            --[[ parts ]] {{2, 'unsigned'}},
        },
    })

    -- Indexes of _session_settings system space.
    t.assert_equals(find_entries(380), {
        {
            --[[ id ]] 380,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{0, 'string'}},
        },
    })

    -- Indexes of _gc_consumers system space.
    t.assert_equals(find_entries(388), {
        {
            --[[ id ]] 388,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {{0, 'string'}},
        },
    })

    -- Indexes of _recovery_point system space.
    t.assert_equals(find_entries(396), {
        {
            --[[ id ]] 396,
            --[[ iid ]] 0,
            --[[ name ]] 'primary',
            --[[ type ]] 'tree',
            --[[ opts ]] {unique = true},
            --[[ parts ]] {
                {field = 0, type = 'unsigned'},
                {field = 1, type = 'unsigned'},
                {field = 2, type = 'unsigned'},
            },
        },
    })

    -- Self-check: ensure that all the _index entries were checked.
    t.assert_equals(find_entries_calls, space_ids())
end

g.test_user = function(cg)
    t.assert_equals(cg.space._user, {
        {
            --[[ id ]] 0,
            --[[ owner ]] 1,
            --[[ name ]] 'guest',
            --[[ type ]] 'user',
            --[[ auth ]] {['chap-sha1'] = 'vhvewKp0tNyweZQ+cFKAlsyphfg='},
            --[[ auth_history ]] {},
            --[[ last_modified ]] 0,
        },
        {
            --[[ id ]] 1,
            --[[ owner ]] 1,
            --[[ name ]] 'admin',
            --[[ type ]] 'user',
            --[[ auth ]] {},
            --[[ auth_history ]] {},
            --[[ last_modified ]] 0,
        },
        {
            --[[ id ]] 2,
            --[[ owner ]] 1,
            --[[ name ]] 'public',
            --[[ type ]] 'role',
            --[[ auth ]] {},
            --[[ auth_history ]] {},
            --[[ last_modified ]] 0,
        },
        {
            --[[ id ]] 3,
            --[[ owner ]] 1,
            --[[ name ]] 'replication',
            --[[ type ]] 'role',
            --[[ auth ]] {},
            --[[ auth_history ]] {},
            --[[ last_modified ]] 0,
        },
        {
            --[[ id ]] 31,
            --[[ owner ]] 1,
            --[[ name ]] 'super',
            --[[ type ]] 'role',
            --[[ auth ]] {},
            --[[ auth_history ]] {},
            --[[ last_modified ]] 0,
        },
    })
end

g.test_func = function(cg)
    local lua_func_body = 'function(code) return assert(loadstring(code))() end'
    assert_equals_with_placeholders(cg.space._func, {
        {
            --[[ 1: id ]] 1,
            --[[ 2: owner ]] 1,
            --[[ 3: name ]] 'box.schema.user.info',
            --[[ 4: setuid ]] 0,
            --[[ 5: language ]] 'LUA',
            --[[ 6: body ]] '',
            --[[ 7: routine_type ]] 'function',
            --[[ 8: param_list ]] {},
            --[[ 9: returns ]] 'any',
            --[[ 10: aggregate ]] 'none',
            --[[ 11: sql_data_access ]] 'none',
            --[[ 12: is_deterministic ]] false,
            --[[ 13: is_sandboxed ]] false,
            --[[ 14: is_null_call ]] true,
            --[[ 15: exports ]] {'LUA'},
            --[[ 16: opts ]] {},
            --[[ 17: comment ]] '',
            --[[ 18: created ]] '<datetime_str>',
            --[[ 19: last_altered ]] '<datetime_str>',
            --[[ 20: trigger ]] {},
        },
        {
            --[[ 1: id ]] 65,
            --[[ 2: owner ]] 1,
            --[[ 3: name ]] 'LUA',
            --[[ 4: setuid ]] 0,
            --[[ 5: language ]] 'LUA',
            --[[ 6: body ]] lua_func_body,
            --[[ 7: routine_type ]] 'function',
            --[[ 8: param_list ]] {'string'},
            --[[ 9: returns ]] 'any',
            --[[ 10: aggregate ]] 'none',
            --[[ 11: sql_data_access ]] 'none',
            --[[ 12: is_deterministic ]] false,
            --[[ 13: is_sandboxed ]] false,
            --[[ 14: is_null_call ]] true,
            --[[ 15: exports ]] {'LUA', 'SQL'},
            --[[ 16: opts ]] {},
            --[[ 17: comment ]] '',
            --[[ 18: created ]] '<datetime_str>',
            --[[ 19: last_altered ]] '<datetime_str>',
            --[[ 20: trigger ]] {},
        },
    })
end

g.test_priv = function(cg)
    -- Users/roles.
    local GUEST = 0
    local ADMIN = 1
    local PUBLIC = 2
    local REPLICATION = 3
    local SUPER = 31

    -- Funcs.
    local BOX_SCHEMA_USER_INFO = 1
    local LUA = 65

    -- System views/spaces.
    local VCOLLATION = 277
    local VSPACE = 281
    local VSEQUENCE = 286
    local VINDEX = 289
    local VFUNC = 297
    local VUSER = 305
    local VPRIV = 313
    local TRUNCATE = 330
    local VSPACE_SEQUENCE = 341
    local SESSION_SETTINGS = 380
    local CLUSTER = 320
    local GC_CONSUMERS = 388

    -- Privileges.
    local R = 1
    local W = 2
    local RW = 3
    local X = 4
    local SU = 24 -- session and usage
    local ALL = 4294967295

    t.assert_equals(cg.space._priv, {
        -- Format: grantor, grantee, object_type, object_id, privilege.
        {ADMIN, GUEST, 'role', PUBLIC, X},
        {ADMIN, GUEST, 'universe', 0, SU},
        {ADMIN, ADMIN, 'universe', 0, ALL},
        {ADMIN, PUBLIC, 'function', BOX_SCHEMA_USER_INFO, X},
        {ADMIN, PUBLIC, 'function', LUA, R},
        {ADMIN, PUBLIC, 'space', VCOLLATION, R},
        {ADMIN, PUBLIC, 'space', VSPACE, R},
        {ADMIN, PUBLIC, 'space', VSEQUENCE, R},
        {ADMIN, PUBLIC, 'space', VINDEX, R},
        {ADMIN, PUBLIC, 'space', VFUNC, R},
        {ADMIN, PUBLIC, 'space', VUSER, R},
        {ADMIN, PUBLIC, 'space', VPRIV, R},
        {ADMIN, PUBLIC, 'space', TRUNCATE, W},
        {ADMIN, PUBLIC, 'space', VSPACE_SEQUENCE, R},
        {ADMIN, PUBLIC, 'space', SESSION_SETTINGS, RW},
        {ADMIN, REPLICATION, 'space', CLUSTER, W},
        {ADMIN, REPLICATION, 'space', GC_CONSUMERS, W},
        {ADMIN, REPLICATION, 'universe', 0, R},
        {ADMIN, SUPER, 'universe', 0, ALL},
    })
end
