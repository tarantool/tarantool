local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_each(function(cg)
    cg.server = server:new{alias = 'downgrade', box_cfg = {}}
    cg.server:start()
end)

g.after_each(function(cg)
    cg.server:drop()
end)

g.test_downgrade_versions = function(cg)
    cg.server:exec(function()
        local versions = box.schema.downgrade_versions()
        local helper = require('test.box-luatest.downgrade_helper')

        -- ascending order
        local function version_cmp(a, b)
            return helper.parse_version(a) < helper.parse_version(b)
        end

        -- Check versions are listed in descending order and indirectly test
        -- items format too.
        local sorted = table.copy(versions)
        table.sort(sorted, version_cmp)
        t.assert_equals(versions, sorted)
    end)
end

g.test_downgrade_sanity_checks = function(cg)
    cg.server:exec(function()
        -- Return schema version from _schema space in string format
        -- like '2.10.4'.
        local function schema_version()
            local t = box.space._schema:get{'version'}
            return string.format('%d.%d.%d', t[2], t[3], t[4])
        end

        local version = schema_version()
        t.assert_error_msg_contains('version should be in format A.B.C',
                                    box.schema.downgrade, 'xxx')
        t.assert_equals(schema_version(), version)
        t.assert_error_msg_contains('version should be in format A.B.C',
                                    box.schema.downgrade, '2')
        t.assert_equals(schema_version(), version)
        t.assert_error_msg_contains('version should be in format A.B.C',
                                    box.schema.downgrade, '2.2')
        t.assert_equals(schema_version(), version)
        t.assert_error_msg_contains('version should be in format A.B.C',
                                    box.schema.downgrade, '2.2.2.2')
        t.assert_equals(schema_version(), version)

        t.assert_error_msg_contains(
            'Downgrade is only possible to version listed in' ..
            ' box.schema.downgrade_versions().',
            box.schema.downgrade, '2.9.255')
        t.assert_equals(schema_version(), version)

        -- Check that after downgrade schema version is in accordance
        -- with list of existent versions.
        box.schema.downgrade("2.10.4")
        t.assert_equals(schema_version(), "2.10.4")
        box.schema.downgrade("2.10.3")
        t.assert_equals(schema_version(), "2.10.1")
        box.schema.downgrade("2.8.2")
        t.assert_equals(schema_version(), "2.7.1")

        t.assert_error_msg_contains(
            'Cannot downgrade as current schema version 2.7.1 is older' ..
            ' then schema version 2.9.1 for Tarantool 2.10.0',
            box.schema.downgrade, '2.10.0')
        t.assert_equals(schema_version(), "2.7.1")
    end)
end

g.test_downgrade_from_more_recent_version = function(cg)
    cg.server:exec(function()
        local tarantool = require('tarantool')
        local function schema_version()
            local t = box.space._schema:get{'version'}
            return string.format('%d.%d.%d', t[2], t[3], t[4])
        end

        local app_version = tarantool.version:match('^%d+%.%d+%.%d+')
        local major, minor, patch = app_version:match('(%d+)%.(%d+)%.(%d+)')
        box.space._schema:replace({'version', tonumber(major), tonumber(minor),
                                   tonumber(patch) + 1})
        local new_version = schema_version()
        local err = 'Cannot downgrade as current schema version %s is newer' ..
                    ' than Tarantool version %s'
        t.assert_error_msg_contains(err:format(new_version, app_version),
                                    box.schema.downgrade,
                                    box.schema.downgrade_versions()[1])
        t.assert_equals(schema_version(), new_version)
    end)
end

g.test_downgrade_and_upgrade = function(cg)
    cg.server:exec(function()
        box.schema.downgrade("2.8.2")
        -- Snapshot is required here (and is documented as required in downgrade
        -- process). Otherwise on start we will load SQL_BUILTIN functions
        -- added by downgrade from 2.9.1 while current schema version is
        -- recent one. And this is prohibited.
        box.snapshot()
    end)
    cg.server:stop()
    cg.server:start()
end

-----------------------------
-- Check downgrade from 2.9.1
-----------------------------

