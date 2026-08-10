local fio = require('fio')
local json = require('json')
local popen = require('popen')
local socket = require('socket')
local t = require('luatest')

local SRC_DIR = fio.abspath(os.getenv('TARANTOOL_SRC_DIR') or '../..')
local TOOL = fio.pathjoin(SRC_DIR, 'tools/gh-review.py')
local PYTHON = os.getenv('PYTHON_EXECUTABLE') or 'python3'
local PR = 77
-- Environment for the fixture commands: the inherited one, but with
-- the global git config cut off, so that the developer's settings
-- (commit.gpgsign, diff.*) cannot break or reshape the scratch repo.
local SHELL_ENV = os.environ()
SHELL_ENV.GIT_CONFIG_GLOBAL = '/dev/null'
SHELL_ENV.GIT_CONFIG_NOSYSTEM = '1'
--
-- gh-review.py tool tests.
--
local g = t.group('gh_review_tool')

local function read_all(ph, opts)
    local res = ''
    while true do
        local chunk = ph:read(opts)
        if chunk == nil or chunk == '' then
            return res
        end
        res = res .. chunk
    end
end

-- Run a shell command in the given directory, return its output, fail the test
-- if the command fails.
local function shell(dir, cmd)
    cmd = ('cd %q && %s'):format(dir, cmd)
    local ph = popen.new({'/bin/sh', '-c', cmd},
                         {env = SHELL_ENV, stdout = popen.opts.PIPE})
    t.assert(ph)
    local out = read_all(ph)
    local st = ph:wait()
    ph:close()
    t.assert_equals(st.exit_code, 0, ('%s:\n%s'):format(cmd, out))
    return out
end

--
-- The fake GitHub server. Serves the routes installed by each test case and
-- records every received request for the assertions.
--
local function gh_handle_client(gh, fd)
    local head = fd:read({delimiter = '\r\n\r\n'})
    if head == nil or head == '' then
        return
    end
    local method, path = head:match('^(%u+) (%S+) HTTP')
    local clen = tonumber(head:match('[Cc]ontent%-[Ll]ength: (%d+)')) or 0
    local body = ''
    if clen > 0 then
        body = fd:read(clen)
    end
    local req = {method = method, path = path, body = body, head = head}
    table.insert(gh.requests, req)
    local reply = nil
    for _, r in ipairs(gh.routes) do
        if method == r.method and path:match(r.pattern) then
            reply = r.reply
            break
        end
    end
    if reply == nil then
        reply = {status = 404, body = {message = 'no such route'}}
    elseif type(reply) == 'function' then
        reply = reply(req)
    end
    local status = reply.status or 200
    local rbody = reply.body or ''
    if type(rbody) == 'table' then
        rbody = json.encode(rbody)
    end
    local ctype = reply.content_type or 'application/json'
    local headers = {
        ['Content-Type'] = ctype .. '; charset=utf-8',
        ['Content-Length'] = #rbody,
        ['Connection'] = 'close',
    }
    if reply.link ~= nil then
        headers['Link'] = reply.link
    end
    local lines = {('HTTP/1.1 %d X'):format(status)}
    for k, v in pairs(headers) do
        table.insert(lines, ('%s: %s'):format(k, v))
    end
    fd:write(table.concat(lines, '\r\n') .. '\r\n\r\n' .. rbody)
end

-- Requests matching the pattern, as 'METHOD path' strings.
local function gh_log(cg, pattern)
    local res = {}
    for _, req in ipairs(cg.gh.requests) do
        local line = req.method .. ' ' .. req.path
        if pattern == nil or line:match(pattern) then
            table.insert(res, line)
        end
    end
    return res
end

-- The only request matching the pattern, with the parsed JSON body.
local function gh_request(cg, pattern)
    local found = nil
    for _, req in ipairs(cg.gh.requests) do
        if (req.method .. ' ' .. req.path):match(pattern) then
            t.assert_equals(found, nil, 'one request expected: ' .. pattern)
            found = req
        end
    end
    t.assert_not_equals(found, nil, 'no request matching: ' .. pattern)
    if found.body ~= '' then
        found.json = json.decode(found.body)
    end
    return found
end

