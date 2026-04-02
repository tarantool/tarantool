local t = require('luatest')
local fio = require('fio')
local it = require('test.interactive_tarantool')

local g = t.group()

local function write_file(path, content)
    local fh = fio.open(path, {'O_WRONLY', 'O_CREAT', 'O_TRUNC'},
                         tonumber('0600', 8))
    fh:write(content)
    fh:close()
end

local function file_contains(path, needle)
    local fh = fio.open(path, {'O_RDONLY'})
    if fh == nil then
        return false
    end
    local content = fh:read(65536)
    fh:close()

    if content == nil then
        return false
    end
    return content:find(needle, 1, true) ~= nil
end

-- Execute a unique marker command in the interactive console and return
-- the marker string. The history file in use will contain this marker.
local function execute_marker(instance)
    local marker = 'history_marker_' .. tostring(math.random(100000, 999999))
    instance:roundtrip("'" .. marker .. "'", marker)
    return marker
end

g.after_each(function(g)
    if g.it ~= nil then
        g.it:close()
        g.it = nil
    end
    if g.temp_dir ~= nil then
        fio.rmtree(g.temp_dir)
        g.temp_dir = nil
    end
end)

-- When $HOME/.tarantool_history exists, it takes priority over
-- XDG_STATE_HOME and $HOME/.local/state paths.
g.test_legacy_home_history_takes_priority = function(g)
    g.temp_dir = fio.tempdir()
    local home_dir = fio.pathjoin(g.temp_dir, 'home')
    local xdg_state_home = fio.pathjoin(g.temp_dir, 'xdg_state')
    local xdg_tarantool_dir = fio.pathjoin(xdg_state_home, 'tarantool')
    local home_state_dir = fio.pathjoin(home_dir, '.local', 'state',
                                        'tarantool')

    fio.mktree(home_dir)
    fio.mktree(xdg_tarantool_dir)
    fio.mktree(home_state_dir)

    local home_history = fio.pathjoin(home_dir, '.tarantool_history')
    local xdg_history = fio.pathjoin(xdg_tarantool_dir, '.tarantool_history')

    write_file(home_history, 'legacy_cmd\n')

    g.it = it.new({
        env = {
            HOME = home_dir,
            XDG_STATE_HOME = xdg_state_home,
        }
    })

    local marker = execute_marker(g.it)

    t.assert(file_contains(home_history, marker),
             'history should be saved to $HOME/.tarantool_history')
    t.assert_not(file_contains(xdg_history, marker),
                 'history must not be saved to XDG path when legacy exists')
end

-- When $HOME/.tarantool_history does not exist but
-- $XDG_STATE_HOME/tarantool/ directory does, history is saved there.
g.test_xdg_state_home_dir_used = function(g)
    g.temp_dir = fio.tempdir()
    local home_dir = fio.pathjoin(g.temp_dir, 'home')
    local xdg_state_home = fio.pathjoin(g.temp_dir, 'xdg_state')
    local xdg_tarantool_dir = fio.pathjoin(xdg_state_home, 'tarantool')

    fio.mktree(home_dir)
    fio.mktree(xdg_tarantool_dir)

    local xdg_history = fio.pathjoin(xdg_tarantool_dir,
                                     '.tarantool_history')
    local home_history = fio.pathjoin(home_dir, '.tarantool_history')

    g.it = it.new({
        env = {
            HOME = home_dir,
            XDG_STATE_HOME = xdg_state_home,
        }
    })

    local marker = execute_marker(g.it)

    t.assert(file_contains(xdg_history, marker),
             'history should be saved to $XDG_STATE_HOME/tarantool/')
    t.assert_not(fio.path.exists(home_history),
                 'legacy history file must not be created')
end

-- When neither legacy file nor XDG dir exist, but
-- $HOME/.local/state/tarantool/ directory exists, history is saved there.
g.test_home_local_state_fallback = function(g)
    g.temp_dir = fio.tempdir()
    local home_dir = fio.pathjoin(g.temp_dir, 'home')
    local home_state_dir = fio.pathjoin(home_dir, '.local', 'state',
                                        'tarantool')

    fio.mktree(home_state_dir)

    local state_history = fio.pathjoin(home_state_dir, '.tarantool_history')
    local home_history = fio.pathjoin(home_dir, '.tarantool_history')

    g.it = it.new({
        env = {
            HOME = home_dir,
        }
    })

    local marker = execute_marker(g.it)

    t.assert(file_contains(state_history, marker),
             'history should be saved to $HOME/.local/state/tarantool/')
    t.assert_not(fio.path.exists(home_history),
                 'legacy history file must not be created')
end

-- When XDG_STATE_HOME is set but the tarantool subdirectory does not exist,
-- fall back to $HOME/.tarantool_history if it exists.
g.test_xdg_nonexistent_dir_falls_back_to_legacy = function(g)
    g.temp_dir = fio.tempdir()
    local home_dir = fio.pathjoin(g.temp_dir, 'home')
    local xdg_state_home = fio.pathjoin(g.temp_dir, 'xdg_state')

    fio.mktree(home_dir)

    local home_history = fio.pathjoin(home_dir, '.tarantool_history')
    write_file(home_history, 'old_cmd\n')

    g.it = it.new({
        env = {
            HOME = home_dir,
            XDG_STATE_HOME = xdg_state_home,
        }
    })

    local marker = execute_marker(g.it)

    t.assert(file_contains(home_history, marker),
             'history should fall back to $HOME/.tarantool_history')
    t.assert_not(fio.path.exists(fio.pathjoin(xdg_state_home, 'tarantool')),
                 'non-existent XDG dir must not be created')
end

-- Fresh install with XDG_STATE_HOME set: directory is created and history
-- is saved to $XDG_STATE_HOME/tarantool/.tarantool_history.
g.test_fresh_install_creates_xdg_dir = function(g)
    g.temp_dir = fio.tempdir()
    local home_dir = fio.pathjoin(g.temp_dir, 'home')
    local xdg_state_home = fio.pathjoin(g.temp_dir, 'xdg_state')

    fio.mktree(home_dir)

    local xdg_history = fio.pathjoin(xdg_state_home, 'tarantool',
                                     '.tarantool_history')

    g.it = it.new({
        env = {
            HOME = home_dir,
            XDG_STATE_HOME = xdg_state_home,
        }
    })

    local marker = execute_marker(g.it)

    t.assert(file_contains(xdg_history, marker),
             'history should be saved to newly created XDG dir')
    t.assert_not(fio.path.exists(fio.pathjoin(home_dir, '.tarantool_history')),
                 'legacy history file must not be created')
end

-- Fresh install without XDG_STATE_HOME: directory is created and history
-- is saved to $HOME/.local/state/tarantool/.tarantool_history.
g.test_fresh_install_creates_home_state_dir = function(g)
    g.temp_dir = fio.tempdir()
    local home_dir = fio.pathjoin(g.temp_dir, 'home')

    fio.mktree(home_dir)

    local state_history = fio.pathjoin(home_dir, '.local', 'state',
                                       'tarantool', '.tarantool_history')

    g.it = it.new({
        env = {
            HOME = home_dir,
        }
    })

    local marker = execute_marker(g.it)

    t.assert(file_contains(state_history, marker),
             'history should be saved to newly created state dir')
    t.assert_not(fio.path.exists(fio.pathjoin(home_dir,
                                              '.tarantool_history')),
                 'legacy history file must not be created')
end