-- Manual check:
--
--  Tarantool 2.11.0-entrypoint-851-gc5dc51262
--  type 'help' for interactive help
--  tarantool> box.cfg{}
--  tarantool> box.schema.downgrade('2.8.2')
--  tarantool> box.snapshot()
--  tarantool> os.exit()
--
--  Tarantool 2.8.4-0-g47e6bd362
--  tarantool> box.cfg{}
--  tarantool> \set language sql
--  tarantool> CREATE TABLE t (s INTEGER PRIMARY KEY, CHECK(LEAST(s, 10) = 10));
--   tarantool> insert into t values (1);
--  ---
--  - null
--  - 'Check constraint failed ''ck_unnamed_T_1'': LEAST(s, 10) = 10'
--  ...
--  tarantool> tarantool> insert into t values (11);
--  ---
--  - row_count: 1
--  ...
--  tarantool> os.exit()
--
-- Without restoring builtin sql function creating table fails:
--
--  tarantool> CREATE TABLE t (s INTEGER PRIMARY KEY, CHECK(LEAST(s, 10) = 10));
--  ---
--  - null
--  - 'Failed to create check constraint ''ck_unnamed_T_1'': Function ''LEAST''
--    does not exist'
--  ...
g.test_downgrade_insert_back_sql_builtin_functions = function(cg)
    cg.server:exec(function()
        local fun = require('fun')
        local helper = require('test.box-luatest.downgrade_helper')

        box.space._func:create_index('__language',
                                     {parts = {{5, 'string'}}, unique = false})
        local rows = box.space._func.index.__language:select('SQL_BUILTIN')
        t.assert_equals(#rows, 0)

        local app_version = helper.app_version('2.9.1')
        t.assert_equals(box.schema.downgrade_issues(app_version), {})
        box.schema.downgrade(app_version)
        rows = box.space._func.index.__language:select('SQL_BUILTIN')
        t.assert_equals(#rows, 0)

        local prev_version = helper.prev_version('2.9.1')
        t.assert_equals(box.schema.downgrade_issues(prev_version), {})
        rows = box.space._func.index.__language:select('SQL_BUILTIN')
        t.assert_equals(#rows, 0)

        local names_expected = {
            "TRIM", "TYPEOF", "PRINTF", "UNICODE", "CHAR", "HEX", "VERSION",
            "QUOTE", "REPLACE", "SUBSTR", "GROUP_CONCAT", "JULIANDAY", "DATE",
            "TIME", "DATETIME", "STRFTIME", "CURRENT_TIME",
            "CURRENT_TIMESTAMP", "CURRENT_DATE", "LENGTH", "POSITION", "ROUND",
            "UPPER", "LOWER", "IFNULL", "RANDOM", "CEIL", "CEILING",
            "CHARACTER_LENGTH", "CHAR_LENGTH", "FLOOR", "MOD", "OCTET_LENGTH",
            "ROW_COUNT", "COUNT", "LIKE", "ABS", "EXP", "LN", "POWER", "SQRT",
            "SUM", "TOTAL", "AVG", "RANDOMBLOB", "NULLIF", "ZEROBLOB", "MIN",
            "MAX", "COALESCE", "EVERY", "EXISTS", "EXTRACT", "SOME", "GREATER",
            "LESSER", "SOUNDEX", "LIKELIHOOD", "LIKELY", "UNLIKELY",
            "_sql_stat_get", "_sql_stat_push", "_sql_stat_init", "GREATEST",
            "LEAST",
        }

        box.schema.downgrade(prev_version)
        rows = box.space._func.index.__language:select('SQL_BUILTIN')
        local names_actual = fun.totable(fun.map(function(t) return t.name end,
                                                 rows))
        t.assert_equals(names_actual, names_expected)

        -- idempotence check
        box.schema.downgrade(prev_version)
        rows = box.space._func.index.__language:select('SQL_BUILTIN')
        local names_actual = fun.totable(fun.map(function(t) return t.name end,
                                                 rows))
        t.assert_equals(names_actual, names_expected)
    end)
end

------------------------------
-- Check downgrade from 2.10.0
------------------------------

-- Manual check:
--
--  Tarantool Enterprise 2.11.0-entrypoint-113-g803baaf
--  type 'help' for interactive help
--  tarantool> box.cfg{}
--  tarantool> box.schema.space.create('test', {
--      engine = 'vinyl',
--      defer_deletes = true,
--  })
--  tarantool> box.space._space:get{box.space.test.id}.flags
--  ---
--  - {'defer_deletes': true}
--  ...
--  tarantool> box.schema.downgrade('2.9.1')
--  tarantool> box.space._space:get{box.space.test.id}.flags
--  ---
--  - {}
--  ...
g.test_downgrade_deferred_deletes = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        local test = box.schema.space.create('test', {engine = 'vinyl'})
        local flags = box.space._space:get{test.id}.flags
        flags.defer_deletes = true
        box.space._space:update(test.id, {{'=', 'flags', flags}})

        local app_version = helper.app_version('2.10.0')
        t.assert_equals(box.schema.downgrade_issues(app_version), {})
        box.schema.downgrade('2.10.0')
        t.assert_equals(box.space._space:get{test.id}.flags.defer_deletes, true)

        local prev_version = helper.prev_version('2.10.0')
        t.assert_equals(box.schema.downgrade_issues(prev_version), {})
        t.assert_equals(box.space._space:get{test.id}.flags.defer_deletes, true)

        box.schema.downgrade(prev_version)
        t.assert_equals(box.space._space:get{test.id}.flags.defer_deletes, nil)

        -- idempotence check
        box.schema.downgrade(prev_version)
        t.assert_equals(box.space._space:get{test.id}.flags.defer_deletes, nil)
    end)
end

-- Manual check:
--
--  Tarantool Enterprise 2.11.0-entrypoint-113-g803baaf
--  type 'help' for interactive help
--  tarantool> box.cfg{}
--  tarantool> box.schema.space.create('city')
--  tarantool> box.space.city:format{{name='payload', type='string'}}
--  tarantool> box.space.city:create_index('payload')
--  tarantool> box.space.city:replace{'moscow'}
--  tarantool> box.schema.func.create('capitalize', {
--      language = 'LUA',
--      is_deterministic = true,
--      body = [[function(row) error('boom') end]],
--  })
--  tarantool> box.space.city:upgrade{func = 'capitalize'}
--  tarantool> box.schema.downgrade('2.9.1')
--  ---
--  - error: 'builtin/box/upgrade.lua:1861: Background update is active in space
--      ''city''. It is supported starting from version 2.10.0.'
--  ...
g.test_downgrade_space_upgrade = function(cg)
    t.tarantool.skip_if_not_enterprise()
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        local name = box.schema.space.create('name')
        name:format{{name='payload', type='string'}}
        name:create_index('payload')

        local city = box.schema.space.create('city')
        city:format{{name='payload', type='string'}}
        city:create_index('payload')

        name:replace{'ivan'}
        city:replace{'moscow'}

        box.schema.func.create('capitalize', {
            language = 'LUA',
            is_deterministic = true,
            body = [[function(row) error('boom') end]],
        })

        local flags = {
            upgrade = {
                func = box.func.capitalize.id,
                owner = box.info.uuid,
            },
        }
        box.space._space:update(name.id, {{'=', 'flags', flags}})
        box.space._space:update(city.id, {{'=', 'flags', flags}})

        helper.check_issues('2.10.0', {
            "Background update is active in space 'name'. " ..
            "It is supported starting from version 2.10.0.",
            "Background update is active in space 'city'. " ..
            "It is supported starting from version 2.10.0.",
        })
    end)
end

-- Manual check:
--
--  Tarantool Enterprise 2.11.0-entrypoint-113-g803baaf
--  type 'help' for interactive help
--  tarantool> box.cfg{}
--  tarantool> box.schema.space.create('word')
--  tarantool> box.space.word:format{
--      {name = 'payload', type = 'string', compression = 'zstd'}
--  }
--  tarantool> box.schema.downgrade('2.9.1')
--  ---
--  - error: 'builtin/box/upgrade.lua:1861: Tuple compression is found in space
--      ''word'', field ''payload''. It is supported starting from version
--      2.10.0.'
--  ...
g.test_downgrade_tuple_compression = function(cg)
    t.tarantool.skip_if_not_enterprise()
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        local word = box.schema.space.create('word')
        local format = {
            {name = 'payload', type = 'string', compression = 'zstd'},
        }
        box.space._space:update(word.id, {{'=', 'format', format}})

        local sentence = box.schema.space.create('sentence')
        format = {
            {name = 'payload', type = 'string', compression = 'lz4'},
        }
        box.space._space:update(sentence.id, {{'=', 'format', format}})

        helper.check_issues('2.10.0', {
            "Tuple compression is found in space 'word', field 'payload'. " ..
            "It is supported starting from version 2.10.0.",
            "Tuple compression is found in space 'sentence', field" ..
            " 'payload'. It is supported starting from version 2.10.0.",
        })
    end)
