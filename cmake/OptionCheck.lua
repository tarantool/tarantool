#!/usr/bin/env tarantool

-- This script is a helper for cmake/OptionCheck.cmake.
--
-- ./cmake/OptionCheck.lua
--     --name         <the sub-project's name: e.g. curl>
--     --cmake        <cmake binary: an absolute path>
--     --generator    <cmake generator: Unix Makefiles, Ninja, ...>
--     --source-dir   <the sub-project's sources>
--     --options-file <where we pass the options, for the report>
--     --work-dir     <where to place temporary build dirs>
--     --flags        <flags our build configures the sub-project with> (*)
--     --includes     <the CMake modules the sub-project includes>      (*)
--     --packages     <the packages the sub-project finds>              (*)
--     --module-path  <where the sub-project's own Find modules live>   (*)
--     --ignored      <names we pass deliberately; not reported>        (*)
--
-- The options marked with (*) accept a list of arguments (takes every argument
-- as its item, including ones starting with a dash, until the next known
-- option or the end of the argument list):
--
--  | <...> --flags -DFOO=1 -DBAR=2 --includes GNUInstallDirs <...>
--
-- The idea of the check is to compare passed flags with options declared by the
-- sub-project:
--
--  | diff -u <FLAGS> <DECLARED OPTIONS>
--
-- The declared options are collected from a CMake cache file of the
-- sub-project (it is configured in a separate directory for this purpose).
--
-- However, a direct comparison would highlight a lot of options defined by
-- CMake itself or by find_* commands. We determine this baseline and exclude
-- it from the comparison:
--
--  | diff -u <FLAGS - BASELINE> <DECLARED OPTIONS - BASELINE>
--
-- In order to determine the baseline we configure a stub CMake project of the
-- following form:
--
--  | cmake_minimum_required(VERSION <...>)
--  | project(<...>-stub C)
--  | list(APPEND CMAKE_MODULE_PATH <MODULE PATH>)
--  |
--  | include(<INCLUDE>)
--  | <...>
--  | include(<INCLUDE>)
--  |
--  | find_package(<PACKAGE>)
--  | <...>
--  | find_package(<PACKAGE>)
--
-- <MODULE PATH> is from --module-path, <INCLUDE> is from --includes, <PACKAGE>
-- is from --packages.
--
-- All the variables from the CMake cache of this stub project are taken as
-- the baseline.
--
-- And the known CMake built-in variables (plus ones that find_*() calls
-- define) are ignored too.
--
-- And, finally, options a user asked to ignore are ignored too.
--
-- So, the final formula is the following:
--
--  | diff -u <FLAGS - BASELINE - KNOWN_CMAKE - IGNORED>
--  |         <DECLARED OPTIONS - BASELINE - KNOWN_CMAKE - IGNORED>

local fio = require('fio')
local fun = require('fun')
local popen = require('popen')

-- {{{ General purpose helpers

local function die(fmt, ...)
    io.stderr:write(string.format(fmt, ...) .. '\n')
    os.exit(1)
end

local function as_set(list)
    return fun.iter(list):map(function(item) return item, true end):tomap()
end

local function set_compare(a, b)
    local removed = {}
    local added = {}

    for key, _ in pairs(a) do
        if not b[key] then
            removed[key] = true
        end
    end

    for key, _ in pairs(b) do
        if not a[key] then
            added[key] = true
        end
    end

    return removed, added
end

local function set_filter(set, filter)
    return fun.iter(set):filter(filter):tomap()
end

local function find_duplicates(list)
    local occurrences = {}
    for _, v in ipairs(list) do
        occurrences[v] = (occurrences[v] or 0) + 1
    end

    local res = {}
    for v, count in pairs(occurrences) do
        if count > 1 then
            res[v] = count
        end
    end
    return res
end

local function read_file(...)
    local path = fio.pathjoin(...)
    local fh, err = fio.open(path, {'O_RDONLY'})
    assert(fh ~= nil, {'unable to read file %s: %s', path, err})
    local data = fh:read()
    fh:close()
    return data, path
end

local function write_file(data, ...)
    local path = fio.pathjoin(...)
    local fh, err = fio.open(path, {'O_WRONLY', 'O_CREAT', 'O_TRUNC'})
    assert(fh ~= nil, {'unable to write file %s: %s', path, err})
    fh:write(data)
    fh:close()
end

local function popen_read(argv)
    local ph = popen.new(argv, {stdout = popen.opts.PIPE})
    local chunks = {}
    while true do
        local chunk, err = ph:read()
        assert(chunk ~= nil, {'popen error: %s', err})
        if chunk == '' then -- EOF
            table.insert(chunks, '<end of output>')
            break
        end
        table.insert(chunks, chunk)
    end
    local status = ph:wait()
    ph:close()
    return status, table.concat(chunks)
end

-- }}} General purpose helpers

