-- Working tree generator.
--
-- Generates a tree of Lua files using provided templates and
-- filenames.
--
-- Basic usage:
--
-- | local t = require('luatest')
-- | local treegen = require('test.treegen')
-- |
-- | local g = t.group()
-- |
-- | local SCRIPT_TEMPLATE = [[
-- |     <...>
-- | ]]
-- |
-- | g.before_all(function(g)
-- |     treegen.init(g)
-- |     treegen.add_template(g, '^.*$', SCRIPT_TEMPLATE)
-- | end)
-- |
-- | g.after_all(function(g)
-- |     treegen.clean(g)
-- | end)
-- |
-- | g.foobar_test = function(g)
-- |     local dir = treegen.prepare_directory(g,
-- |         {'foo/bar.lua', 'main.lua'})
-- |     <..test case..>
-- | end

local fio = require('fio')
local log = require('log')
local fun = require('fun')

local treegen = {}

local function find_template(g, script)
    for _, template_def in ipairs(g.templates) do
        if script:match(template_def.pattern) then
            return template_def.template
        end
    end
    error(("treegen: can't find a template for script %q"):format(script))
end

-- Write provided script into the given directory.
function treegen.write_script(dir, script, body)
    local script_abspath = fio.pathjoin(dir, script)
    local flags = {'O_CREAT', 'O_WRONLY', 'O_TRUNC'}
    local mode = tonumber('644', 8)

    local scriptdir_abspath = fio.dirname(script_abspath)
    log.info(('Creating a directory: %s'):format(scriptdir_abspath))
    fio.mktree(scriptdir_abspath)

    log.info(('Writing a script: %s'):format(script_abspath))
    local fh = fio.open(script_abspath, flags, mode)
    fh:write(body)
    fh:close()
    return script_abspath
end

-- Generate a script that follows a template and write it at the
-- given path in the given directory.
local function gen_script(g, dir, script, replacements)
    local template = find_template(g, script)
    local replacements = fun.chain({script = script}, replacements):tomap()
    local body = template:gsub('<(.-)>', replacements)
    treegen.write_script(dir, script, body)
end

function treegen.init(g)
    g.tempdirs = {}
    g.templates = {}
end

-- Remove all temporary directories created by the test
-- unless KEEP_DATA environment variable is set to a
-- non-empty value.
function treegen.clean(g)
    local dirs = table.copy(g.tempdirs)
    g.tempdirs = nil

    local keep_data = (os.getenv('KEEP_DATA') or '') ~= ''

    for _, dir in ipairs(dirs) do
        if keep_data then
            log.info(('Left intact due to KEEP_DATA env var: %s'):format(dir))
        else
            log.info(('Recursively removing: %s'):format(dir))
            fio.rmtree(dir)
        end
    end

    g.templates = nil
end

function treegen.add_template(g, pattern, template)
    table.insert(g.templates, {
        pattern = pattern,
        template = template,
    })
end

-- Create a temporary directory with given scripts.
--
-- The scripts are generated using templates added by
-- treegen.add_template().
--
-- Example for {'foo/bar.lua', 'baz.lua'}:
--
-- /
-- + tmp/
--   + rfbWOJ/
--     + foo/
--     | + bar.lua
--     + baz.lua
--
-- The return value is '/tmp/rfbWOJ' for this example.
function treegen.prepare_directory(g, scripts, replacements)
    local replacements = replacements or {}

    assert(type(scripts) == 'table')
    assert(type(replacements) == 'table')

    local dir = fio.tempdir()
    table.insert(g.tempdirs, dir)

    for _, script in ipairs(scripts) do
        gen_script(g, dir, script, replacements)
    end

    return dir
end

return treegen