end

-- Manual check:
--
--  Tarantool Enterprise 2.11.0-entrypoint-113-g803baaf
--  type 'help' for interactive help
--  tarantool> box.cfg{}
--  tarantool> box.schema.space.create('person')
--  tarantool> box.space.person:format{{name = 'id', type = 'unsigned'},
--                          {name = 'name', type = 'string'}}
--  tarantool> box.schema.space.create('participant')
--  tarantool> box.space.participant:format{
--      {name = 'id', type = 'unsigned'},
--      {
--          name = 'person_id',
--          foreign_key = {space = 'person', field = 'id'},
--      },
--  }
--  tarantool> box.schema.downgrade('2.9.1')
--  ---
--  - error: 'builtin/box/upgrade.lua:1861: Foreign key constraint is found in
--      space ''participant'', field ''person_id''. It is supported starting
--      from version 2.10.0.'
--  ...
g.test_downgrade_field_foreign_key_contraint = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        local person = box.schema.space.create('person')
        person:format{{name = 'id', type = 'unsigned'},
                      {name = 'name', type = 'string'}}

        local participant = box.schema.space.create('participant')
        local format = {
            {name = 'id', type = 'unsigned'},
            {
                name = 'person_id',
                foreign_key = {person = {space = person.id, field = 'id'}},
            },
        }
        box.space._space:update(participant.id, {{'=', 'format', format}})

        local developer = box.schema.space.create('developer')
        box.space._space:update(developer.id, {{'=', 'format', format}})

        helper.check_issues('2.10.0', {
            "Foreign key constraint is found in space 'participant'," ..
            " field 'person_id'. It is supported starting from version 2.10.0.",
            "Foreign key constraint is found in space 'developer'," ..
            " field 'person_id'. It is supported starting from version 2.10.0.",
        })
    end)
