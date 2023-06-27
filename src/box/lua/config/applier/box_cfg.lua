local urilib = require('uri')
local log = require('internal.config.utils.log')
local instance_config = require('internal.config.instance_config')

-- Accept a comma separated list of URIs and return the first one
-- that is suitable to create a client socket (not just to listen
-- on the server as, say, 0.0.0.0:3301 or localhost:0).
--
-- See the uri_is_suitable_to_connect() method in the instance
-- schema object for details.
local function find_suitable_uri_to_connect(uris)
    for _, u in ipairs(urilib.parse_many(uris)) do
        if instance_config:uri_is_suitable_to_connect(u) then
            -- The urilib.format() call has the second optional
            -- argument `write_password`. Let's assume that the
            -- given URIs are to listen on them and so have no
            -- user/password.
            return urilib.format(u)
        end
    end
    return nil
end

local function find_password(configdata, username)
    -- The guest user can't have a password.
    if username == 'guest' then
        return nil
    end

    -- Find a user definition in the config.
    local user_def = configdata:get('credentials.users.' .. username,
        {use_default = true})
    if user_def == nil then
        error(('box_cfg.apply: cannot find user %s in the config to use its ' ..
            'password in a replication peer URI'):format(username), 0)
    end

    -- There is a user definition without a password. Let's assume
    -- that the user has no password.
    if user_def.password ~= nil then
        return user_def.password.plain
    end
    return nil
end

local function peer_uri(configdata, peer_name)
    local names = configdata:names()
    local err_msg_prefix = ('box_cfg.apply: unable to build replicaset %q ' ..
        'of group %q'):format(names.replicaset_name, names.group_name)

    local opts = {peer = peer_name, use_default = true}
    local listen = configdata:get('iproto.listen', opts)
    local advertise = configdata:get('iproto.advertise.peer', opts)

    if advertise ~= nil and not advertise:endswith('@') then
        -- The iproto.advertise.peer option contains an URI.
        --
        -- There are the following cases.
        --
        -- * host:port
        -- * user@host:port
        -- * user:pass@host:port
        --
        -- Note: the host:port part may represent a Unix domain
        -- socket: host = 'unix/', port = '/path/to/socket'.
        --
        -- The second case needs additional handling: we should
        -- find password for the given user in the 'credential'
        -- section of the config.
        --
        -- Otherwise, the URI is returned as is.
        local u, err = urilib.parse(advertise)
        -- NB: The URI is validated, so the parsing can't fail.
        assert(u ~= nil, err)
        if u.login ~= nil and u.password == nil then
            u.password = find_password(configdata, u.login)
            return urilib.format(u, true)
        end
        return advertise
    elseif listen ~= nil then
        -- The iproto.advertise.peer option has no URI.
        --
        -- There are the following cases.
        --
        -- * <no iproto.advertise.peer>
        -- * user@
        -- * user:pass@
        --
        -- In any case we should find an URI suitable to create a
        -- client socket in iproto.listen option. After this, add
        -- the auth information if any.
        local uri = find_suitable_uri_to_connect(listen)
        if uri == nil then
            error(('%s: instance %q has no iproto.advertise.peer or ' ..
                'iproto.listen URI suitable to create a client socket'):format(
                err_msg_prefix, peer_name), 0)
        end

        -- No additional auth information in iproto.advertise.peer:
        -- return the listen URI as is.
        if advertise == nil then
            return uri
        end

        -- Extract user and password from the iproto.advertise
        -- option. If no password given, find it in the
        -- 'credentials' section of the config.
        assert(advertise:endswith('@'))
        local auth = advertise:sub(1, -2):split(':', 1)
        local username = auth[1]
        local password = auth[2] or find_password(configdata, username)

        -- Rebuild the listen URI with the given username and
        -- password,
        local u, err = urilib.parse(uri)
        -- NB: The URI is validated, so the parsing can't fail.
        assert(u ~= nil, err)
        u.login = username
        u.password = password
        return urilib.format(u, true)
    end

    return nil
end