local function run_tool(cg, argline, env_extra)
    local env = {
        PATH = os.getenv('PATH'),
        HOME = os.getenv('HOME'),
        -- HOME is needed, but brings the developer's ~/.gitconfig
        -- within the reach of the tool's git calls - keep those
        -- hermetic.
        GIT_CONFIG_GLOBAL = SHELL_ENV.GIT_CONFIG_GLOBAL,
        GIT_CONFIG_NOSYSTEM = SHELL_ENV.GIT_CONFIG_NOSYSTEM,
        GH_TOKEN = 'test-token',
        GH_COOKIE = 'Test-Bot:test-session-cookie',
        GH_API_URL = cg.url,
        GH_WEB_URL = cg.url,
    }
    for k, v in pairs(env_extra or {}) do
        env[k] = v ~= '' and v or nil
    end
    local cmd = ('cd %q && %q %q %s'):format(cg.repo, PYTHON, TOOL,
                                             argline)
    local ph = popen.new({'/bin/sh', '-c', cmd},
                         {env = env, stdout = popen.opts.PIPE,
                          stderr = popen.opts.PIPE})
    t.assert(ph)
    -- Read before waiting - a child blocked on a full pipe would
    -- never exit.
    local res = {
        stdout = read_all(ph),
        stderr = read_all(ph, {stderr = true}),
    }
    res.code = ph:wait().exit_code
    ph:close()
    return res
end

local function run_tool_ok(cg, argline, env_extra)
    local res = run_tool(cg, argline, env_extra)
    t.assert_equals(res.code, 0, res.stderr)
    return res
end

local function run_tool_err(cg, argline, env_extra)
    local res = run_tool(cg, argline, env_extra)
    t.assert_equals(res.code, 1, res.stdout)
    return res
end

--
-- The Python unit-call bridge: import the tool, evaluate one
-- expression, return it as decoded JSON.
--
local function py_call(expr)
    local script = ([[
import importlib.util, json
spec = importlib.util.spec_from_file_location('ghr', %s)
m = importlib.util.module_from_spec(spec)
spec.loader.exec_module(m)
print(json.dumps(%s))
]]):format(json.encode(TOOL), expr)
    local ph = popen.new({PYTHON, '-c', script},
                         {env = {PATH = os.getenv('PATH')},
                          stdout = popen.opts.PIPE,
                          stderr = popen.opts.PIPE})
    t.assert(ph)
    local out = read_all(ph)
    local err = read_all(ph, {stderr = true})
    local st = ph:wait()
    ph:close()
    t.assert_equals(st.exit_code, 0, err)
    return json.decode(out)
end

--
-- The scratch git repository:
--
--   main:  base -> C (modifies 'line 20', adds other.txt)
--   pr:    base -> A (inserts A-one, A-two after line 5)
--               -> B (deletes A-two and the original line 10)
--
-- The main branch advancing after the fork makes the base...commit three-dot
-- diff differ from the plain two-dot one, so the position computation is
-- checked against the semantics GitHub actually uses.
--

local function write_file(dir, name, lines)
    local fh = fio.open(fio.pathjoin(dir, name),
                        {'O_WRONLY', 'O_CREAT', 'O_TRUNC'}, tonumber(644, 8))
    t.assert(fh)
    fh:write(table.concat(lines, '\n') .. '\n')
    fh:close()
end