end

-- Manual check:
--
--  Tarantool Enterprise 2.11.0-entrypoint-113-g803baaf
--  type 'help' for interactive help
--  tarantool> box.cfg{}
--  tarantool> box.schema.func.create('azimuth_normalized', {
--      language = 'LUA',
--      is_deterministic = true,
--      body = [[function(value)
--          return normalize_somehow(value)
--      end]],
--  })
--  tarantool> box.schema.space.create('movement2d')
--  tarantool> box.space.movement2d:format{
--      {name = 'id', type = 'unsigned'},
--      {name = 'distance', type = 'number'},
--      {
--          name = 'azimuth',
--          type = 'number',
--          constraint = 'azimuth_normalized',
--      },
--  }
--  ---
--  - error: 'builtin/box/upgrade.lua:1861: Field constraint is found in space
--      ''movement2d'', field ''azimuth''. It is supported starting from version
--      2.10.0.'
--  ...
g.test_downgrade_field_contraint = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        box.schema.func.create('azimuth_normalized', {
            language = 'LUA',
            is_deterministic = true,
            body = [[function(value)
                return normalize_somehow(value)
            end]],
        })

        local movement2d = box.schema.space.create('movement2d')
        local format = {
            {name = 'id', type = 'unsigned'},
            {name = 'distance', type = 'number'},
            {
                name = 'azimuth',
                type = 'number',
                -- azimuth is in [0, 2 * PI)
                constraint = {normalized = box.func.azimuth_normalized.id}
            },
        }
        box.space._space:update(movement2d.id, {{'=', 'format', format}})

        local movement3d = box.schema.space.create('movement3d')
        format = {
            {name = 'id', type = 'unsigned'},
            {name = 'distance', type = 'number'},
            {name = 'polar', type = 'number'},
            {
                name = 'azimuth',
                type = 'number',
                -- azimuth is in [0, 2 * PI)
                constraint = {normalized = box.func.azimuth_normalized.id}
            },
        }
        box.space._space:update(movement3d.id, {{'=', 'format', format}})

        helper.check_issues('2.10.0', {
            "Field constraint is found in space 'movement2d'," ..
            " field 'azimuth'. It is supported starting from version 2.10.0.",
            "Field constraint is found in space 'movement3d'," ..
            " field 'azimuth'. It is supported starting from version 2.10.0.",
        })
    end)
end