local function peer_uris(configdata)
    local peers = configdata:peers()
    if #peers <= 1 then
        return nil
    end

    local names = configdata:names()
    local err_msg_prefix = ('box_cfg.apply: unable to build replicaset %q ' ..
        'of group %q'):format(names.replicaset_name, names.group_name)

    local uris = {}
    for _, peer_name in ipairs(peers) do
        local uri = peer_uri(configdata, peer_name)
        if uri == nil then
            error(('%s: instance %q has neither iproto.advertise nor ' ..
                'iproto.listen options'):format(err_msg_prefix, peer_name), 0)
        end
        table.insert(uris, uri)
    end
    return uris
end

local function log_destination(configdata)
    local log = configdata:get('log', {use_default = true})
    if log.to == 'stderr' then
        return box.NULL
    elseif log.to == 'file' then
        return ('file:%s'):format(log.file)
    elseif log.to == 'pipe' then
        return ('pipe:%s'):format(log.pipe)
    elseif log.to == 'syslog' then
        local res = ('syslog:identity=%s,facility=%s'):format(
            log.syslog.identity,
            log.syslog.facility)
        -- TODO: Syslog's URI format is different from tarantool's
        -- one for Unix domain sockets: `unix:/path/to/socket` vs
        -- `unix/:/path/to/socket`. We expect syslog's format
        -- here, but maybe it worth to accept our own one (or even
        -- just path) and transform under hood.
        if log.syslog.server ~= nil then
            res = res .. (',server=%s'):format(log.syslog.server)
        end
        return res
    else
        assert(false)
    end
end

local function apply(config)
    local configdata = config._configdata
    local box_cfg = configdata:filter(function(w)
        return w.schema.box_cfg ~= nil
    end, {use_default = true}):map(function(w)
        return w.schema.box_cfg, w.data
    end):tomap()

    -- Construct box_cfg.replication.
    if box_cfg.replication == nil then
        box_cfg.replication = peer_uris(configdata)
    end

    -- Construct logger destination (box_cfg.log) and log modules.
    --
    -- `log.nonblock`, `log.level`, `log.format`, 'log.modules'
    -- options are marked with the `box_cfg` annotations and so
    -- they're already added to `box_cfg`.
    box_cfg.log = log_destination(configdata)

    -- Read-only or read-write?
    --
    -- 'rw' and 'ro' mean itself.
    --
    -- The default is determined depending of amount of instances
    -- in the given replicaset.
    --
    -- * 1 instance: read-write.
    -- * >1 instances: read-only.
    local mode = configdata:get('database.mode', {use_default = true})
    if mode == 'ro' then
        box_cfg.read_only = true
    elseif mode == 'rw' then
        box_cfg.read_only = false
    elseif #configdata:peers() == 1 then
        assert(mode == nil)
        box_cfg.read_only = false
    elseif #configdata:peers() > 1 then
        assert(mode == nil)
        box_cfg.read_only = true
    else
        assert(false)
    end

    assert(type(box.cfg) == 'function' or type(box.cfg) == 'table')
    if type(box.cfg) == 'table' then
        local box_cfg_nondynamic = configdata:filter(function(w)
            return w.schema.box_cfg ~= nil and w.schema.box_cfg_nondynamic
        end, {use_default = true}):map(function(w)
            return w.schema.box_cfg, w.data
        end):tomap()
        box_cfg_nondynamic.log = box_cfg.log
        for k, v in pairs(box_cfg_nondynamic) do
            if v ~= box.cfg[k] then
                local warning = 'box_cfg.apply: non-dynamic option '..k..
                    ' will not be set until the instance is restarted'
                config:_alert({type = 'warn', message = warning})
                box_cfg[k] = nil
            end
        end
    end

    -- Add instance, replicaset and group (cluster) names.
    local names = configdata:names()
    box_cfg.cluster_name = names.group_name
    box_cfg.replicaset_name = names.replicaset_name
    box_cfg.instance_name = names.instance_name

    log.debug('box_cfg.apply: %s', box_cfg)

    box.cfg(box_cfg)
end

return {
    name = 'box_cfg',
    apply = apply,
}
