local t = require('luatest')
local fio = require('fio')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

local g_root = t.group('module_root', {
    {layout = 'rocks'},
    {layout = 'direct'},
})

local g_ancestors = t.group('physical_ancestors', {
    {missing = false},
    {missing = true},
})

g_ancestors.test_rocks_in_physical_parent = function(g)
    local dir = treegen.prepare_directory({}, {})
    local suffix = jit.os == 'OSX' and 'dylib' or 'so'
    treegen.write_file(dir, 'target/deep/child/.keep', '')
    t.assert(fio.symlink('target/deep/child', dir .. '/link'))
    t.assert(fio.symlink('target/deep', dir .. '/alias'))
    if not g.params.missing then
        t.assert(fio.mkdir(dir .. '/target/deep/wd'))
    end
    treegen.write_file(dir, 'target/deep/.rocks/share/tarantool/probe.lua',
                       'return "physical parent"')
    treegen.write_file(dir, 'target/.rocks/share/tarantool/probe.lua',
                       'return "distant parent"')
    treegen.write_file(dir, '.rocks/share/tarantool/probe.lua',
                       'return "lexical parent"')
    local native = 'target/deep/.rocks/lib/tarantool/native_probe.' .. suffix
    treegen.write_file(dir, native,
                       'physical native parent')
    treegen.write_file(dir, 'main.lua', [[
        local fio = require('fio')
        package.path = ''
        package.cpath = ''
        local existed = fio.path.is_dir('target/deep/wd')
        local paths = {'link/../wd', 'alias/wd',
                       'link/../missing/inner/../../wd',
                       'link/../missing/nested/wd'}
        for _, path in ipairs(paths) do
            for _, root in ipairs({path, fio.cwd() .. '/' .. path}) do
                package.setsearchroot(root)
                package.loaded.probe = nil
                assert(require('probe') == 'physical parent')
                local filename = assert(package.search('native_probe'))
                local f = assert(io.open(filename))
                assert(f:read('*a') == 'physical native parent')
                f:close()
            end
        end
        assert(fio.path.is_dir('target/deep/wd') == existed)
        assert(not fio.path.lexists('target/deep/missing'))
        print('rocks lookup follows physical parents without creating them')
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {LUA_PATH = '', LUA_CPATH = ''},
        {'main.lua'}, {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout,
        'rocks lookup follows physical parents without creating them')
end

g_root.test_modules_in_missing_parent_root = function(g)
    local dir = treegen.prepare_directory({}, {})
    local suffix = jit.os == 'OSX' and 'dylib' or 'so'
    local lua_dir = g.params.layout == 'direct' and '' or
                    '.rocks/share/tarantool/'
    local native_dir = g.params.layout == 'direct' and '' or
                       '.rocks/lib/tarantool/'
    treegen.write_file(dir, 'wd/' .. lua_dir .. 'probe.lua',
                       'return "workdir"')
    -- Native fixtures test filename selection, not shared-library loading.
    treegen.write_file(dir, 'wd/' .. native_dir .. 'native_probe.' .. suffix,
                       'native workdir')
    treegen.write_file(dir, 'main.lua', [[
        local fio = require('fio')
        local minifio = require('internal.minifio')
        package.path = ''
        package.cpath = ''
        local function check()
            local root = fio.cwd()
            local missing = not fio.path.lexists('missing')
            local normalized = minifio.normalize_missing_parents(
                root .. '/missing/../wd')
            assert(normalized == root ..
                   (missing and '/wd' or '/missing/../wd'))
            local roots = {'missing/../wd', fio.cwd() .. '/missing/../wd'}
            for _, root in ipairs(roots) do
                package.setsearchroot(root)
                package.loaded.probe = nil
                assert(require('probe') == 'workdir')
                local filename = assert(package.search('native_probe'))
                local f = assert(io.open(filename))
                assert(f:read('*a') == 'native workdir')
                f:close()
            end
        end
        check()
        assert(fio.mkdir('missing'))
        check()
        print('module lookup handles missing and existing parents')
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {LUA_PATH = '', LUA_CPATH = ''},
        {'main.lua'}, {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout,
                         'module lookup handles missing and existing parents')
end

g_root.test_modules_in_mixed_missing_and_symlink_parent_root = function(g)
    local dir = treegen.prepare_directory({}, {})
    local suffix = jit.os == 'OSX' and 'dylib' or 'so'
    local lua_dir = g.params.layout == 'direct' and '' or
                    '.rocks/share/tarantool/'
    local native_dir = g.params.layout == 'direct' and '' or
                       '.rocks/lib/tarantool/'
    treegen.write_file(dir, 'target/child/.keep', '')
    t.assert(fio.symlink('target/child', dir .. '/link'))
    treegen.write_file(dir, 'target/wd/' .. lua_dir .. 'probe.lua',
                       'return "filesystem"')
    treegen.write_file(dir, 'wd/' .. lua_dir .. 'probe.lua',
                       'return "lexical"')
    treegen.write_file(dir, 'wd/' .. lua_dir .. 'lexical_only.lua',
                       'return "wrong root"')
    treegen.write_file(dir,
                       'target/wd/' .. native_dir .. 'native_probe.' .. suffix,
                       'filesystem native')
    treegen.write_file(dir, 'wd/' .. native_dir .. 'native_probe.' .. suffix,
                       'lexical native')
    treegen.write_file(dir, '.rocks/share/tarantool/ancestor.lua',
                       'return "ancestor"')
    treegen.write_file(dir, 'main.lua', [[
        local fio = require('fio')
        package.path = ''
        package.cpath = ''
        local path = 'link/../missing/./inner/../../wd'
        for _, root in ipairs({path, fio.cwd() .. '/' .. path}) do
            package.setsearchroot(root)
            package.loaded.probe = nil
            assert(require('probe') == 'filesystem')
            assert(package.search('lexical_only') == nil)
            assert(require('ancestor') == 'ancestor')
            local filename = assert(package.search('native_probe'))
            local f = assert(io.open(filename))
            assert(f:read('*a') == 'filesystem native')
            f:close()
        end
        assert(not fio.path.exists('target/missing'))
        print('mixed missing and symlink parents preserve module choice')
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {LUA_PATH = '', LUA_CPATH = ''},
        {'main.lua'}, {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout,
        'mixed missing and symlink parents preserve module choice')
end

g.test_rocks_parent_errors_are_not_missing_directories = function()
    local dir = treegen.prepare_directory({}, {})
    local suffix = jit.os == 'OSX' and 'dylib' or 'so'
    treegen.write_file(dir, 'blocker', 'not a directory')
    t.assert(fio.symlink('absent', dir .. '/dangling'))
    t.assert(fio.symlink('loop', dir .. '/loop'))
    treegen.write_file(dir, 'wd/.rocks/share/tarantool/probe.lua',
                       'return "wrong root"')
    treegen.write_file(dir, 'wd/.rocks/lib/tarantool/native_probe.' .. suffix,
                        'wrong native root')
    treegen.write_file(dir, '.rocks/share/tarantool/ancestor_probe.lua',
                       'return "wrong ancestor"')
    treegen.write_file(dir, 'main.lua', [[
        local fio = require('fio')
        package.path = ''
        package.cpath = ''
        local paths = {'dangling/../wd', 'blocker/child/../../wd',
                       'loop/child/../../wd'}
        local minifio = require('internal.minifio')
        for _, path in ipairs(paths) do
            local root = fio.cwd() .. '/' .. path
            assert(minifio.normalize_missing_parents(root) == root)
            package.setsearchroot(root)
            local ok, err = pcall(require, 'probe')
            assert(not ok)
            assert(err:find(root .. '/.rocks/share/tarantool/probe.lua',
                            1, true))
            assert(package.search('native_probe') == nil)
            assert(package.search('ancestor_probe') == nil)
        end
        -- New Lua states search modules before their error type is ready.
        package.setsearchroot(fio.cwd() .. '/blocker/child/../../wd')
        box.cfg({app_threads = 1})
        assert(minifio.is_enoent('blocker/child') == false)
        local errno = require('errno')
        assert(fio.mkdir('locked'))
        assert(fio.chmod('locked', tonumber('000', 8)))
        local root = fio.cwd() .. '/locked/child/../wd'
        local st = fio.lstat('locked/child')
        local stat_errno = errno()
        local is_enoent = minifio.is_enoent('locked/child')
        local normalized = minifio.normalize_missing_parents(root)
        package.setsearchroot(root)
        local ancestor = package.search('ancestor_probe')
        assert(fio.chmod('locked', tonumber('700', 8)))
        -- Root can traverse locked directories, so EACCES is not guaranteed.
        assert(st == nil)
        if stat_errno == errno.EACCES then
            assert(is_enoent == false)
            assert(normalized == root)
            assert(ancestor == nil)
        else
            assert(stat_errno == errno.ENOENT)
            assert(is_enoent == true)
            assert(normalized == fio.cwd() .. '/locked/wd')
            assert(ancestor ~= nil)
        end
        print('dangling links and lookup errors retain the original root')
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {LUA_PATH = '', LUA_CPATH = ''},
        {'main.lua'}, {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout,
        'dangling links and lookup errors retain the original root')
end

g_root.test_modules_in_symlink_parent_root = function(g)
    local dir = treegen.prepare_directory({}, {})
    local lua_dir = g.params.layout == 'direct' and '' or
                    '.rocks/share/tarantool/'
    treegen.write_file(dir, 'target/child/.keep', '')
    t.assert(fio.symlink('target/child', dir .. '/link'))
    -- link/../wd resolves to target/wd; lexical normalization picks wd.
    treegen.write_file(dir, 'target/wd/' .. lua_dir .. 'probe.lua',
                       'return "filesystem"')
    treegen.write_file(dir, 'wd/' .. lua_dir .. 'probe.lua',
                       'return "lexical"')
    treegen.write_file(dir, 'wd/' .. lua_dir .. 'lexical_only.lua',
                       'return "wrong root"')
    treegen.write_file(dir, '.rocks/share/tarantool/ancestor.lua',
                       'return "ancestor"')
    treegen.write_file(dir, 'main.lua', [[
        local fio = require('fio')
        local minifio = require('internal.minifio')
        for _, root in ipairs({'link/../wd', fio.cwd() .. '/link/../wd'}) do
            local absolute = root:startswith('/') and root or
                             fio.cwd() .. '/' .. root
            assert(minifio.normalize_missing_parents(absolute) == absolute)
            package.setsearchroot(root)
            -- A cached module could mask a wrong root on the second pass.
            package.loaded.probe = nil
            assert(require('probe') == 'filesystem')
            assert(package.search('lexical_only') == nil)
            assert(require('ancestor') == 'ancestor')
        end
        -- An unset root must follow cwd instead of retaining the pin.
        package.setsearchroot(box.NULL)
        assert(fio.chdir('target/wd'))
        package.loaded.probe = nil
        assert(require('probe') == 'filesystem')
        print('module searchroot preserves filesystem traversal')
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {LUA_PATH = ''}, {'main.lua'},
        {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout,
                          'module searchroot preserves filesystem traversal')
end
