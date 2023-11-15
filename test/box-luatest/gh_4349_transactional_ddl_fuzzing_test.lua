local server = require('luatest.server')
local t = require('luatest')

local g = t.group('generic')

g.before_all(function(cg)
    cg.server = server:new({box_cfg = {memtx_use_mvcc_engine = false}})
    cg.server:start()
end)

g.after_all(function(cg)
    cg.server:drop()
end)

-- Stress-test the transactional DDL.
g.test_stress = function(cg)
    cg.server:exec(function()
        box.test_storage = {}

        local str = require('luatest.pp').tostring

        -- Park-Miller RNG with 32-bit operations only:
        -- https://en.wikipedia.org/wiki/Lehmer_random_number_generator
        local function getrandom(seed)
            local state = seed % 0x7fffffff
            return function(a, b)
                -- Precomputed parameters for Schrage's method.
                local M = 0x7fffffff
                local A = 48271
                local Q = 44488 -- M / A
                local R = 3399  -- M % A

                local div = math.floor(state / Q) -- max: M / Q = A = 48,271
                local rem = state % Q             -- max: Q - 1     = 44,487

                local s = rem * A -- max: 44,487 * 48,271 = 2,147,431,977
                local t = div * R -- max: 48,271 *  3,399 =   164,073,129
                local rand_int = s - t;

                if rand_int < 0 then
                    rand_int = rand_int + M
                end

                state = rand_int

                if b == nil then
                    a, b = 1, a
                end
                local range = b - a
                local result = a + (rand_int % (range + 1))
                return result
            end
        end

        local seed = os.time()
        local randomnumber = getrandom(seed)
        print('Random seed: ' .. seed)

        local NUM_FIBERS = 9
        local NUM_NAMES = 9
        local NUM_TUPLE_CONSTRAINTS = 3
        local finish_on_fail = { value = false }
        local fiber = require('fiber')
        local errinj = box.error.injection
        local object_types = {'space', 'user', 'role', 'sequence', 'function'}
        local ddl_functions = {}
        local random = {}

        local function log(msg)
            io.write('--[[' .. fiber.self().storage['id'] .. ']] ' .. msg .. '\n')
            io.flush()
        end

        -- Selects a random item in the range array[1..N], where N is integer.
        -- Does not select the 0-item or any of non-integer key values.
        local function random_of(array)
            local random_i = randomnumber(#array)
            return array[random_i]
        end

        -- Selects a random non-system space.
        local function random_space()
            local spaces = {}
            for k, v in pairs(box.space) do
                if type(k) == 'number' and k >= 512 then
                    table.insert(spaces, v)
                end
            end
            if #spaces == 0 then
                return false, '<unexisting space>'
            end
            table.sort(spaces, function(a, b) return a.name < b.name end)
            local space = random_of(spaces)
            return space, space.name
        end

        local function random_sequence()
            local objects = {}
            for k, v in pairs(box.sequence) do
                if type(k) == 'number' then
                    table.insert(objects, v)
                end
            end
            if #objects == 0 then
                return false, '<unexisting sequence>'
            end
            table.sort(objects, function(a, b) return a.name < b.name end)
            local object = random_of(objects)
            return object, object.name
        end

        local function random_function()
            local objects = {}
            for k, v in pairs(box.func) do
                if type(k) == 'number' and
                   string.match(v.name, 'function_') ~= nil then
                    table.insert(objects, v.name)
                end
            end
            if #objects == 0 then
                return false
            end
            table.sort(objects)
            return random_of(objects)
        end

        local function random_user_or_role(object_type)
            -- Only select non-system entries (id >= 32).
            local objects = box.space._user.index.primary:select(32, {
                iterator = 'ge'
            })
            if #objects == 0 then
                return false
            end
            local names = {}
            for _, v in pairs(objects) do
                if v[4] == object_type then
                    table.insert(names, v[3])
                end
            end
            if #names == 0 then
                return false
            end
            table.sort(names)
            return random_of(names)
        end

        local function random_user()
            return random_user_or_role('user')
        end

        local function random_role()
            return random_user_or_role('role')
        end

        local function random_object_type()
            return random_of({
                'space', 'sequence', 'function',
                'user', 'role', 'universe',
            })
        end

        local function random_privilege(object_type, for_user)
            local object_type_privelegies = {}
            object_type_privelegies['space'] = {
                'read', 'write', 'create',
                'alter', 'drop',
            }
            object_type_privelegies['sequence'] = {
                'read', 'write', 'create',
                'alter', 'drop',
            }
            object_type_privelegies['function'] = {
                'execute', 'create', 'drop',
            }
            object_type_privelegies['user'] = {
                'alter', 'create', 'drop',
            }
            object_type_privelegies['role'] = {
                'execute', 'create', 'drop',
            }
            object_type_privelegies['universe'] = {
                'read', 'write', 'execute',
                'create', 'alter', 'drop',
            }
            if for_user then
                table.insert(object_type_privelegies['space'], 'usage')
                table.insert(object_type_privelegies['sequence'], 'usage')
                table.insert(object_type_privelegies['function'], 'usage')
                table.insert(object_type_privelegies['role'], 'usage')
            end
            return random_of(object_type_privelegies[object_type])
        end

        local function random_object(privilege, object_type)
            -- Create privileges are system-wide, so can't be given granularily.
            if privilege == 'create' then
                return nil
            end
            -- Can't get a random object of such type (e. g. universe).
            if random[object_type] == nil then
                return nil
            end
            -- Apply the access for all objects with some chance (if random 0).
            if randomnumber(0, 3) == 0 then
                return nil
            end
            local random_result = {random[object_type]()}
            -- No objects of such type.
            if random_result[1] == false then
                return nil
            end
            -- The object name is either the first or the second return value.
            if type(random_result[1]) == 'string' then
                return random_result[1]
            end
            assert(type(random_result[2]) == 'string')
            return random_result[2]
        end

        local function random_user_or_role_privilege(object_type, name)
            local ok, info = pcall(box.schema[object_type].info, name)
            if not ok or #info <= 1 then
                return false, false, ''
            end
            -- Do not drop the 'execute' access on the 'public' role.
            table.remove(info, 1)
            -- XXX: Sort the array?
            local info_entry = random_of(info)
            local privs = {}
            for priv in string.gmatch(info_entry[1], '%w+') do
                table.insert(privs, priv)
            end
            local priv = random_of(privs)
            local object_type = info_entry[2]
            local object = info_entry[3]
            return priv, object_type, object
        end

        -- Selects a random of space indexes. The primary key
        -- is only selected if there's no secondary keys.
        local function random_index(space)
            local secondary_keys = {}
            for i, v in pairs(space.index) do
                if type(i) == 'number' and i > 0 then
                    table.insert(secondary_keys, v)
                end
            end
            -- If there's no secondary keys, return the primary key if exists.
            if #secondary_keys == 0 then
                return space.index[0] ~= nil and space.index[0] or false
            end
            table.sort(secondary_keys, function(a, b) return a.name < b.name end)
            return random_of(secondary_keys)
        end

        local function random_space_index()
            local space, space_name = random_space()
            local index = space and random_index(space)
            local index_name = index and index.name or '<unexisting index>'
            return space, space_name, index, index_name
        end

        local constraints = {}
        local next_constraint_id = 1

        local function random_constraint()
            -- With some probability we create a new constraint instead
            -- of using an existing one (in case the random returns 0).
            local i = randomnumber(0, #constraints)
            if #constraints == 0 or i == 0 then
                local constraint_name = 'constraint_' .. next_constraint_id
                next_constraint_id = next_constraint_id + 1
                local func_create_opts = {
                    language = 'LUA',
                    is_deterministic = true,
                    body = 'function(unused, unused) return true end'
                }
                box.schema.func.create(constraint_name, func_create_opts)
                log('box.schema.func.create("' .. constraint_name ..
                    '", ' .. str(func_create_opts) .. ') -- WHATEVER')
                table.insert(constraints, constraint_name)
                return constraint_name
            end
            return 'constraint_' .. i
        end

        local function random_constraints(constraint_count)
            local constraints = {}
            for i = 1, constraint_count do
                constraints['constraint_' .. i] = random_constraint()
            end
            return constraints
        end

        local function box_do(func, ...)
            local ok, errmsg = pcall(func, ...)
            local msg = ok and 'SUCCESS' or ('FAILURE (' .. errmsg .. ')')
            return ok, msg
        end

        local function schema_object_do(object, func, ...)
            if not object then
                return true, 'SKIPPED'
            end
            return box_do(object[func], object, ...)
        end

        local function ddl_space_create()
            local name = 'space_' .. randomnumber(NUM_NAMES)
            local ok, msg = box_do(box.schema.space.create, name)
            log('box.schema.space.create("' .. name .. '") -- ' .. msg)
            return ok
        end

        local function ddl_space_drop()
            local space, space_name = random_space()
            local ok, msg = schema_object_do(space, 'drop')
            log('box.space.' .. space_name .. ':drop() -- ' .. msg)
            return ok
        end

        local function ddl_space_format()
            local space, space_name = random_space()
            local new_name = random_of({'id', 'identifier', 'data', 'value'})
            local new_type = random_of({'integer', 'unsigned'})
            local new_format = {{new_name, new_type}}
            local ok, msg = schema_object_do(space, 'format', new_format)
            log('box.space.' .. space_name .. ':format('
                .. str(new_format) .. ') -- ' .. msg)
            return ok
        end

        local function ddl_space_rename()
            local space, space_name = random_space()
            local new_name = 'space_' .. randomnumber(NUM_NAMES)
            local ok, msg = schema_object_do(space, 'rename', new_name)
            log('box.space.' .. space_name .. ':rename("'
                .. new_name .. '") -- ' .. msg)
            return ok
        end

        local function ddl_space_alter_tuple_constraints()
            local space, space_name = random_space()
            -- We let it be zero constraints, thus minus one.
            local constraint_count = randomnumber(NUM_TUPLE_CONSTRAINTS) - 1
            local constraints = random_constraints(constraint_count)
            local alter_opts = {
                constraint = #constraints ~= 0 and constraints or nil
            }
            local ok, msg = schema_object_do(space, 'alter', alter_opts)
            log('box.space.' .. space_name .. ':alter('
                .. str(alter_opts) .. ') -- ' .. msg)
            return ok
        end

        local function ddl_space_index_create()
            local space, space_name = random_space()
            local index_name = 'index_' .. randomnumber(NUM_NAMES)
            local _, sequence = random_sequence()
            -- Use a sequence on the primary key with some probability.
            local use_sequence = space and space.index[0] == nil and
                                 randomnumber(0, 3) == 0
            local opts = {
                sequence = use_sequence and sequence or nil
            }
            local ok, msg = schema_object_do(space, 'create_index',
                                             index_name, opts)
            log('box.space.' .. space_name .. ':create_index(' ..
                index_name .. ', ' .. str(opts) .. ') -- ' .. msg)
            return ok
        end

        local function random_func_index_func_or_new()
            local name = 'func_index_func_' .. randomnumber(NUM_NAMES)
            if not box.schema.func.exists(name) then
                local opts = {
                    language = 'LUA',
                    is_sandboxed = true,
                    is_deterministic = true,
                    body = 'function(t) return {t[0]} end'
                }
                local ok, msg = box_do(box.schema.func.create, name, opts)
                log('box.schema.func.create("' .. name ..
                    '", ' .. str(opts) .. ') -- ' .. msg)
                t.assert_equals(ok, true)
            end
            return name
        end

        local function ddl_space_index_create_functional()
            local space, space_name = random_space()
            local index_name = 'index_' .. randomnumber(NUM_NAMES)
            local func = random_func_index_func_or_new()
            local opts = {func = func}
            local ok, msg = schema_object_do(space, 'create_index',
                                             index_name, opts)
            if space and space.index[0] == nil then
                ok = true
                msg = msg .. ' <considered OK, skipping>'
            end
            log('box.space.' .. space_name .. ':create_index("' ..
                index_name .. '", ' .. str(opts) .. ') -- ' .. msg)
            return ok
        end


        local function ddl_space_index_drop()
            local _, space_name, index, index_name = random_space_index()
            local ok, msg = schema_object_do(index, 'drop')
            log('box.space.' .. space_name .. '.index.'
                .. index_name .. ':drop() -- ' .. msg)
            return ok
        end

        local function ddl_space_index_alter_uniqueness()
            local _, space_name, index, index_name = random_space_index()
            local alter_opts = {
                unique = index and not index.unique or false
            }
            local ok, msg = schema_object_do(index, 'alter', alter_opts)
            log('box.space.' .. space_name .. '.index.' .. index_name
                .. ':alter(' .. str(alter_opts) .. ') -- ' .. msg)
            return ok
        end

        local function ddl_space_index_alter_parts()
            local _, space_name, index, index_name = random_space_index()
            local alter_opts = {parts = {
                1, random_of({'integer', 'unsigned'})
            }}
            local ok, msg = schema_object_do(index, 'alter', alter_opts)
            log('box.space.' .. space_name .. '.index.' .. index_name
                .. ':alter(' .. str(alter_opts) .. ') -- ' .. msg)
            return ok
        end

        local function ddl_space_index_alter_sequence()
            local space, space_name = random_space()
            local pk = space.index[0]
            local pk_name = pk and pk.name or '<unexisting primary key>'
            local _, sequence = random_sequence()
            local alter_opts = {sequence = sequence}
            local ok, msg = schema_object_do(pk, 'alter', alter_opts)
            log('box.space.' .. space_name .. '.index.' .. pk_name
                .. ':alter(' .. str(alter_opts) .. ') -- ' .. msg)
            return ok
        end

        local function ddl_space_index_rename()
            local _, space_name, index, index_name = random_space_index()
            local new_name = 'index_' .. randomnumber(NUM_NAMES)
            local ok, msg = schema_object_do(index, 'rename', new_name)
            log('box.space.' .. space_name .. '.index.' .. index_name
                .. ':rename("' .. new_name .. '") -- ' .. msg)
            return ok
        end

        local function ddl_user_or_role_grant(what)
            local name = random_user_or_role(what)
            local object_type = random_object_type()
            local privilege = random_privilege(object_type, (what == 'user'))
            local object = random_object(privilege, object_type)
            local ok, msg = box_do(box.schema[what].grant, name,
                                   privilege, object_type, object)
            log('box.schema.' .. what .. '.grant("' .. name ..
                '", "' .. privilege .. '", "' .. object_type ..
                '", ' .. str(object) .. ') -- ' .. msg)
            return ok
        end

        local function ddl_user_or_role_revoke(what)
            local name = random_user_or_role(what)
            local privilege, object_type, object =
                random_user_or_role_privilege(what, name)
            local ok, msg = true, 'SKIPPED (nothing to revoke)'
            if privilege then
                ok, msg = box_do(box.schema[what].revoke, name,
                                 privilege, object_type, object)
            end
            log('box.schema.' .. what .. '.revoke("' .. name ..
                '", ' .. str(privilege) .. ', ' .. str(object_type) ..
                ', ' .. str(object) .. ') -- ' .. msg)
            return ok
        end

        local function ddl_user_create()
            local name = 'user_' .. randomnumber(NUM_NAMES)
            local ok, msg = box_do(box.schema.user.create, name)
            log('box.schema.user.create("' .. name .. '") -- ' .. msg)
            return ok
        end

        local function ddl_user_drop()
            local name = random_user()
            local ok, msg = box_do(box.schema.user.drop, name)
            log('box.schema.user.drop("' .. name .. '") -- ' .. msg)
            return ok
        end

        local function ddl_user_grant()
            return ddl_user_or_role_grant('user')
        end

        local function ddl_user_revoke()
            return ddl_user_or_role_revoke('user')
        end

        local function ddl_role_create()
            local name = 'role_' .. randomnumber(NUM_NAMES)
            local ok, msg = box_do(box.schema.role.create, name)
            log('box.schema.role.create("' .. name .. '") -- ' .. msg)
            return ok
        end

        local function ddl_role_drop()
            local name = random_role()
            local ok, msg = box_do(box.schema.role.drop, name)
            log('box.schema.role.drop("' .. name .. '") -- ' .. msg)
            return ok
        end

        local function ddl_role_grant()
            return ddl_user_or_role_grant('role')
        end

        local function ddl_role_revoke()
            return ddl_user_or_role_revoke('role')
        end

        local function random_sequence_opts()
            local opts = {}
            opts.step = randomnumber(1, 10) * random_of({1, -1})
            opts.min = randomnumber(-100, 100)
            opts.max = randomnumber(opts.min, 200)
            opts.start = randomnumber(opts.min, opts.max)
            opts.cycle = random_of({true, false})
            return opts
        end

        local function ddl_sequence_create()
            local name = 'sequence_' .. randomnumber(NUM_NAMES)
            local opts = random_sequence_opts()
            local ok, msg = box_do(box.schema.sequence.create, name, opts)
            log('box.schema.sequence.create("' .. name .. '") -- ' .. msg)
            return ok
        end

        local function ddl_sequence_alter()
            local object, name = random_sequence()
            local opts = random_sequence_opts()
            local ok, msg = schema_object_do(object, 'alter', opts)
            log('box.sequence.' .. name .. ':alter('
                .. str(opts) .. ') -- ' .. msg)
            return ok
        end

        local function ddl_sequence_rename()
            local object, name = random_sequence()
            local alter_opts = {
                name = 'sequence_' .. randomnumber(NUM_NAMES)
            }
            local ok, msg = schema_object_do(object, 'alter', alter_opts)
            log('box.sequence.' .. name .. ':alter('
                .. str(alter_opts) .. ') -- ' .. msg)
            return ok
        end

        local function ddl_sequence_drop()
            local object, name = random_sequence()
            local ok, msg = schema_object_do(object, 'drop')
            log('box.sequence.' .. name .. ':drop() -- ' .. msg)
            return ok
        end

        local function ddl_function_create()
            local name = 'function_' .. randomnumber(NUM_NAMES)
            local opts = {
                language = 'LUA',
                is_deterministic = true,
                is_sandboxed = true,
                body = 'function() return true end'
            }
            local ok, msg = box_do(box.schema.func.create, name, opts)
            log('box.schema.func.create("' .. name ..
                '", ' .. str(opts) .. ') -- ' .. msg)
            return ok
        end

        local function ddl_function_drop()
            local name = random_function()
            local ok, msg = box_do(box.schema.func.drop, name)
            log('box.schema.func.drop("' .. name .. '") -- ' .. msg)
            return ok
        end

        ddl_functions['space'] = {
            ddl_space_create,
            ddl_space_drop,
            ddl_space_format,
            ddl_space_rename,
            ddl_space_alter_tuple_constraints,
            ddl_space_index_create,
            ddl_space_index_create_functional,
            ddl_space_index_rename,
            ddl_space_index_alter_parts,
            ddl_space_index_alter_uniqueness,
            ddl_space_index_alter_sequence,
            ddl_space_index_drop,
        }

        ddl_functions['user'] = {
            ddl_user_create,
            ddl_user_drop,
            ddl_user_grant,
            ddl_user_revoke,
        }

        ddl_functions['role'] = {
            ddl_role_create,
            ddl_role_drop,
            ddl_role_grant,
            ddl_role_revoke,
        }

        ddl_functions['sequence'] = {
            ddl_sequence_create,
            ddl_sequence_alter,
            ddl_sequence_rename,
            ddl_sequence_drop,
        }

        ddl_functions['function'] = {
            ddl_function_create,
            ddl_function_drop,
        }

        random['space'] = random_space
        random['user'] = random_user
        random['role'] = random_role
        random['sequence'] = random_sequence
        random['function'] = random_function

        for _, object_type in pairs(object_types) do
            assert(random[object_type] ~= nil)
            assert(#ddl_functions[object_type] ~= 0)
        end

        local function gen_ddl(object_type)
            -- If there's no object of such type - create it.
            if random[object_type]() == false then
                return ddl_functions[object_type][1] -- Create.
            else
                return random_of(ddl_functions[object_type])
            end
        end

        local function gen_ddls()
            local ddls = {}
            for _, object_type in pairs(object_types) do
                table.insert(ddls, gen_ddl(object_type))
            end
            return ddls
        end

        local iterations = 0

        local function tx_performer(id)
            fiber.self().storage['id'] = id
            while true do
                iterations = iterations + 1
                if iterations == 10000 then
                    errinj.set('ERRINJ_WAL_IO', true)
                    finish_on_fail.value = true
                    return
                end

                local ddls = gen_ddls()
                box.begin()
                local success = true
                for _, ddl in pairs(ddls) do
                    success = success and ddl()
                end
                if success then
                    local _, msg = box_do(box.commit)
                    log('box.commit() -- ' .. msg)
                else
                    box.rollback()
                    log('box.rollback()')
                end
                if not success and finish_on_fail.value then
                    return
                else
                    fiber.yield()
                end
            end
        end

        local fibers = {}
        for i = 1, NUM_FIBERS do
            fibers[i] = fiber.new(tx_performer, i)
            fibers[i]:set_joinable(true)
        end
        for i = 1, NUM_FIBERS do
            local success, errmsg = fiber.join(fibers[i])
            -- The errmsg check is first to show up on GitHub CI.
            t.assert_equals(errmsg, nil)
            t.assert_equals(success, true)
        end
    end)
end