-- {{{ Argument parsing

-- This is a somewhat unusual parser: list options consume everything after
-- them until a next option. We need it to make wiring with CMake easier,
-- where a transformation of the (a; b; c) list to the
-- `--foo a --foo b --foo c` command line is cumbersome.
local function parse_args(argv, argv_def)
    local function key_of(option)
        return (option:gsub('-', '_'))
    end

    -- Collect values per option.
    local grouped = {}
    local last_option
    for _, argument in ipairs(argv) do
        local option = argument:match('^%-%-(.+)$')
        if option ~= nil then
            assert(argv_def[option] ~= nil, {'unknown option --%s', option})
            assert(grouped[option] == nil, {'duplicate option --%s', option})
            grouped[option] = {}
            last_option = option
        else
            assert(last_option ~= nil, {'unexpected argument %s', argument})
            table.insert(grouped[last_option], argument)
        end
    end

    -- Validate against option types, transform keys to snake_case, unwrap
    -- scalar values.
    return fun.iter(argv_def):map(function(option, kind)
        local v = grouped[option]
        assert(v ~= nil, {'--%s is missing', option})

        if kind == 'scalar' then
            assert(#v ~= 0, {'--%s needs a value', option})
            assert(#v == 1, {'--%s expects one value', option})
            return key_of(option), v[1]
        else -- list
            return key_of(option), v
        end
    end):tomap()
end

-- }}} Argument parsing

-- {{{ Flag / option helpers

local function option_of(flag)
    return flag:match('^%-D([^:=]+)') or ''
end

local function flags2options(flags)
    return fun.iter(flags):map(function(flag)
        -- Strip -D from -D<...>, transform to set.
        return option_of(flag), true
    end):tomap()
end

-- }}} Flag / option helpers

-- {{{ Classify CMake options

local function cmake_builtin_variables(cmake)
    local argv = {cmake, '--help-variable-list'}
    local status, stdout = popen_read(argv)
    assert(status.exit_code == 0, {
        'failed to run %s [exit code %d]:\n%s',
        table.concat(argv, ' '), status.exit_code, stdout,
    })

    -- NB: It is safe to add items to the end during the ipairs traversal.
    local queue = stdout:rstrip():split('\n')
    local res = {}
    for _, option in ipairs(queue) do
        if option:find('<CONFIG>') then
            table.insert(queue, (option:gsub('<CONFIG>', 'DEBUG')))
            table.insert(queue, (option:gsub('<CONFIG>', 'RELEASE')))
            table.insert(queue, (option:gsub('<CONFIG>', 'RELWITHDEBINFO')))
            table.insert(queue, (option:gsub('<CONFIG>', 'MINSIZEREL')))
        elseif option:find('<') == nil and option:find('>') == nil then
            res[option] = true
        end
    end
    return res
end

local function cmake_discovery_options(packages)
    local res = {
        CMAKE_MODULE_PATH = true,
        CMAKE_PREFIX_PATH = true,
        CMAKE_FIND_ROOT_PATH = true,
        CMAKE_SYSROOT = true,
    }
    for _, package in ipairs(packages) do
        res[('%s_ROOT'):format(package:upper())] = true
        res[('%s_ROOT_DIR'):format(package:upper())] = true
    end
    return res
end

-- }}} Classify CMake options

-- {{{ Generate the stub project

local function cmake_minimum(source_dir)
    local re = 'cmake_minimum_required%s*%(%s*VERSION%s+([^%s%)]+)'
    local version, path = read_file(source_dir, 'CMakeLists.txt'):match(re)
    assert(version ~= nil, {'%s: found no cmake_minimum_required()', path})
    return version
end

local function write_stub(config, dir, version)
    local function add(buf, fmt, ...)
        table.insert(buf, fmt:format(...))
    end

    local buf = {}
    add(buf, 'cmake_minimum_required(VERSION %s)', version)
    add(buf, 'project(%s-stub C)', config.name)
    for _, path in ipairs(config.module_path) do
        add(buf, 'list(APPEND CMAKE_MODULE_PATH %s)', path)
    end
    for _, module in ipairs(config.includes) do
        add(buf, 'include(%s)', module)
    end
    for _, package in ipairs(config.packages) do
        add(buf, 'find_package(%s)', package)
    end

    fio.mktree(dir)
    write_file(table.concat(buf, '\n') .. '\n', dir, 'CMakeLists.txt')
end

-- }}} Generate the stub project

-- {{{ Configure a project

local function configure(config, what, source, binary, flags)
    local status, stdout = popen_read(fun.chain({
        config.cmake,
        '-S', source,
        '-B', binary,
        '-G', config.generator,
        '--no-warn-unused-cli',
    }, flags):totable())
    assert(status.exit_code == 0, {
        'failed to configure %s [exit code %d]:\n%s',
        what, status.exit_code, stdout,
    })
end

-- }}} Configure a project

-- {{{ Parse CMake cache file

local function parse_cmake_cache(path)
    -- Follow `cmake -LA`: show the entries of all types except INTERNAL,
    -- STATIC and UNINITIALIZED.
    --
    -- Note: A -D<...> value that the project doesn't declare stays
    -- UNINITIALIZED.
    local CACHE_VAR_TYPES = {
        BOOL = true,
        PATH = true,
        FILEPATH = true,
        STRING = true,
    }

    local entries = {}
    for _, line in ipairs(read_file(path):rstrip():split('\n')) do
        local name, kind = line:match('^([^#/][^=:]*):(%u+)=')
        if kind ~= nil and CACHE_VAR_TYPES[kind] then
            entries[name] = true
        end
    end
    return entries
end

-- }}} Parse CMake cache file

-- {{{ Report

local function report(config, ours, declared, duplicates)
    local removed, added = set_compare(ours, declared)
    local diff = fun.chain(
        fun.iter(removed):map(function(x) return '-' .. x end):totable(),
        fun.iter(added):map(function(x) return '+' .. x end):totable()
    ):totable()
    -- Group similar names.
    table.sort(diff, function(a, b) return a:sub(2) < b:sub(2) end)

    local duplicates_list = fun.iter(duplicates):map(function(option, count)
        return ('%s (%d times)'):format(option, count)
    end):totable()
    table.sort(duplicates_list)

    if #diff == 0 and #duplicates_list == 0 then
        return
    end

    local buf = {}

    local function add_noindent(fmt, ...)
        table.insert(buf, fmt:format(...))
    end

    local function add(fmt, ...)
        -- Indented lines are not reformatted by CMake.
        if fmt ~= '' then
            fmt = '  ' .. fmt
        end
        add_noindent(fmt, ...)
    end

    add_noindent('%s options are out of sync', config.name)
    add('')

    if #diff > 0 then
        add('--- %s', config.options_file)
        add('+++ the options %s declares', config.name)
        for _, line in ipairs(diff) do
            add(line)
        end
        add('')
        add('How to act:')
        add('')
        add('  +NAME, this is %s\'s own option -> pass it in %s', config.name,
            fio.basename(config.options_file))
        add('  +NAME, a module declares it -> add the module to INCLUDES ' ..
            'or PACKAGES')
        add('  -NAME, we pass it on purpose -> add to IGNORED')
        add('  -NAME, we don\'t need it -> drop the flag')
        add('')
    end

    if #duplicates_list > 0 then
        add('passed more than once:')
        for _, line in ipairs(duplicates_list) do
            add('  ' .. line)
        end
        add('')
    end

    die('%s', table.concat(buf, '\n'))
end

-- }}} Report

local argv_def = {
    ['name'] = 'scalar',
    ['cmake'] = 'scalar',
    ['generator'] = 'scalar',
    ['source-dir'] = 'scalar',
    ['options-file'] = 'scalar',
    ['work-dir'] = 'scalar',
    ['flags'] = 'list',
    ['includes'] = 'list',
    ['packages'] = 'list',
    ['module-path'] = 'list',
    ['ignored'] = 'list',
}

-- NB: xpcall is to prettify the error message.
xpcall(function()
    -- Parse CLI arguments.
    local config = parse_args(arg, argv_def)
    -- popen() execs the given path: it doesn't search PATH.
    assert(config.cmake:startswith('/'),
           {'--cmake needs an absolute path, got %s', config.cmake})
    local flags = config.flags
    local ignored = as_set(config.ignored)

    -- Directories.
    local work = config.work_dir
    local stub_source = fio.pathjoin(work, 'stub')
    local stub_build = fio.pathjoin(work, 'stub-build')
    local project_build = fio.pathjoin(work, 'project-build')
    local stub_cache_file = fio.pathjoin(stub_build, 'CMakeCache.txt')
    local cache_file = fio.pathjoin(project_build, 'CMakeCache.txt')

    -- Cleanup: the directories we create, not the work directory itself.
    -- This way a mistake in --work-dir would not be so fatal.
    fio.rmtree(stub_source)
    fio.rmtree(stub_build)
    fio.rmtree(project_build)

    -- Write stub.
    local version = cmake_minimum(config.source_dir)
    write_stub(config, stub_source, version)

    -- Configure the stub.
    --
    -- Pass discovery options to it (library paths and so on).
    local discovery_options = cmake_discovery_options(config.packages)
    local stub_flags = fun.iter(flags):filter(function(flag)
        return discovery_options[option_of(flag)] ~= nil
    end):totable()
    configure(config, 'the stub project', stub_source, stub_build, stub_flags)

    -- Configure the real sub-project.
    configure(config, config.name, config.source_dir, project_build, flags)

    -- Parse cache entries.
    local stub_cache = parse_cmake_cache(stub_cache_file)
    local cache = parse_cmake_cache(cache_file)

    -- We ignore all CMake's builtin variables except the ones the
    -- sub-project declares itself.
    local builtin_variables = cmake_builtin_variables(config.cmake)
    for option, _ in pairs(cache) do
        builtin_variables[option] = nil
    end

    -- Three kinds of name stay out of the comparison: what the caller
    -- asked to ignore, what we derive from CMake and from PACKAGES, and
    -- what the stub project measured.
    local function effectively_ignored(option)
        return
            -- Deliberately ignored by a user's call.
            ignored[option] or
            -- CMake's builtin variable (not necessarily cached).
            builtin_variables[option] or
            -- A path option that is known as not cached when passed with
            -- -D<...>. Unlike the builtin CMake variables, the discovery
            -- options declared by the sub-project are still ignored.
            discovery_options[option] or
            -- CMake or a Find module defines it.
            stub_cache[option]
    end

    local function compared(option)
        return not effectively_ignored(option)
    end

    -- What we pass to the sub-project and what it declares.
    local ours = set_filter(flags2options(flags), compared)
    local declared = set_filter(cache, compared)

    -- Options we pass more than once.
    local duplicates = find_duplicates(fun.iter(flags):map(option_of):totable())

    -- Compare the options and report.
    report(config, ours, declared, duplicates)
end, function(e)
    if type(e) == 'table' then
        die(unpack(e))
    else
        io.stderr:write(debug.traceback(tostring(e)) .. '\n')
        die('internal error')
    end
end)
