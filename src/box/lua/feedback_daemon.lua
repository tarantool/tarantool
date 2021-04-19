-- feedback_daemon.lua (internal file)
--
local log   = require('log')
local json  = require('json')
local fiber = require('fiber')
local http  = require('http.client')
local fio = require('fio')

local PREFIX = "feedback_daemon"

local daemon = {
    enabled  = false,
    interval = 0,
    host     = nil,
    fiber    = nil,
    control  = nil,
    guard    = nil,
    shutdown = nil
}

local function get_fiber_id(f)
    local fid = 0
    if f ~= nil and f:status() ~= "dead" then
        fid = f:id()
    end
    return fid
end

local function determine_cgroup_env_impl()
    local fh = fio.open('/proc/1/cgroup', {'O_RDONLY'})
    if not fh then
        return ''
    end

    -- fh:read() doesn't read empty "proc" files
    local big_enough_chunk = 4096
    local s = fh:read(big_enough_chunk)
    fh:close()

    if s:find('docker') then
        return 'docker'
    elseif s:find('lxc') then
        return 'lxc'
    end

    return ''
end

local cached_determine_cgroup_env

local function determine_cgroup_env()
    if cached_determine_cgroup_env == nil then
        cached_determine_cgroup_env = determine_cgroup_env_impl()
    end

    return cached_determine_cgroup_env
end

local function is_system_space(space)
    return box.schema.SYSTEM_ID_MIN <= space.id and
           space.id <= box.schema.SYSTEM_ID_MAX
end

local function jsonpaths_from_idx_parts(idx)
    local paths = {}

    for _, part in pairs(idx.parts) do
        if type(part.path) == 'string' then
            table.insert(paths, part.path)
        end
    end

    return paths
end

local function is_jsonpath_index(idx)
    return #jsonpaths_from_idx_parts(idx) > 0
end

local function is_jp_multikey_index(idx)
    for _, path in pairs(jsonpaths_from_idx_parts(idx)) do
        if path:find('[*]', 1, true) then
            return true
        end
    end

    return false
end

local function is_functional_index(idx)
    return idx.func ~= nil
end

local function is_func_multikey_index(idx)
    if is_functional_index(idx) then
        local fid = idx.func.fid
        local func = fid and box.func[fid]
        return func and func.is_multikey or false
    end

    return false
end

local function fill_in_base_info(feedback)
    if box.info.status ~= "running" then
        return nil, "not running"
    end
    feedback.tarantool_version = box.info.version
    feedback.server_id         = box.info.uuid
    feedback.cluster_id        = box.info.cluster.uuid
    feedback.uptime            = box.info.uptime
end

local function fill_in_platform_info(feedback)
    feedback.os     = jit.os
    feedback.arch   = jit.arch
    feedback.cgroup = determine_cgroup_env()
end

local function fill_in_indices_stats(space, stats)
    for name, idx in pairs(space.index) do
        if type(name) == 'number' then
            local idx_type = idx.type
            if idx_type == 'TREE' then
                if is_functional_index(idx) then
                    stats.functional = stats.functional + 1
                    if is_func_multikey_index(idx) then
                        stats.functional_multikey = stats.functional_multikey + 1
                    end
                elseif is_jsonpath_index(idx) then
                    stats.jsonpath = stats.jsonpath + 1
                    if is_jp_multikey_index(idx) then
                        stats.jsonpath_multikey = stats.jsonpath_multikey + 1
                    end
                end
                stats.tree = stats.tree + 1
            elseif idx_type == 'HASH' then
                stats.hash = stats.hash + 1
            elseif idx_type == 'RTREE' then
                stats.rtree = stats.rtree + 1
            elseif idx_type == 'BITSET' then
                stats.bitset = stats.bitset + 1
            end
        end
    end
end

