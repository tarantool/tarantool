local t = require('luatest')
local fio = require('fio')
local it = require('test.interactive_tarantool')

local g = t.group()

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

g.test_xdg_state_home_priority = function(g)
    g.temp_dir = fio.tempdir()
    local xdg_state_home = fio.pathjoin(g.temp_dir, 'xdg_state')
    local xdg_tarantool_dir = fio.pathjoin(xdg_state_home, 'tarantool')
    local home_dir = fio.pathjoin(g.temp_dir, 'home')
    local home_state_dir = fio.pathjoin(home_dir, '.local', 'state', 'tarantool')
    
    fio.mktree(xdg_tarantool_dir)
    fio.mktree(home_state_dir)
    
    local xdg_history_file = fio.pathjoin(xdg_tarantool_dir, '.tarantool_history')
    local home_history_file = fio.pathjoin(home_dir, '.tarantool_history')
    
    local xdg_content = 'xdg_command_1\nxdg_command_2\n'
    local home_content = 'home_command_1\nhome_command_2\n'
    
    local xdg_fh = fio.open(xdg_history_file, {'O_WRONLY', 'O_CREAT'}, 0600)
    xdg_fh:write(xdg_content)
    xdg_fh:close()
    
    local home_fh = fio.open(home_history_file, {'O_WRONLY', 'O_CREAT'}, 0600)
    home_fh:write(home_content)
    home_fh:close()
    
    g.it = it.new({
        env = {
            XDG_STATE_HOME = xdg_state_home,
            HOME = home_dir,
        }
    })
    
    g.it:roundtrip("require('strict').off()")
    local result = g.it:roundtrip("return os.getenv('XDG_STATE_HOME')")
    t.assert_equals(result, xdg_state_home)
end

g.test_home_local_state_fallback = function(g)
    g.temp_dir = fio.tempdir()
    local home_dir = fio.pathjoin(g.temp_dir, 'home')
    local home_state_dir = fio.pathjoin(home_dir, '.local', 'state', 'tarantool')
    
    fio.mktree(home_state_dir)
    
    local home_history_file = fio.pathjoin(home_state_dir, '.tarantool_history')
    local old_history_file = fio.pathjoin(home_dir, '.tarantool_history')
    
    local state_content = 'state_command_1\nstate_command_2\n'
    local old_content = 'old_command_1\nold_command_2\n'
    
    local state_fh = fio.open(home_history_file, {'O_WRONLY', 'O_CREAT'}, 0600)
    state_fh:write(state_content)
    state_fh:close()
    
    local old_fh = fio.open(old_history_file, {'O_WRONLY', 'O_CREAT'}, 0600)
    old_fh:write(old_content)
    old_fh:close()
    
    g.it = it.new({
        env = {
            HOME = home_dir,
        }
    })
    
    g.it:roundtrip("require('strict').off()")
    local result = g.it:roundtrip("return os.getenv('HOME')")
    t.assert_equals(result, home_dir)
end

g.test_home_fallback_no_xdg = function(g)
    g.temp_dir = fio.tempdir()
    local home_dir = fio.pathjoin(g.temp_dir, 'home')
    
    fio.mktree(home_dir)
    
    local old_history_file = fio.pathjoin(home_dir, '.tarantool_history')
    local old_content = 'old_command_1\nold_command_2\n'
    
    local old_fh = fio.open(old_history_file, {'O_WRONLY', 'O_CREAT'}, 0600)
    old_fh:write(old_content)
    old_fh:close()
    
    g.it = it.new({
        env = {
            HOME = home_dir,
        }
    })
    
    g.it:roundtrip("require('strict').off()")
    local result = g.it:roundtrip("return os.getenv('HOME')")
    t.assert_equals(result, home_dir)
end

g.test_xdg_state_home_nonexistent_dir = function(g)
    g.temp_dir = fio.tempdir()
    local xdg_state_home = fio.pathjoin(g.temp_dir, 'xdg_state')
    local home_dir = fio.pathjoin(g.temp_dir, 'home')
    
    fio.mktree(home_dir)
    
    local old_history_file = fio.pathjoin(home_dir, '.tarantool_history')
    local old_content = 'old_command_1\nold_command_2\n'
    
    local old_fh = fio.open(old_history_file, {'O_WRONLY', 'O_CREAT'}, 0600)
    old_fh:write(old_content)
    old_fh:close()
    
    g.it = it.new({
        env = {
            XDG_STATE_HOME = xdg_state_home,
            HOME = home_dir,
        }
    })
    
    g.it:roundtrip("require('strict').off()")
    local result = g.it:roundtrip("return os.getenv('HOME')")
    t.assert_equals(result, home_dir)
end
