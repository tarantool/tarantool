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
        local str = require('luatest.pp').tostring

        -- Start mark for the reproduce generator.
        --[[COPY_TO_REPRODUCE]]--

        box.test_state = {}

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
        print('Random seed: ' .. seed)

        local local_rng = getrandom(seed)

        local NUM_FIBERS = 9
        local NUM_NAMES = 9
        local NUM_TUPLE_CONSTRAINTS = 3
        local NULL_OBJECT = {name = '<invalid>', index = {}} -- TODO
        local finish_on_fail = {value = false}
        local fiber = require('fiber')
        local errinj = box.error.injection
        local object_types = {'space'} --, 'user', 'role', 'sequence', 'function'}
        local ddl_functions = {}
        local random = {}

        local function log(msg)
            io.write('--[[' .. fiber.self().storage['id'] .. ']] ' .. msg .. '\n')
            io.flush()
        end

        -- Selects a random item in the range array[1..N], where N is integer.
        -- Does not select the 0-item or any of non-integer key values. Uses
        -- the given Random Number Generator.
        local function random_of_array_rng(array, rng)
            local random_i = rng(#array)
            return array[random_i]
        end

        -- Selects a random item in the table with the filter applied. The
        -- sorting function (sort_f) is used to make the random selection
        -- deterministic across lua runs. The obj_f function is used to
        -- transform the filtered table items before selection.
        local function random_of_table_rng(t, filter, sort_f, obj_f, rng)
            local objects = {}
            for k, v in pairs(t) do
                if filter == nil or filter(k, v) then
                    local object = obj_f ~= nil and obj_f(v) or v
                    table.insert(objects, object)
                end
            end
            if #objects == 0 then
                return NULL_OBJECT
            end
            table.sort(objects, sort_f)
            return random_of_array_rng(objects, rng)
        end

        -- See random_of_table_rng, local (for test generation).
        local function random_of_table(table, filter, sort_f, obj_f)
            return random_of_table_rng(table, filter, sort_f, obj_f, local_rng)
        end

        t.assert_not_equals(box.test_state, nil)

        -- Random functions to be used by the test instructions.
        box.test_state.rand = {}

        -- Generates a random integer from 1 to a, or from a to b inlcusively.
        box.test_state.rand.int = getrandom(seed)

        -- Generates a random name with the given prefix and name_count.
        -- If the name_count is nil, then the NUM_NAMES is used.
        box.test_state.rand.name = function(prefix, name_count)
            name_count = name_count or NUM_NAMES
            return prefix .. box.test_state.rand.int(name_count)
        end

        -- See random_of_array_rng.
        box.test_state.rand.of_array = function(array)
            return random_of_array_rng(array, box.test_state.rand.int)
        end

        -- See random_of_table_rng.
        box.test_state.rand.of_table = function(table, filter, sort_f, obj_f)
            return random_of_table_rng(table, filter, sort_f, obj_f,
                                       box.test_state.rand.int)
        end

        -- Selects a random non-system space.
        box.test_state.rand.space = function()
            return box.test_state.rand.of_table(
                box.space,
                function(k, v) return type(k) == 'number' and k >= 512 end,
                function(a, b) return a.name < b.name end
            )
        end

        -- Selects a random sequence.
        box.test_state.rand.sequence = function()
            return box.test_state.rand.of_table(
                box.sequence,
                function(k, v) return type(k) == 'number' end,
                function(a, b) return a.name < b.name end
            )
        end

        -- Selects a random manually created function.
        box.test_state.rand['function'] = function()
            return box.test_state.rand.of_table(
                box.func,
                function(k, v)
                    return type(k) == 'number' and
                           string.match(v.name, 'function_') ~= nil
                end,
                function(a, b) return a.name < b.name end
            )
        end

        local function random_user_or_role(object_type)
            -- Only select non-system entries (id >= 32).
            local objects = box.space._user.index.primary:select(32, {
                iterator = 'ge'
            })
            local name = box.test_state.rand.of_table(
                objects,
                function(k, v) return v[4] == object_type end,
                nil, -- The string array is sorted automatically.
                function(x) return x[3] end
            )
            return {name = name}
        end

        -- Selects a random non-system user.
        box.test_state.rand.user = function()
            return random_user_or_role('user')
        end

        -- Selects a random non-system role.
        box.test_state.rand.role = function()
            return random_user_or_role('role')
        end

        -- Selects a random object type.
        box.test_state.rand.object_type = function()
            return box.test_state.rand.of_array({
                'space', 'sequence', 'function',
                'user', 'role', 'universe',
            })
        end

        -- Selects a random privilege for the given object type. The for_user
        -- options specifies if the privilege is to be given to a user, false
        -- means, the privilege is meant to be given to a role.
        box.test_state.rand.privilege = function(object_type, for_user)
            local object_type_privs = {}
            object_type_privs['space'] = {
                'read', 'write', 'create',
                'alter', 'drop',
            }
            object_type_privs['sequence'] = {
                'read', 'write', 'create',
                'alter', 'drop',
            }
            object_type_privs['function'] = {
                'execute', 'create', 'drop',
            }
            object_type_privs['user'] = {
                'alter', 'create', 'drop',
            }
            object_type_privs['role'] = {
                'execute', 'create', 'drop',
            }
            object_type_privs['universe'] = {
                'read', 'write', 'execute',
                'create', 'alter', 'drop',
            }
            if for_user then
                table.insert(object_type_privs['space'], 'usage')
                table.insert(object_type_privs['sequence'], 'usage')
                table.insert(object_type_privs['function'], 'usage')
                table.insert(object_type_privs['role'], 'usage')
            end
            return box.test_state.rand.of_array(object_type_privs[object_type])
        end

        -- Selects a random schema object given its type. The function is
        -- designed to select a specific object to grant privileges for, so
        -- it takes the privilege in order to determine if the privilege can
        -- be given granularily.
        box.test_state.rand.object = function(privilege, object_type)
            -- Create privileges are system-wide, so can't be given granularily.
            if privilege == 'create' then
                return nil
            end
            -- Can't get a random object of such type (e. g. universe).
            if box.test_state.rand[object_type] == nil then
                return nil
            end
            -- Apply the access for all objects with some chance (if random 0).
            if box.test_state.rand.int(0, 3) == 0 then
                return nil
            end
            local random_result = {box.test_state.rand[object_type]()}
            -- No objects of such type.
            if random_result == NULL_OBJECT then
                return nil
            end
            return random_result.name
        end

        -- Selects a random privilege given to the given user or role.
        -- The object_type is "user" or "role".
        box.test_state.rand.user_or_role_privilege = function(object_type, name)
            local ok, info = pcall(box.schema[object_type].info, name)
            if not ok or #info <= 1 then
                return false, false, ''
            end
            -- Do not drop the 'execute' access on the 'public' role.
            table.remove(info, 1)
            -- XXX: Sort the array?
            local info_entry = box.test_state.rand.of_array(info)
            local privs = {}
            for priv in string.gmatch(info_entry[1], '%w+') do
                table.insert(privs, priv)
            end
            local priv = random_of(privs)
            local object_type = info_entry[2]
            local object = info_entry[3]
            return priv, object_type, object
        end

        -- Selects a random of space secondary indexes.
        local function space_random_sk(space)
            return box.test_state.rand.of_table(
                space.index,
                function(k, v) return type(i) == 'number' and i > 0 end,
                function(a, b) return a.name < b.name end
            )
        end

        -- Selects a random secondary index of a random space or NULL_OBJECT.
        box.test_state.rand.sk = function()
            local space = random_space()
            local index = space ~= NULL_OBJECT and space_random_sk(space)
            return index or NULL_OBJECT
        end

        local function random_constraint_or_new()
            local name = box.test_state.rand.name('constraint_', NUM_NAMES * 3)
            if not box.func.exists(name) then
                local opts = {
                    language = 'LUA',
                    is_deterministic = true,
                    body = 'function(unused, unused) return true end'
                }
                local ok, msg = box_do(box.schema.func.create, name, opts)
                log('box.schema.func.create("' .. name ..
                    '", ' .. str(opts) .. ') -- ' .. msg)
                t.assert_equals(ok, true)
            end
            return box.func[name]
        end

        box.test_state.rand.constraints = function(constraint_count)
            local constraints = {}
            for i = 1, constraint_count do
                constraints['constraint_' .. i] = random_constraint_or_new()
            end
            return constraints
        end

        local function box_do(func, ...)
            if not func then
                return true, 'SKIPPED'
            end
            local ok, errmsg = pcall(func, ...)
            local msg = ok and 'SUCCESS' or ('FAILURE (' .. errmsg .. ')')
            return ok, msg
        end

        local function schema_object_do(object, func, ...)
            return box_do(object[func], object, ...)
        end

        -- The ddl instructions to be executed by fibers.
        box.test_state.ddl = {}

        box.test_state.ddl.space = {}

        box.test_state.ddl.space.create = function()
            local name = box.test_state.rand.name('space_')
            local ok, msg = box_do(box.schema.space.create, name)
            log('box.schema.space.create("' .. name .. '") -- ' .. msg)
            return ok
        end

        box.test_state.ddl.space.drop = function()
            local space = box.test_state.rand.space()
            local ok, msg = schema_object_do(space, 'drop')
            log('box.space.' .. space.name .. ':drop() -- ' .. msg)
            return ok
        end
--[[
        box.test_state.ddl.space.format = function()
            local space = random_space()
            local new_name = random_of({'id', 'identifier', 'data', 'value'})
            local new_type = random_of({'integer', 'unsigned'})
            local new_format = {{new_name, new_type}}
            local ok, msg = schema_object_do(space, 'format', new_format)
            log('box.space.' .. space.name .. ':format('
                .. str(new_format) .. ') -- ' .. msg)
            return ok
        end

        box.test_state.ddl.space.rename = function()
            local space = random_space()
            local old_name = space.name
            local new_name = box.test_state.rand.name('space_')
            local ok, msg = schema_object_do(space, 'rename', new_name)
            log('box.space.' .. old_name .. ':rename("'
                .. new_name .. '") -- ' .. msg)
            return ok
        end

        box.test_state.ddl.space.alter_tuple_constraints = function()
            local space = random_space()
            -- We let it be zero constraints, thus minus one.
            local count = box.test_state.rand.int(NUM_TUPLE_CONSTRAINTS) - 1
            local constraints = random_constraints(count)
            local alter_opts = {
                constraint = #constraints ~= 0 and constraints or nil
            }
            local ok, msg = schema_object_do(space, 'alter', alter_opts)
            log('box.space.' .. space_name .. ':alter('
                .. str(alter_opts) .. ') -- ' .. msg)
            return ok
        end

        box.test_state.ddl.space.index_create = function()
            local space = random_space()
            local index_name = box.test_state.rand.name('index_')
            local sequence = random_sequence()
            -- Use a sequence on the primary key with some probability.
            local use_sequence = space and space.index[0] == nil and
                                 box.test_state.rand.int(0, 3) == 0
            local opts = {
                sequence = use_sequence and sequence.name or nil
            }
            local ok, msg = schema_object_do(space, 'create_index',
                                             index_name, opts)
            log('box.space.' .. space.name .. ':create_index(' ..
                index_name .. ', ' .. str(opts) .. ') -- ' .. msg)
            return ok
        end

        local function random_func_index_func_or_new()
            local name = box.test_state.rand.name('func_index_func_')
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

        box.test_state.ddl.space.index_create_functional = function()
            local space = random_space()
            local index_name = 'index_' .. local_rng(NUM_NAMES)
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


        box.test_state.ddl.space.index_drop = function()
            local _, space_name, index, index_name = random_index()
            local ok, msg = schema_object_do(index, 'drop')
            log('box.space.' .. space_name .. '.index.'
                .. index_name .. ':drop() -- ' .. msg)
            return ok
        end

        box.test_state.ddl.space.index_alter_uniqueness = function()
            local _, space_name, index, index_name = random_index()
            local alter_opts = {
                unique = index and not index.unique or false
            }
            local ok, msg = schema_object_do(index, 'alter', alter_opts)
            log('box.space.' .. space_name .. '.index.' .. index_name
                .. ':alter(' .. str(alter_opts) .. ') -- ' .. msg)
            return ok
        end

        box.test_state.ddl.space.index_alter_parts = function()
            local _, space_name, index, index_name = random_index()
            local alter_opts = {parts = {
                1, random_of({'integer', 'unsigned'})
            }}
            local ok, msg = schema_object_do(index, 'alter', alter_opts)
            log('box.space.' .. space_name .. '.index.' .. index_name
                .. ':alter(' .. str(alter_opts) .. ') -- ' .. msg)
            return ok
        end

        box.test_state.ddl.space.index_alter_sequence = function()
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

        box.test_state.ddl.space.index_rename = function()
            local _, space_name, index, index_name = random_index()
            local new_name = 'index_' .. local_rng(NUM_NAMES)
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
            local name = 'user_' .. local_rng(NUM_NAMES)
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
            local name = 'role_' .. local_rng(NUM_NAMES)
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
            opts.step = local_rng(1, 10) * random_of({1, -1})
            opts.min = local_rng(-100, 100)
            opts.max = local_rng(opts.min, 200)
            opts.start = local_rng(opts.min, opts.max)
            opts.cycle = random_of({true, false})
            return opts
        end

        local function ddl_sequence_create()
            local name = 'sequence_' .. local_rng(NUM_NAMES)
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
                name = 'sequence_' .. local_rng(NUM_NAMES)
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
            local name = 'function_' .. local_rng(NUM_NAMES)
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
--]]

        box.test_state.commit = function()
            local ok, msg = box_do(box.commit)
            log('box.commit() -- ' .. msg)
        end

        -- End mark for the reproduce generator.
        --[[/COPY_TO_REPRODUCE]]--

        local function random_ddl(object_type)
            return random_of_table(
                box.test_state.ddl[object_type],
                nil, -- No filter.
                function(a, b)
                    local a_info = debug.getinfo(a)
                    local b_info = debug.getinfo(b)
                    local a = a_info.source .. a_info.linedefined
                    local b = b_info.source .. b_info.linedefined
                    return a < b
                end
            )
        end

        local function gen_ddls()
            local ddls = {}
            for _, object_type in ipairs(object_types) do
                table.insert(ddls, random_ddl(object_type))
            end
            return ddls
        end

        local function gen_fiber_transactions(count)
            local transactions = {}
            for i = 1, count do
                table.insert(transactions, gen_ddls())
            end
            return transactions
        end

        local function tx_performer(id, transactions)
            fiber.self().storage['id'] = id
            for k, ddls in ipairs(transactions) do
                box.begin()
                local success = true
                for _, ddl in ipairs(ddls) do
                    success = success and ddl()
                end
                if success then
                    local _, msg = box_do(box.commit)
                    log('box.commit() -- ' .. msg)
                else
                    box.rollback()
                    log('box.rollback()')
                end
            end
            errinj.set('ERRINJ_WAL_IO', true)
            log('box.error.injection.set(\'ERRINJ_WAL_IO\', true)')
        end

        local function function_name(func)
            local info = debug.getinfo(func)
            local source = info.source
            if source:sub(1, 1) == '@' then
                -- This is a file path starting with '@'.
                source = source:sub(2) -- Get rid of the '@'.
                -- Read the function source from the file.
                local file = io.open(source)
                local lines = {}
                for line in file:lines() do
                    table.insert(lines, line)
                end
                source = ''
                for i = info.linedefined, info.lastlinedefined do
                    source = source .. lines[i]
                end
            end
            local name, _ = source:match("([%w\\._]+)(.+)")
            return name
        end

        local function print_reproduce(fiber_transactions)
            local out = io.open('gh_4349_transactional_ddl_fuzzing_test.reproduce.' .. seed .. '.lua', 'w')

            out:write('----------------------------------------------------------', '\n')
            out:write('------------------------ REPRODUCE -----------------------', '\n')
            out:write('----------------------------------------------------------', '\n')

            -- Print the luatest hook.
            out:write('box.cfg{}\n')
            out:write('local t = setmetatable({}, {__index = function() return function() end end})\n')

            -- Print the functions reqired for the reproduce.
            local source = debug.getinfo(1,'S').source:sub(2)
            local file = io.open(source)
            local source = file:read('*a')
            local copy_start = source:find('--[[COPY_TO_REPRODUCE]]--', 1, true)
            local copy_end = source:find('--[[/COPY_TO_REPRODUCE]]--', 1, true)
            local copy = source:sub(copy_start, copy_end - 1)
            local copy = copy:gsub('local seed = ', 'local seed = ' .. seed .. ' -- ')
            out:write(copy, '\n')
            -- Print the fiber functions.
            for i = 1, #fiber_transactions do
                out:write('\nfunction fiber_' .. i .. '_f(id)\n')
                out:write('    fiber.self().storage[\'id\'] = id\n')
                out:write('    local success')
                for _, ddls in ipairs(fiber_transactions[i]) do
                    out:write('    box.begin()', '\n')
                    out:write('    success = true', '\n')
                    for _, v in ipairs(ddls) do
                        local ddl = function_name(v)
                        out:write('    success = success and ' .. ddl .. '()', '\n')
                        out:write('    if success then box.test_state.commit() else box.rollback() end', '\n')
                    end
                end
                out:write('    box.error.injection.set(\'ERRINJ_WAL_IO\', true)', '\n')
                out:write('end', '\n')
            end
            -- Print the fibers execution.
            for i = 1, NUM_FIBERS do
                out:write('fiber_' .. i .. ' = fiber.new(fiber_' .. i .. '_f, ' .. i .. ')', '\n')
                out:write('fiber_' .. i .. ':set_joinable(true)', '\n')
            end
            for i = 1, NUM_FIBERS do
                out:write('local success, errmsg = fiber.join(fiber_' .. i .. ')', '\n')
                out:write('if not success then print(errmsg) end\n')
            end

            out:write('----------------------------------------------------------', '\n')
            out:write('----------------------------------------------------------', '\n')
            out:write('----------------------------------------------------------', '\n')
            out.close()
        end

        local fiber_transactions = {}
        for i = 1, NUM_FIBERS do
            fiber_transactions[i] = gen_fiber_transactions(300 + i * 5)
        end

        print_reproduce(fiber_transactions)

        local fibers = {}
        for i = 1, NUM_FIBERS do
            fibers[i] = fiber.new(tx_performer, i, fiber_transactions[i])
            fibers[i]:set_joinable(true)
        end

        for i = 1, NUM_FIBERS do
            local success, errmsg = fiber.join(fibers[i])
            -- The errmsg check is first to show up on GitHub CI.
            t.assert_equals(errmsg, nil)
            t.assert_equals(success, true)
        end

        t.assert(false)
    end)
end