g.before_all(function(cg)
    -- popen.new() does not resolve the program name via PATH - find
    -- the interpreter's absolute path. Also skip when there is none.
    local ph = popen.shell('command -v ' .. PYTHON, 'r')
    local out = read_all(ph)
    local st = ph:wait()
    ph:close()
    t.skip_if(st.exit_code ~= 0, 'no python interpreter')
    PYTHON = out:strip()

    local base_lines = {}
    for i = 1, 20 do
        table.insert(base_lines, 'line ' .. i)
    end

    local vardir = fio.abspath(os.getenv('VARDIR') or 'test/var')
    cg.repo = fio.pathjoin(vardir, 'scratch-repo')
    fio.rmtree(cg.repo)
    t.assert(fio.mktree(cg.repo))
    local r = cg.repo
    -- 'git init -b <name>' needs git >= 2.28 and 'git switch' needs
    -- >= 2.23, while some CI machines have an older git - stick to the
    -- ancient equivalents: a post-commit branch rename and 'checkout'.
    shell(r, 'git init -q . && git config user.email t@t.t && ' ..
             'git config user.name t && ' ..
             'git remote add origin git@github.com:test/repo.git')
    write_file(r, 'f.txt', base_lines)
    shell(r, 'git add . && git commit -qm base && git branch -m main')

    shell(r, 'git checkout -qb pr')
    local lines = table.copy(base_lines)
    table.insert(lines, 6, 'A-two')
    table.insert(lines, 6, 'A-one')
    write_file(r, 'f.txt', lines)
    shell(r, 'git commit -aqm commit-A')
    cg.sha_a = shell(r, 'git rev-parse HEAD'):strip()

    -- Delete 'A-two' (line 7) and the original 'line 10' (line 12).
    table.remove(lines, 12)
    table.remove(lines, 7)
    write_file(r, 'f.txt', lines)
    shell(r, 'git commit -aqm commit-B')
    cg.sha_b = shell(r, 'git rev-parse HEAD'):strip()

    shell(r, 'git checkout -q main')
    lines = table.copy(base_lines)
    lines[20] = 'line 20 main'
    write_file(r, 'f.txt', lines)
    write_file(r, 'other.txt', {'other'})
    shell(r, 'git add . && git commit -aqm commit-C')
    cg.sha_main = shell(r, 'git rev-parse HEAD'):strip()
end)

g.before_each(function(cg)
    cg.gh = {requests = {}, routes = {}}
    cg.server = socket.tcp_server('localhost', 0, {handler = function(fd)
        local ok, err = pcall(gh_handle_client, cg.gh, fd)
        if not ok then
            cg.gh.error = err
        end
    end})
    t.assert(cg.server)
    cg.url = 'http://localhost:' .. cg.server:name().port
end)

g.after_each(function(cg)
    if cg.server ~= nil then
        cg.server:close()
        cg.server = nil
    end
    t.assert_equals(cg.gh.error, nil)
end)

-- The routes common for the public path: the PR metadata and the
-- pending review listing with the given contents.
local function install_public_routes(cg, reviews)
    local prefix = '/repos/test/repo/pulls/' .. PR
    table.insert(cg.gh.routes, {
        method = 'GET', pattern = '^' .. prefix .. '$',
        reply = {body = {base = {sha = cg.sha_main, ref = 'main'}}},
    })
    table.insert(cg.gh.routes, {
        method = 'GET', pattern = '^' .. prefix .. '/reviews',
        reply = {body = reviews or {}},
    })
end

g.test_unit_patch_position = function()
    local patch = table.concat({
        '@@ -3,6 +3,8 @@',
        ' line 3',
        ' line 4',
        ' line 5',
        '+A-one',
        '+A-two',
        ' line 6',
        '-line 7',
        ' line 8',
    }, '\n')
    local cases = {
        -- {line, is_left, expected position}.
        -- Context lines are addressable from both sides.
        {3, false, 1},
        {3, true, 1},
        -- Added lines - only on the right.
        {6, false, 4},
        {7, false, 5},
        -- The right line 6 is the added one, the old line 6 is a context line
        -- further below.
        {6, true, 6},
        -- Deleted lines - only on the left.
        {7, true, 7},
        -- Not in the patch at all.
        {100, false, json.NULL},
        {1, false, json.NULL},
    }
    -- One Python invocation for all the cases - the interpreter startup is much
    -- more expensive than the calls themselves.
    local calls = {}
    for _, c in ipairs(cases) do
        table.insert(calls, ('m.patch_position(%s, %d, %s)'):format(
                     json.encode(patch), c[1], c[2] and 'True' or 'False'))
    end
    local res = py_call('[' .. table.concat(calls, ', ') .. ']')
    for i, c in ipairs(cases) do
        t.assert_equals(res[i], c[3],
                        ('case %d: line %d, is_left=%s'):format(
                        i, c[1], tostring(c[2])))
    end
end

