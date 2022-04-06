local clock = require('clock')
local digest = require('digest')
local ffi = require('ffi')
local fiber = require('fiber')
local fio = require('fio')
local fun = require('fun')
local json = require('json')
local errno = require('errno')
local log = require('log')
local yaml = require('yaml')

local checks = require('checks')
local luatest = require('luatest')

ffi.cdef([[
    int kill(pid_t pid, int sig);
]])

local Server = luatest.Server:inherit({})

local WAIT_TIMEOUT = 60
local WAIT_DELAY = 0.1

local DEFAULT_CHECKPOINT_PATTERNS = {"*.snap", "*.xlog", "*.vylog",
                                     "*.inprogress", "[0-9]*/"}

-- Differences from luatest.Server:
--
-- * 'alias' is mandatory.
-- * 'command' is optional, assumed test/instances/default.lua by
--   default.
-- * 'datadir' is optional, specifies a directory: if specified, the directory's
--   contents will be recursively copied into 'workdir' during initialization.
-- * 'workdir' is optional, determined by 'alias'.
-- * The new 'box_cfg' parameter.
-- * engine - provides engine for parameterized tests
Server.constructor_checks = fun.chain(Server.constructor_checks, {
    alias = 'string',
    command = '?string',
    datadir = '?string',
    workdir = '?string',
    box_cfg = '?table',
    engine = '?string',
}):tomap()

Server.socketdir = fio.abspath(os.getenv('VARDIR') or 'test/var')

function Server.build_instance_uri(alias)
    return ('%s/%s.iproto'):format(Server.socketdir, alias)
end

function Server:initialize()
    if self.id == nil then
        local random = digest.urandom(9)
        self.id = digest.base64_encode(random, {urlsafe = true})
    end
    if self.command == nil then
        self.command = 'test/instances/default.lua'
    end
    if self.workdir == nil then
        self.workdir = ('%s/%s-%s'):format(self.socketdir, self.alias, self.id)
        fio.rmtree(self.workdir)
        fio.mktree(self.workdir)
    end
    if self.datadir ~= nil then
        local ok, err = fio.copytree(self.datadir, self.workdir)
        if not ok then
            error(string.format('Failed to copy directory: %s', err))
        end
        self.datadir = nil
    end
    if self.net_box_port == nil and self.net_box_uri == nil then
        self.net_box_uri = self.build_instance_uri(self.alias)
        fio.mktree(self.socketdir)
    end

    -- AFAIU, the inner getmetatable() returns our helpers.Server
    -- class, the outer one returns luatest.Server class.
    getmetatable(getmetatable(self)).initialize(self)
end

--- Generates environment to run process with.
-- The result is merged into os.environ().
-- @return map
function Server:build_env()
    local res = getmetatable(getmetatable(self)).build_env(self)
    if self.box_cfg ~= nil then
        res.TARANTOOL_BOX_CFG = json.encode(self.box_cfg)
    end
    res.TARANTOOL_ENGINE = self.engine
    return res
end

local function wait_cond(cond_name, server, func, ...)
    local alias = server.alias
    local id = server.id
    local pid = server.process.pid

    local deadline = clock.time() + WAIT_TIMEOUT
    while true do
        if func(...) then
            return
        end
        if clock.time() > deadline then
            error(('Waiting for "%s" on server %s-%s (PID %d) timed out')
                  :format(cond_name, alias, id, pid))
        end
        fiber.sleep(WAIT_DELAY)
    end
end

function Server:wait_for_readiness()
    return wait_cond('readiness', self, function()
        local ok, is_ready = pcall(function()
            self:connect_net_box()
            return self.net_box:eval('return _G.ready') == true
        end)
        return ok and is_ready
    end)
end

function Server:wait_election_leader()
    return wait_cond('election leader', self, self.exec, self, function()
        return box.info.election.state == 'leader'
    end)
end

function Server:wait_election_leader_found()
    return wait_cond('election leader is found', self, self.exec, self,
                     function() return box.info.election.leader ~= 0 end)
end

function Server:wait_election_term(term)
    return wait_cond('election term', self, self.exec, self, function(term)
        return box.info.election.term >= term
    end, {term})
end

function Server:wait_synchro_queue_term(term)
    return wait_cond('synchro queue term', self, self.exec, self, function(term)
        return box.info.synchro.queue.term >= term
    end, {term})
end

-- Unlike the original luatest.Server function it waits for
-- starting the server.
function Server:start(opts)
    checks('table', {
        wait_for_readiness = '?boolean',
    })
    getmetatable(getmetatable(self)).start(self)

    -- The option is true by default.
    local wait_for_readiness = true
    if opts ~= nil and opts.wait_for_readiness ~= nil then
        wait_for_readiness = opts.wait_for_readiness
    end

    if wait_for_readiness then
        self:wait_for_readiness()
    end