local function fill_in_schema_stats_impl(schema)
    local spaces = {
        memtx     = 0,
        vinyl     = 0,
        temporary = 0,
        ['local'] = 0,
        sync      = 0,
    }

    local indices = {
        hash                = 0,
        tree                = 0,
        rtree               = 0,
        bitset              = 0,
        jsonpath            = 0,
        jsonpath_multikey   = 0,
        functional          = 0,
        functional_multikey = 0,
    }

    local space_ids = {}
    for name, space in pairs(box.space) do
        local is_system = is_system_space(space)
        if not is_system and type(name) == 'number' then
            table.insert(space_ids, name)
        end
    end

    for _, id in pairs(space_ids) do
        local space = box.space[id]
        if space == nil then
            goto continue;
        end

        if space.engine == 'vinyl' then
            spaces.vinyl = spaces.vinyl + 1
        elseif space.engine == 'memtx' then
            if space.temporary then
                spaces.temporary = spaces.temporary + 1
            end
            spaces.memtx = spaces.memtx + 1
        end
        if space.is_local then
            spaces['local'] = spaces['local'] + 1
        end
        if space.is_sync then
            spaces.sync = spaces.sync + 1
        end
        fill_in_indices_stats(space, indices)

        fiber.yield()
        ::continue::
    end

    for k, v in pairs(spaces) do
        schema[k..'_spaces'] = v
    end

    for k, v in pairs(indices) do
        schema[k..'_indices'] = v
    end
end

local cached_schema_version = 0
local cached_schema_features = {}

local function fill_in_schema_stats(features)
    local schema_version = box.internal.schema_version()
    if cached_schema_version < schema_version then
        local schema = {}
        fill_in_schema_stats_impl(schema)
        cached_schema_version = schema_version
        cached_schema_features = schema
    end
    features.schema = cached_schema_features
end

local function read_first_file(pattern)
    local path = fio.glob(pattern)[1]
    if not path then
        local err = string.format(
            'Tarantool repo not installed: nothing matches "%s"',
            pattern
        )
        return nil, err
    end

    local fh, err = fio.open(path, {'O_RDONLY'})
    if not fh then
        return nil, err
    end

    local s, err = fh:read()
    fh:close()
    if type(s) ~= 'string' then
        return nil, err
    end

    return s
end

local function extract_repo_url_apt()
    local content, err = read_first_file('/etc/apt/sources.list.d/tarantool*.list')
    if not content then
        return nil, err
    end

    return content:match('deb ([^ ]*)')
end

local function extract_repo_url_yum()
    local content, err = read_first_file('/etc/yum.repos.d/tarantool*.repo')
    if not content then
        return nil, err
    end

    return content:match('baseurl=([^\n]*)')
end

local function fill_in_repo_url(feedback)
    if jit.os == 'Linux' then
        feedback.repo_url = extract_repo_url_yum() or extract_repo_url_apt()
    end
end

local function fill_in_features(feedback)
    feedback.features = {}
    fill_in_schema_stats(feedback.features)
    feedback.features.on_reload_configuration_used =
        type(box.on_reload_configuration) == 'function'
end

local function fill_in_options(feedback)
    local options = {}
    options.election_mode = box.cfg.election_mode
    options.replication_synchro_quorum = box.cfg.replication_synchro_quorum
    options.memtx_use_mvcc_engine = box.cfg.memtx_use_mvcc_engine
    feedback.options = options
end

local function fill_in_stats(feedback)
    local stats = {box = {}, net = {}}
    local box_stat = box.stat()
    local net_stat = box.stat.net()

    stats.time = fiber.time64()
    -- Send box.stat().*.total.
    for op, tbl in pairs(box_stat) do
        if type(tbl) == 'table' and tbl.total ~= nil then
            stats.box[op] = {
                total = tbl.total
            }
        end
    end
    -- Send box.stat.net().*.total and box.stat.net().*.current.
    for val, tbl in pairs(net_stat) do
        if type(tbl) == 'table' and
           (tbl.total ~= nil or tbl.current ~= nil) then
            stats.net[val] = {
                total = tbl.total,
                current = tbl.current
            }
        end
    end
    feedback.stats = stats
end

local function fill_in_events(self, feedback)
    feedback.events = self.cached_events