-- A child producing more than the pipe buffer (~64KB) deadlocks
-- against a wait-before-read runner: the reading-first order in
-- py_call()/run_tool() is what keeps this test from hanging.
g.test_unit_large_child_output = function()
    t.assert_equals(#py_call('"x" * (1024 * 1024)'), 1024 * 1024)
end

g.test_public_api_comment = function(cg)
    install_public_routes(cg)
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = '/pulls/' .. PR .. '/reviews$',
        reply = {body = {id = 1, node_id = 'R_test'}},
    })
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = '/graphql$',
        reply = {body = {data = {addPullRequestReviewComment =
                                 {comment = {databaseId = 42}}}}},
    })
    local res = run_tool_ok(cg, ('comment --pr %d -c %s -p f.txt -l 6 ' ..
                                 '-m test-body'):format(PR, cg.sha_a))
    t.assert_str_contains(res.stdout, 'Added pending comment 42')
    -- The review is created only after the anchor is validated, and the
    -- position is computed against the three-dot diff: the base branch changes
    -- after the fork must not shift it.
    t.assert_equals(gh_log(cg), {
        'GET /repos/test/repo/pulls/' .. PR,
        'GET /repos/test/repo/pulls/' .. PR .. '/reviews?per_page=100',
        'POST /repos/test/repo/pulls/' .. PR .. '/reviews',
        'POST /graphql',
    })
    local gql = gh_request(cg, 'POST /graphql').json
    t.assert_equals(gql.variables.pos, 4)
    t.assert_equals(gql.variables.sha, cg.sha_a)
    t.assert_equals(gql.variables.body, 'test-body')
    t.assert_equals(gql.variables.rid, 'R_test')
end

-- The position is an offset into the diff as GitHub renders it, while
-- 'git diff' obeys the local config knobs reshaping the patch: the context
-- width, the hunk-boundary algorithm, the colors. The tool must pin the diff
-- format, or the same line would silently yield a different position - and a
-- misplaced comment on GitHub.
g.test_public_api_comment_hostile_diff_config = function(cg)
    shell(cg.repo, 'git config diff.context 5 && ' ..
                   'git config diff.algorithm patience && ' ..
                   'git config color.diff always')
    install_public_routes(cg)
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = '/pulls/' .. PR .. '/reviews$',
        reply = {body = {id = 1, node_id = 'R_test'}},
    })
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = '/graphql$',
        reply = {body = {data = {addPullRequestReviewComment =
                                 {comment = {databaseId = 42}}}}},
    })
    run_tool_ok(cg, ('comment --pr %d -c %s -p f.txt -l 6 ' ..
                     '-m test-body'):format(PR, cg.sha_a))
    local gql = gh_request(cg, 'POST /graphql').json
    t.assert_equals(gql.variables.pos, 4)
end

g.after_test('test_public_api_comment_hostile_diff_config', function(cg)
    shell(cg.repo, 'git config --remove-section diff && ' ..
                   'git config --remove-section color')
end)

g.test_public_api_bad_anchor_creates_nothing = function(cg)
    install_public_routes(cg)
    local res = run_tool_err(cg, ('comment --pr %d -c %s -p f.txt ' ..
                                  '-l 99 -m x'):format(PR, cg.sha_a))
    t.assert_str_contains(res.stderr, 'is not in the PR diff')
    t.assert_equals(gh_log(cg, 'POST'), {})
end

g.test_public_api_unknown_commit = function(cg)
    install_public_routes(cg)
    local res = run_tool_err(cg, ('comment --pr %d -c deadbeef12 ' ..
                                  '-p f.txt -l 6 -m x'):format(PR))
    t.assert_str_contains(res.stderr,
                          'not in the local repo - fetch the PR')
end

g.test_public_api_unknown_base = function(cg)
    local prefix = '/repos/test/repo/pulls/' .. PR
    table.insert(cg.gh.routes, {
        method = 'GET', pattern = '^' .. prefix .. '$',
        reply = {body = {base = {sha = ('e'):rep(40), ref = 'main'}}},
    })
    local res = run_tool_err(cg, ('comment --pr %d -c %s -p f.txt ' ..
                                  '-l 6 -m x'):format(PR, cg.sha_a))
    t.assert_str_contains(res.stderr, 'git fetch origin main')
end

local PRIVATE_API_PATTERN =
    '/test/repo/pull/' .. PR .. '/page_data/create_review_comment$'