-- Manual check:
--
--  Tarantool Enterprise 2.11.0-entrypoint-113-g803baaf
--  type 'help' for interactive help
--  tarantool> box.cfg{}
--  tarantool> box.schema.func.create('in_box', {
--      language = 'LUA',
--      is_deterministic = true,
--      body = [[function(row)
--          return row.x > 0 and row.x < 1 and row.y > 0 and row.y < 1
--      end]],
--  })
--  tarantool> box.schema.space.create('pos_in_box', {constraint = 'in_box'})
--  tarantool> box.space.pos_in_box:format{{name = 'id', type = 'unsigned'},
--                              {name = 'x', type = 'number'},
--                              {name = 'y', type = 'number'}}
--  tarantool> box.schema.downgrade('2.9.1')
--  ---
--  - error: 'builtin/box/upgrade.lua:1861: Tuple constraint is found in space
--      ''pos_in_box''. It is supported starting from version 2.10.0.'
--  ...
g.test_downgrade_tuple_contraint = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        box.schema.func.create('in_box', {
            language = 'LUA',
            is_deterministic = true,
            body = [[function(row)
                return row.x > 0 and row.x < 1 and row.y > 0 and row.y < 1
            end]],
        })
        local pos_in_box = box.schema.space.create('pos_in_box')
        pos_in_box:format{{name = 'id', type = 'unsigned'},
                          {name = 'x', type = 'number'},
                          {name = 'y', type = 'number'}}
        local flags = {constraint = {in_box = box.func.in_box.id}}
        box.space._space:update(pos_in_box.id, {{'=', 'flags', flags}})

        box.schema.func.create('in_circle', {
            language = 'LUA',
            is_deterministic = true,
            body = [[function(row)
                return row.x * row.x + row.y * row.y < 1
            end]],
        })
        local pos_in_circle = box.schema.space.create('pos_in_circle')
        pos_in_box:format{{name = 'id', type = 'unsigned'},
                          {name = 'x', type = 'number'},
                          {name = 'y', type = 'number'}}
        local flags = {constraint = {in_circle = box.func.in_circle.id}}
        box.space._space:update(pos_in_circle.id, {{'=', 'flags', flags}})

        helper.check_issues('2.10.0', {
            "Tuple constraint is found in space 'pos_in_box'. " ..
            "It is supported starting from version 2.10.0.",
            "Tuple constraint is found in space 'pos_in_circle'. " ..
            "It is supported starting from version 2.10.0.",
        })
    end)
end

-- Check that FOREIGN KEY constraints could be downgraded.
g.test_downgrade_sql_tuple_foreign_keys = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        box.execute([[CREATE TABLE t(i INT PRIMARY KEY, a INT, UNIQUE(a, i));]])
        box.execute([[ALTER TABLE t ADD CONSTRAINT f ]]..
                    [[FOREIGN KEY (i, a) REFERENCES t(a, i);]])

        local function foreign_key_before()
            local space = box.space._space.index.name:get{'T'}
            t.assert(space.flags.foreign_key ~= nil)
            t.assert(space.flags.foreign_key.F ~= nil)
            t.assert_equals(space.flags.foreign_key.F.space, space.id)
            t.assert_equals(space.flags.foreign_key.F.field, {[0] = 1, [1] = 0})
        end

        local function foreign_key_after()
            local space = box.space._space.index.name:get{'T'}
            t.assert(space.flags.foreign_key == nil)
            local result = box.space._fk_constraint:get{'F', space.id}
            local expected = {'F', space.id, space.id, false, 'full',
                              'no_action', 'no_action', {0, 1}, {1, 0}}
            t.assert_equals(result, expected)
        end

        foreign_key_before()

        local app_version = helper.app_version('2.10.0')
        t.assert_equals(box.schema.downgrade_issues(app_version), {})
        box.schema.downgrade(app_version)
        foreign_key_before()

        local prev_version = helper.prev_version('2.10.0')
        t.assert_equals(box.schema.downgrade_issues(prev_version), {})
        foreign_key_before()

        box.schema.downgrade(prev_version)
        foreign_key_after()

        -- idempotence check
        box.schema.downgrade(prev_version)
        foreign_key_after()
    end)
end

g.test_downgrade_sql_tuple_foreign_keys = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        local opts = {body = 'function() return end', takes_raw_args = true}
        box.schema.func.create('foo', opts)
        box.schema.func.create('bar', opts)

        local foo = box.space._func.index.name:get('foo')
        t.assert_equals(foo.opts.takes_raw_args, true)

        helper.check_issues('2.10.0', {
            "takes_raw_args option is set for function 'foo'" ..
            " It is supported starting from version 2.10.0.",
            "takes_raw_args option is set for function 'bar'" ..
            " It is supported starting from version 2.10.0.",
        })
    end)
end

------------------------------
-- Check downgrade from 2.10.5
------------------------------