end

local function fill_in_feedback(self, feedback)
    fill_in_base_info(feedback)
    fill_in_platform_info(feedback)
    fill_in_repo_url(feedback)
    fill_in_features(feedback)
    fill_in_options(feedback)
    fill_in_stats(feedback)
    fill_in_events(self, feedback)

    return feedback
end

local function feedback_loop(self)
    fiber.name(PREFIX, { truncate = true })
    -- Speed up the first send.
    local send_timeout = math.min(120, self.interval)

    while true do
        local msg = self.control:get(send_timeout)
        send_timeout = self.interval
        -- if msg == "send" then we simply send feedback
        if msg == "stop" then
            break
        end
        local feedback = self:generate_feedback()
        if feedback ~= nil then
            pcall(http.post, self.host, json.encode(feedback), {timeout=1})
        end
    end
    self.shutdown:put("stopped")
end

local function guard_loop(self)
    fiber.name(string.format("guard of %s", PREFIX), {truncate=true})

    while true do

        if get_fiber_id(self.fiber) == 0 then
            self.fiber = fiber.create(feedback_loop, self)
            log.verbose("%s restarted", PREFIX)
        end
        local st = pcall(fiber.sleep, self.interval)
        if not st then
            -- fiber was cancelled
            break
        end
    end
    self.shutdown:put("stopped")
end

local function save_event(self, event)
    if type(event) ~= 'string' then
        error("Usage: box.internal.feedback_daemon.save_event(string)")
    end
    if type(self.cached_events) ~= 'table' then
        return
    end

    self.cached_events[event] = (self.cached_events[event] or 0) + 1
end

-- these functions are used for test purposes only
local function start(self)
    self:stop()
    if self.enabled then
        -- There may be up to 5 fibers triggering a send during bootstrap or
        -- shortly after it. And maybe more to come. Do not make anyone wait for
        -- feedback daemon to process the incoming events, and set channel size
        -- to 10 just in case.
        self.control = fiber.channel(10)
        self.shutdown = fiber.channel()
        self.cached_events = {}
        self.guard = fiber.create(guard_loop, self)
    end
    log.verbose("%s started", PREFIX)
end

local function stop(self)
    if (get_fiber_id(self.guard) ~= 0) then
        self.guard:cancel()
        self.shutdown:get()
    end
    if (get_fiber_id(self.fiber) ~= 0) then
        self.control:put("stop")
        self.shutdown:get()
    end
    self.guard = nil
    self.fiber = nil
    self.control = nil
    self.shutdown = nil
    log.verbose("%s stopped", PREFIX)
end

local function reload(self)
    self:stop()
    self:start()
end

setmetatable(daemon, {
    __index = {
        set_feedback_params = function()
            box.internal.cfg_set_crash()
            daemon.enabled  = box.cfg.feedback_enabled
            daemon.host     = box.cfg.feedback_host
            daemon.interval = box.cfg.feedback_interval
            reload(daemon)
            return
        end,
        -- this function is used in saving feedback in file
        generate_feedback = function()
            return fill_in_feedback(daemon, { feedback_version = 7 })
        end,
        start = function()
            start(daemon)
        end,
        stop = function()
            stop(daemon)
        end,
        reload = function()
            reload(daemon)
        end,
        save_event = function(event)
            save_event(daemon, event)
        end,
        send = function()
            if daemon.control ~= nil then
                daemon.control:put("send")
            end
        end
    }
})

box.feedback = {}
box.feedback.save = function(file_name)
    if type(file_name) ~= "string" then
        error("Usage: box.feedback.save(path)")
    end
    local feedback = json.encode(daemon.generate_feedback())
    local fh, err = fio.open(file_name, {'O_CREAT', 'O_RDWR', 'O_TRUNC'},
                             tonumber('0777', 8))
    if not fh then
        error(err)
    end
    fh:write(feedback)
    fh:close()
end

if box.internal == nil then
    box.internal = { [PREFIX] = daemon }
else
    box.internal[PREFIX] = daemon
end