g.test_private_api_comment = function(cg)
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = PRIVATE_API_PATTERN,
        reply = {body = {}},
    })
    local res = run_tool_ok(cg, ('comment --pr %d --experimental ' ..
                                 '-c %s -p f.txt -l 6 -m exp-body')
                                :format(PR, cg.sha_b))
    t.assert_str_contains(res.stdout, 'via the private API')
    -- The private API is the only request: no REST at all, no pending review
    -- pre-creation.
    t.assert_equals(#cg.gh.requests, 1)
    local req = gh_request(cg, PRIVATE_API_PATTERN).json
    t.assert_equals(req.text, 'exp-body')
    t.assert_equals(req.line, 6)
    t.assert_equals(req.comparisonStartOid, cg.sha_a)
    t.assert_equals(req.comparisonEndOid, cg.sha_b)
    t.assert_equals(req.positioning.commitOid, cg.sha_b)
    t.assert_equals(req.side, 'right')
end

-- The experimental path authenticates with the cookie alone, so the
-- token must not even be resolved for it: with no GH_TOKEN in the
-- environment and no `gh` binary in PATH an eager resolution would
-- crash before the single private-API request is made.
g.test_private_api_comment_no_token = function(cg)
    local bindir = fio.pathjoin(fio.dirname(cg.repo), 'bin-no-gh')
    fio.rmtree(bindir)
    t.assert(fio.mktree(bindir))
    local git = shell(cg.repo, 'command -v git'):strip()
    t.assert(fio.symlink(git, fio.pathjoin(bindir, 'git')))
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = PRIVATE_API_PATTERN,
        reply = {body = {}},
    })
    local res = run_tool_ok(cg, ('comment --pr %d --experimental ' ..
                                 '-c %s -p f.txt -l 6 -m exp-body')
                                :format(PR, cg.sha_b),
                            {GH_TOKEN = '', PATH = bindir})
    t.assert_str_contains(res.stdout, 'via the private API')
    t.assert_equals(#cg.gh.requests, 1)
end

g.test_private_api_left_and_range = function(cg)
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = PRIVATE_API_PATTERN,
        reply = {body = {}},
    })
    -- A deleted line: 'A-two' exists only in the parent of B.
    local res = run_tool_ok(cg, ('comment --pr %d --experimental ' ..
                                 '-c %s -p f.txt -l 7 --left -m x')
                                :format(PR, cg.sha_b))
    t.assert_str_contains(res.stdout, 'f.txt:7')
    local req = gh_request(cg, PRIVATE_API_PATTERN).json
    t.assert_equals(req.side, 'left')
    t.assert_equals(req.positioning.commitOid, cg.sha_a)
    -- A range.
    cg.gh.requests = {}
    run_tool_ok(cg, ('comment --pr %d --experimental -c %s -p f.txt ' ..
                     '--start-line 6 -l 7 -m x'):format(PR, cg.sha_a))
    req = gh_request(cg, PRIVATE_API_PATTERN).json
    t.assert_equals(req.subjectType, 'multiline')
    t.assert_equals(req.positioning.startLine, 6)
    t.assert_equals(req.positioning.endLine, 7)
end

g.test_private_api_api_errors = function(cg)
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = PRIVATE_API_PATTERN,
        reply = {status = 422, body = {error = 'Line could not be ' ..
                                               'resolved.'}},
    })
    local args = ('comment --pr %d --experimental -c %s -p f.txt ' ..
                  '-l 6 -m x'):format(PR, cg.sha_b)
    local res = run_tool_err(cg, args)
    t.assert_str_contains(res.stderr,
                          'the private API returned HTTP 422')
    t.assert_str_contains(res.stderr, 'Line could not be resolved')
    -- Non-JSON response - the expired cookie symptom.
    cg.gh.routes = {{
        method = 'POST', pattern = PRIVATE_API_PATTERN,
        reply = {body = 'sign in', content_type = 'text/html'},
    }}
    res = run_tool_err(cg, args)
    t.assert_str_contains(res.stderr, 'did not return JSON')
end