-- No manual check required
g.test_downgrade_vspace_sequence = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        local function get_defs()
            return {
                box.space._space:get{box.schema.VSPACE_SEQUENCE_ID},
                box.space._index:select{box.schema.VSPACE_SEQUENCE_ID},
                grant = box.space._priv:get{
                    -- 2 is PUBLIC
                    2, 'space', box.schema.VSPACE_SEQUENCE_ID},
            }
        end

        local defs = get_defs()
        local app_version = helper.app_version('2.10.5')
        t.assert_equals(box.schema.downgrade_issues(app_version), {})
        t.assert_equals(get_defs(), defs)

        local prev_version = helper.prev_version('2.10.5')
        t.assert_equals(box.schema.downgrade_issues(prev_version), {})
        t.assert_equals(get_defs(), defs)

        local nodefs = { nil, {}, nil}
        box.schema.downgrade(prev_version)
        t.assert_equals(get_defs(), nodefs)

        -- idempotence check
        box.schema.downgrade(prev_version)
        t.assert_equals(get_defs(), nodefs)
    end)
end

------------------------------
-- Check downgrade from 2.11.0
------------------------------

-- Manual check:
--
--  Tarantool Enterprise 2.11.0-entrypoint-113-g803baaf
--  type 'help' for interactive help
--  tarantool> box.cfg{}
--  tarantool> box.schema.space.create('word')
--  tarantool> box.space.word:format{{name = 'payload',
--                                    type = 'string',
--                                    compression = 'zlib'}}
--  tarantool> box.schema.downgrade('2.10.0')
--  ---
--  - error: 'builtin/box/upgrade.lua:1861: Tuple compression with ''zlib'' algo
--      is found in space ''word'', field ''payload''. It is supported starting
--      from version 2.11.0.'
--  ...
g.test_downgrade_zlib_compression = function(cg)
    t.tarantool.skip_if_not_enterprise()
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        local format = {
            {name = 'payload', type = 'string', compression = 'zlib'},
        }

        local word = box.schema.space.create('word')
        box.space._space:update(word.id, {{'=', 'format', format}})
        local sentence = box.schema.space.create('sentence')
        box.space._space:update(sentence.id, {{'=', 'format', format}})

        helper.check_issues('2.11.0', {
            "Tuple compression with 'zlib' algo is found in space 'word'," ..
            " field 'payload'. It is supported starting from version 2.11.0.",
            "Tuple compression with 'zlib' algo is found in space" ..
            " 'sentence', field 'payload'. It is supported starting from" ..
            " version 2.11.0.",
        })
    end)
end

-- Manual check:
--
--  Tarantool Enterprise 2.11.0-entrypoint-113-g803baaf
--  type 'help' for interactive help
--  tarantool> box.cfg{}
--  tarantool> box.schema.func.create('is_true', {body = 'field',
--                                                language = 'SQL_EXPR'})
--  tarantool> box.schema.downgrade('2.10.0')
--  ---
--  - error: 'builtin/box/upgrade.lua:1861: Function ''is_true'' has language
--      type SQL_EXPR. It is supported starting from version 2.11.0.'
--  ...
g.test_downgrade_sql_expr_function = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        local datetime = os.date("%Y-%m-%d %H:%M:%S")
        local create_func = function(name, body)
            local empty = setmetatable({}, { __serialize = 'map' })
            box.space._func:auto_increment{
                1, name, 1, 'SQL_EXPR', body, 'function', {},
                'any', 'none', 'none', true, true, true, {'LUA'},
                empty, '', datetime, datetime, {}
            }
        end

        create_func('is_a', "field == 'a'")
        create_func('is_b', "field == 'b'")

        helper.check_issues('2.11.0', {
            "Function 'is_a' has language type SQL_EXPR. " ..
            "It is supported starting from version 2.11.0.",
            "Function 'is_b' has language type SQL_EXPR. " ..
            "It is supported starting from version 2.11.0.",
        })
    end)
end

-- Manual check
--
--  Tarantool Enterprise 2.11.0-entrypoint-113-g803baaf
--  type 'help' for interactive help
--  tarantool> box.cfg{auth_type = 'pap-sha256'}
--  tarantool> box.schema.user.create('alice', {password = 'secret'})
--  tarantool> box.schema.downgrade('2.10.0')
--  ---
--  - error: 'builtin/box/upgrade.lua:1861: Auth type ''pap-sha256'' is found
--      for user ''alice''. It is supported starting from version 2.11.0.'
--  ...
g.test_downgrade_non_chap_sha1_auth = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        -- ALICE password with some salt
        local auth = {
            ['pap-sha256'] = {
                'WOi6OgOXNUwxr0YU1sR9/rzJYnU=',
                'ZfRPND3Q0ls0hmKuzcPKBJngvDrJx6cooa8HEjj4MH0=',
            },
        }
        box.space._user:auto_increment{1, 'alice', 'user', auth, {}, os.time()}
        -- BOB password with some salt
        auth = {
            ['pap-sha256'] = {
                'FIXvGZk8bQmNztFmsCeTlpSxobk=',
                '87wjwffInpRGMBNATigbXcmKMC9sdwgplKZRjqBcdxQ=',
            },
        }
        box.space._user:auto_increment{1, 'bob', 'user', auth, {}, os.time()}

        helper.check_issues('2.11.0', {
            "Auth type 'pap-sha256' is found for user 'alice'. " ..
            "It is supported starting from version 2.11.0.",
            "Auth type 'pap-sha256' is found for user 'bob'. " ..
            "It is supported starting from version 2.11.0.",
        })
    end)