end

function Server:instance_id()
    -- Cache the value when found it first time.
    if self.instance_id_value then
        return self.instance_id_value
    end
    local id = self:exec(function() return box.info.id end)
    -- But do not cache 0 - it is an anon instance, its ID might change.
    if id ~= 0 then
        self.instance_id_value = id
    end
    return id
end

function Server:instance_uuid()
    -- Cache the value when found it first time.
    if self.instance_uuid_value then
        return self.instance_uuid_value
    end
    local uuid = self:exec(function() return box.info.uuid end)
    self.instance_uuid_value = uuid
    return uuid
end

function Server:election_term()
    return self:exec(function() return box.info.election.term end)
end

function Server:synchro_queue_term()
    return self:exec(function() return box.info.synchro.queue.term end)
end

-- TODO: Add the 'wait_for_readiness' parameter for the restart()
-- method.

-- Unlike the original luatest.Server function it waits until
-- the server will stop.
function Server:stop()
    local alias = self.alias
    local id = self.id
    if self.process then
        local pid = self.process.pid
        getmetatable(getmetatable(self)).stop(self)

        local deadline = clock.time() + WAIT_TIMEOUT
        while true do
            if ffi.C.kill(pid, 0) ~= 0 then
                break
            end
            if clock.time() > deadline then
                error(('Stopping of server %s-%s (PID %d) was timed out'):format(
                    alias, id, pid))
            end
            fiber.sleep(WAIT_DELAY)
        end
    end
end

function Server:cleanup()
    for _, pattern in ipairs(DEFAULT_CHECKPOINT_PATTERNS) do
        fio.rmtree(('%s/%s'):format(self.workdir, pattern))
    end
    self.instance_id_value = nil
    self.instance_uuid_value = nil
end

function Server:drop()
    self:stop()
    self:cleanup()
end

-- A copy of test_run:grep_log.
function Server:grep_log(what, bytes, opts)
    local opts = opts or {}
    local noreset = opts.noreset or false
    -- if instance has crashed provide filename to use grep_log
    local filename = opts.filename or self:eval('return box.cfg.log')
    local file = fio.open(filename, {'O_RDONLY', 'O_NONBLOCK'})

    local function fail(msg)
        local err = errno.strerror()
        if file ~= nil then
            file:close()
        end
        error(string.format("%s: %s: %s", msg, filename, err))
    end

    if file == nil then
        fail("Failed to open log file")
    end
    io.flush() -- attempt to flush stdout == log fd
    local filesize = file:seek(0, 'SEEK_END')
    if filesize == nil then
        fail("Failed to get log file size")
    end
    local bytes = bytes or 65536 -- don't read whole log - it can be huge
    bytes = bytes > filesize and filesize or bytes
    if file:seek(-bytes, 'SEEK_END') == nil then
        fail("Failed to seek log file")
    end
    local found, buf
    repeat -- read file in chunks
        local s = file:read(2048)
        if s == nil then
            fail("Failed to read log file")
        end
        local pos = 1
        repeat -- split read string in lines
            local endpos = string.find(s, '\n', pos)
            endpos = endpos and endpos - 1 -- strip terminating \n
            local line = string.sub(s, pos, endpos)
            if endpos == nil and s ~= '' then
                -- line doesn't end with \n or eof, append it to buffer
                -- to be checked on next iteration
                buf = buf or {}
                table.insert(buf, line)
            else
                if buf ~= nil then -- prepend line with buffered data
                    table.insert(buf, line)
                    line = table.concat(buf)
                    buf = nil
                end
                if string.match(line, "Starting instance") and not noreset then
                    found = nil -- server was restarted, reset search
                else
                    found = string.match(line, what) or found
                end
            end
            pos = endpos and endpos + 2 -- jump to char after \n
        until pos == nil
    until s == ''
    file:close()
    return found
end

function Server:assert_follows_upstream(server_id)
    local status = self:exec(function(id)
        return box.info.replication[id].upstream.status
    end, {server_id})
    luatest.assert_equals(status, 'follow',
        ('%s: server does not follow upstream'):format(self.alias))
end

function Server:get_vclock()
    return self:exec(function() return box.info.vclock end)
end

function Server:wait_vclock(to_vclock)
    while true do
        local vclock = self:get_vclock()
        local ok = true

        for server_id, to_lsn in pairs(to_vclock) do
            local lsn = vclock[server_id]
            if lsn == nil or lsn < to_lsn then
                ok = false
                break
            end
        end

        if ok then
            return
        end

        log.info("wait vclock: %s to %s",
            yaml.encode(vclock), yaml.encode(to_vclock))
        fiber.sleep(0.001)
    end
end

return Server