g.test_cookie_validation = function(cg)
    local args = ('comment --pr %d --experimental -c %s -p f.txt ' ..
                  '-l 6 -m x'):format(PR, cg.sha_b)
    local res = run_tool_err(cg, args, {GH_COOKIE = ''})
    t.assert_str_contains(res.stderr, 'requires the session cookie')
    res = run_tool_err(cg, args, {GH_COOKIE = 'no-login-prefix'})
    t.assert_str_contains(res.stderr, '<login>:<cookie> format')
    -- The cookie is validated before anything is sent.
    t.assert_equals(#cg.gh.requests, 0)
end

g.test_status = function(cg)
    install_public_routes(cg, {{id = 7, node_id = 'R_st',
                                state = 'PENDING'}})
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = '/graphql$',
        reply = function(req)
            local page
            if req.body:match('"cursor": null') then
                page = {
                    pageInfo = {hasNextPage = true, endCursor = 'C1'},
                    nodes = {{databaseId = 1, body = 'one\nmore',
                              path = 'f.txt', originalLine = 6,
                              originalStartLine = json.NULL,
                              originalCommit =
                                  {abbreviatedOid = 'abc1234'}}},
                }
            else
                page = {
                    pageInfo = {hasNextPage = false,
                                endCursor = json.NULL},
                    nodes = {{databaseId = 2, body = 'two',
                              path = 'f.txt', originalLine = 7,
                              originalStartLine = 6,
                              originalCommit =
                                  {abbreviatedOid = 'abc1234'}}},
                }
            end
            return {body = {data = {node = {comments = page}}}}
        end,
    })
    local res = run_tool_ok(cg, 'status --pr ' .. PR)
    t.assert_str_contains(res.stdout, '2 comment(s)')
    t.assert_str_contains(res.stdout, '1 abc1234 f.txt:6 | one')
    t.assert_str_contains(res.stdout, '2 abc1234 f.txt:6-7 | two')
end

g.test_status_no_pending = function(cg)
    install_public_routes(cg)
    local res = run_tool_ok(cg, 'status --pr ' .. PR)
    t.assert_str_contains(res.stdout, 'No pending review')
end

g.test_list_pagination = function(cg)
    -- The pending review is on the second page: without following the
    -- Link headers it is invisible.
    local prefix = '/repos/test/repo/pulls/' .. PR
    table.insert(cg.gh.routes, {
        method = 'GET', pattern = '^' .. prefix .. '/reviews%?per_page',
        reply = {
            body = {{id = 1, state = 'COMMENTED'}},
            link = ('<%s%s/reviews?page=2>; rel="next"'):format(
                   cg.url, prefix),
        },
    })
    table.insert(cg.gh.routes, {
        method = 'GET', pattern = '^' .. prefix .. '/reviews%?page=2',
        reply = {body = {{id = 2, node_id = 'R_p2',
                          state = 'PENDING'}}},
    })
    table.insert(cg.gh.routes, {
        method = 'DELETE', pattern = prefix .. '/reviews/2$',
        reply = {body = {}},
    })
    local res = run_tool_ok(cg, 'abort --pr ' .. PR)
    t.assert_str_contains(res.stdout, 'Dropped pending review 2')
end

g.test_submit_abort = function(cg)
    install_public_routes(cg, {{id = 7, node_id = 'R_su',
                                state = 'PENDING'}})
    local prefix = '/repos/test/repo/pulls/' .. PR
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = prefix .. '/reviews/7/events$',
        reply = {body = {}},
    })
    table.insert(cg.gh.routes, {
        method = 'DELETE', pattern = prefix .. '/reviews/7$',
        reply = {body = {}},
    })
    local res = run_tool_ok(cg, 'submit --pr ' .. PR .. ' -m summary')
    t.assert_str_contains(res.stdout, 'Submitted review 7')
    local req = gh_request(cg, 'POST .*/events').json
    t.assert_equals(req.event, 'COMMENT')
    t.assert_equals(req.body, 'summary')
    res = run_tool_ok(cg, 'abort --pr ' .. PR)
    t.assert_str_contains(res.stdout, 'Dropped pending review 7')
    t.assert_equals(#gh_log(cg, 'DELETE'), 1)
end

g.test_submit_no_pending = function(cg)
    install_public_routes(cg)
    local res = run_tool_err(cg, 'submit --pr ' .. PR .. ' -m x')
    t.assert_str_contains(res.stderr, 'no pending review')
end

g.test_graphql_errors_are_fatal = function(cg)
    install_public_routes(cg, {{id = 7, node_id = 'R_ge',
                                state = 'PENDING'}})
    table.insert(cg.gh.routes, {
        method = 'POST', pattern = '/graphql$',
        reply = {body = {data = json.NULL,
                         errors = {{message = 'boom'}}}},
    })
    local res = run_tool_err(cg, 'status --pr ' .. PR)
    t.assert_str_contains(res.stderr, 'GraphQL failed')
    t.assert_str_contains(res.stderr, 'boom')
end
