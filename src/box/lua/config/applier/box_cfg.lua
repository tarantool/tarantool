local log = require('internal.config.utils.log')

local function peer_uris(configdata)
    local names = configdata:names()

    local peers = configdata:peers()
    if #peers <= 1 then
        return nil
    end

    local uris = {}
    for _, peer_name in ipairs(peers) do
        local iproto = configdata:get('iproto', {peer = peer_name}) or {}
        local uri = iproto.advertise or iproto.listen
        if uri == nil then
            error(('box_cfg.apply: unable to build replicaset %q of group ' ..
                '%q: instance %q has neither iproto.advertise nor ' ..
                'iproto.listen options'):format(names.replicaset_name,
                names.group_name, peer_name), 0)
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

    box_cfg.read_only = not configdata:get('database.rw', {use_default = true})

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
