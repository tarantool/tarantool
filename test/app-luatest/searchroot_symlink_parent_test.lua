local t = require('luatest')
local fio = require('fio')
local treegen = require('luatest.treegen')
local justrun = require('luatest.justrun')

local g = t.group()

g.test_rocks_in_missing_parent_root = function()
    local dir = treegen.prepare_directory({}, {})
    local suffix = jit.os == 'OSX' and 'dylib' or 'so'
    treegen.write_file(dir, 'wd/.rocks/share/tarantool/probe.lua',
                       'return "workdir"')
    -- Native fixtures test filename selection, not shared-library loading.
    treegen.write_file(dir, 'wd/.rocks/lib/tarantool/native_probe.' .. suffix,
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
        print('rocks lookup handles missing and existing parents')
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {LUA_PATH = '', LUA_CPATH = ''},
        {'main.lua'}, {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout,
                         'rocks lookup handles missing and existing parents')
end

g.test_rocks_in_mixed_missing_and_symlink_parent_root = function()
    local dir = treegen.prepare_directory({}, {})
    local suffix = jit.os == 'OSX' and 'dylib' or 'so'
    treegen.write_file(dir, 'target/child/.keep', '')
    t.assert(fio.symlink('target/child', dir .. '/link'))
    treegen.write_file(dir, 'target/wd/.rocks/share/tarantool/probe.lua',
                       'return "filesystem"')
    treegen.write_file(dir, 'wd/.rocks/share/tarantool/probe.lua',
                       'return "lexical"')
    treegen.write_file(dir, 'wd/.rocks/share/tarantool/lexical_only.lua',
                       'return "wrong root"')
    treegen.write_file(dir,
                       'target/wd/.rocks/lib/tarantool/native_probe.' .. suffix,
                       'filesystem native')
    treegen.write_file(dir, 'wd/.rocks/lib/tarantool/native_probe.' .. suffix,
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
        end
        -- New Lua states search modules before their error type is ready.
        package.setsearchroot(fio.cwd() .. '/blocker/child/../../wd')
        box.cfg({app_threads = 1})
        local exists, err = minifio.lexists('blocker/child')
        assert(exists == nil and err == require('errno').ENOTDIR)
        assert(fio.mkdir('locked'))
        assert(fio.chmod('locked', tonumber('000', 8)))
        local root = fio.cwd() .. '/locked/child/../wd'
        local exists, err = minifio.lexists(fio.cwd() .. '/locked/child')
        local normalized = minifio.normalize_missing_parents(root)
        assert(fio.chmod('locked', tonumber('700', 8)))
        -- Root can traverse locked directories, so EACCES is not guaranteed.
        if exists == nil then
            assert(err == require('errno').EACCES)
            assert(normalized == root)
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

g.test_rocks_in_symlink_parent_root = function()
    local dir = treegen.prepare_directory({}, {})
    treegen.write_file(dir, 'target/child/.keep', '')
    t.assert(fio.symlink('target/child', dir .. '/link'))
    -- link/../wd resolves to target/wd; lexical normalization picks wd.
    treegen.write_file(dir, 'target/wd/.rocks/share/tarantool/probe.lua',
                       'return "filesystem"')
    treegen.write_file(dir, 'wd/.rocks/share/tarantool/probe.lua',
                       'return "lexical"')
    treegen.write_file(dir, 'wd/.rocks/share/tarantool/lexical_only.lua',
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
        print('rocks searchroot preserves filesystem traversal')
        os.exit(0)
    ]])
    local res = justrun.tarantool(dir, {LUA_PATH = ''}, {'main.lua'},
        {nojson = true, stderr = true, setsearchroot = false})
    t.assert_equals(res.exit_code, 0, res.stderr)
    t.assert_str_contains(res.stdout,
                          'rocks searchroot preserves filesystem traversal')
end