end

-- No manual check required
g.test_downgrade_user_auth_history_and_last_modified = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        local spaces_ids = {box.space._user.id, box.space._vuser.id}
        local function check_has_new_auth_fields()
            for _, row in box.space._user:pairs() do
                t.assert_equals(#row, 7)
                t.assert_equals(row.auth_history, {})
                t.assert_equals(row.last_modified, 0)
            end
            for _, id in pairs(spaces_ids) do
                local row = box.space._space:get(id)
                t.assert_equals(row[7][6],
                                {name = 'auth_history', type = 'array'})
                t.assert_equals(row[7][7],
                                {name = 'last_modified', type = 'unsigned'})
            end
        end

        local function check_NO_new_auth_fields()
            for _, row in box.space._user:pairs() do
                t.assert_equals(#row, 5)
            end
            for _, id in pairs(spaces_ids) do
                local row = box.space._space:get(id)
                t.assert_equals(row[7][6], nil)
                t.assert_equals(row[7][7], nil)
            end
        end

        local app_version = helper.app_version('2.11.0')
        t.assert_equals(box.schema.downgrade_issues(app_version), {})
        box.schema.downgrade(app_version)
        check_has_new_auth_fields()

        local prev_version = helper.prev_version('2.11.0')
        t.assert_equals(box.schema.downgrade_issues(prev_version), {})
        check_has_new_auth_fields()

        box.schema.downgrade(prev_version)
        check_NO_new_auth_fields()

        -- idempotence check
        box.schema.downgrade(prev_version)
        check_NO_new_auth_fields()
    end)
end

-- Check that SQL CHECK constraints could be downgraded.
g.test_downgrade_sql_tuple_check_contraints = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        box.execute([[CREATE TABLE T(I INT PRIMARY KEY, A INT);]])
        box.execute([[ALTER TABLE T ADD CONSTRAINT C CHECK (A > 10);]])

        local function constraint_before()
            local space = box.space._space.index.name:get{'T'}
            t.assert(space.flags.constraint ~= nil)
            local func_id = space.flags.constraint.C
            t.assert(func_id ~= nil)
            local func = box.space._func:get{func_id}
            t.assert(func ~= nil)
            t.assert_equals(func.body, 'A > 10')
        end

        local function constraint_after(func_id)
            local space = box.space._space.index.name:get{'T'}
            t.assert(box.space._func:get{func_id} == nil)
            t.assert(space.flags.constraint == nil)
            local result = box.space._ck_constraint:get{space.id, 'C'}
            local expected = {space.id, 'C', false, 'SQL', 'A > 10', true}
            t.assert_equals(result, expected)
        end

        constraint_before()
        local func_id = box.space._space.index.name:get{'T'}.flags.constraint.C

        local app_version = helper.app_version('2.11.0')
        t.assert_equals(box.schema.downgrade_issues(app_version), {})
        box.schema.downgrade(app_version)
        constraint_before()

        local prev_version = helper.prev_version('2.11.0')
        t.assert_equals(box.schema.downgrade_issues(prev_version), {})
        constraint_before()

        box.schema.downgrade(prev_version)
        constraint_after(func_id)

        -- idempotence check
        box.schema.downgrade(prev_version)
        constraint_after(func_id)
    end)
end

------------------------------
-- Check downgrade from 2.11.1
------------------------------

-- Check that max_id presents in _schema after downgrade.
g.test_downgrade_schema_max_id = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')

        local function check_max_id(expected_id)
            local max_id_tuple = box.space._schema:get('max_id')
            local max_id = max_id_tuple and max_id_tuple[2]
            t.assert_equals(max_id, expected_id)
        end

        local app_version = helper.app_version('2.11.1')
        t.assert_equals(box.schema.downgrade_issues(app_version), {})
        box.schema.downgrade(app_version)
        check_max_id(nil)

        local prev_version = helper.prev_version('2.11.1')
        t.assert_equals(box.schema.downgrade_issues(prev_version), {})
        check_max_id(nil)

        -- Max id must be maximal non-system space id.
        --
        -- Note that DDL is disabled if the schema is old, see gh-7149,
        -- so we have to use the box.internal.run_schema_upgrade() helper.
        local space_id = 734
        box.internal.run_schema_upgrade(box.schema.space.create,
                                        'test', {id = space_id})
        check_max_id(nil)
        box.schema.downgrade(prev_version)
        check_max_id(space_id)

        -- Idempotence check.
        box.schema.downgrade(prev_version)
        check_max_id(space_id)

        box.schema.upgrade()
        box.space.test:drop()

        -- Max id must be SYSTEM_ID_MAX in the case all the spaces are system.
        space_id = box.schema.SYSTEM_ID_MAX
        box.schema.downgrade(prev_version)
        check_max_id(space_id)

        -- Idempotence check.
        box.schema.downgrade(prev_version)
        check_max_id(space_id)
    end)
end

-----------------------------
-- Check downgrade from 3.0.0
-----------------------------

g.test_downgrade_replicaset_uuid_key = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')
        local _schema = box.space._schema
        local rs_uuid = box.info.replicaset.uuid
        local prev_version = helper.prev_version(helper.app_version('3.0.0'))
        t.assert_equals(box.schema.downgrade_issues(prev_version), {})
        t.assert_equals(_schema:get{'cluster'}, nil)
        t.assert_equals(_schema:get{'replicaset_uuid'}.value, rs_uuid)
        -- 2 for idempotence.
        for _ = 1, 2 do
            box.schema.downgrade(prev_version)
            t.assert_equals(_schema:get{'cluster'}.value, rs_uuid)
            t.assert_equals(_schema:get{'replicaset_uuid'}, nil)
        end
    end)
end

g.test_downgrade_global_names = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')
        box.cfg{
            force_recovery = true,
            cluster_name = 'test'
        }
        local prev_version = helper.prev_version(helper.app_version('3.0.0'))
        local issues = box.schema.downgrade_issues(prev_version)
        t.assert_str_contains(issues[1], 'Cluster name is set')
        box.space._schema:delete{'cluster_name'}
        box.cfg{
            cluster_name = box.NULL,
            replicaset_name = 'test'
        }
        issues = box.schema.downgrade_issues(prev_version)
        t.assert_str_contains(issues[1], 'Replicaset name is set')
        box.space._schema:delete{'replicaset_name'}
        box.cfg{
            replicaset_name = box.NULL,
            instance_name = 'test',
        }
        issues = box.schema.downgrade_issues(prev_version)
        t.assert_str_contains(issues[1], 'Instance name is set')
        box.space._cluster:update({box.info.id}, {{'#', 'name', 1}})
        box.cfg{
            instance_name = box.NULL,
            force_recovery = false,
        }

        local format = box.space._cluster:format()
        t.assert_equals(#format, 3)
        t.assert_equals(format[3].name, 'name')
        box.schema.downgrade(prev_version)
        t.assert_equals(#box.space._cluster:format(), 2)
    end)
end

g.test_downgrade_func_trigger = function(cg)
    cg.server:exec(function()
        local helper = require('test.box-luatest.downgrade_helper')
        local trigger = require('trigger')
        local _func = box.space._func
        local prev_version = helper.prev_version(helper.app_version('3.0.0'))
        local lua_code = 'function(a, b) return a + b end'
        t.assert_equals(trigger.info('test'), {})
        box.schema.func.create('trigger_func',
            {body = lua_code, trigger = 'test'})
        t.assert_equals(box.schema.downgrade_issues(prev_version),
            {"Func trigger_func has trigger option. " ..
             "It is supported from version 3.0.0"})
        box.schema.func.drop('trigger_func')
        box.schema.func.create('non_trigger_func',
            {body = lua_code, trigger = {}})
        t.assert_equals(box.schema.downgrade_issues(prev_version), {})
        t.assert_equals(#_func:format(), 20)
        t.assert_equals(_func:format()[20].name, 'trigger')
        -- 2 for idempotence.
        for _ = 1, 2 do
            box.schema.downgrade(prev_version)
            t.assert_equals(#_func:format(), 19)
            t.assert_equals(trigger.info('test'), {})
            local func_tuple = _func.index.name:get('non_trigger_func')
            t.assert_equals(#func_tuple, 19)
        end
    end)
end
