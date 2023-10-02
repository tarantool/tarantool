local fun = require('fun')
local yaml = require('yaml')
local urilib = require('uri')
local fio = require('fio')
local luatest = require('luatest')

-- Determine advertise URI for given instance from a cluster
-- configuration.
local function find_advertise_uri(config, instance_name, dir)
    if config == nil or next(config) == nil then
        return nil
    end

    -- Determine listen and advertise options that are in effect
    -- for the given instance.
    local advertise
    local listen

    for _, group in pairs(config.groups or {}) do
        for _, replicaset in pairs(group.replicasets or {}) do
            local instance = (replicaset.instances or {})[instance_name]
            if instance == nil then
                break
            end
            if instance.iproto ~= nil then
                if instance.iproto.advertise ~= nil then
                    advertise = advertise or instance.iproto.advertise.client
                end
                listen = listen or instance.iproto.listen
            end
            if replicaset.iproto ~= nil then
                if replicaset.iproto.advertise ~= nil then
                    advertise = advertise or replicaset.iproto.advertise.client
                end
                listen = listen or replicaset.iproto.listen
            end
            if group.iproto ~= nil then
                if group.iproto.advertise ~= nil then
                    advertise = advertise or group.iproto.advertise.client
                end
                listen = listen or group.iproto.listen
            end
        end
    end

    if config.iproto ~= nil then
        if config.iproto.advertise ~= nil then
            advertise = advertise or config.iproto.advertise.client
        end
        listen = listen or config.iproto.listen
    end

    local uri = advertise or listen

    -- Neither advertise, nor listen contain an URI.
    if uri == nil then
        return nil
    end

    uri = uri:gsub('{{ *instance_name *}}', instance_name)

    if dir ~= nil then
        uri = uri:gsub('unix/:%./', ('unix/:%s/'):format(dir))
    end

    -- The listen option can contain several URIs. Let's find
    -- first URI suitable to create a client socket.
    local uris, err = urilib.parse_many(uri)
    if uris == nil then
        error(err)
    end
    for _, u in ipairs(uris) do
        local suitable = u.ipv4 ~= '0.0.0.0' and u.ipv6 ~= '::' and
            u.service ~= '0'
        if suitable then
            return urilib.format(u)
        end
    end

    error(('No suitable URIs to connect found in %s'):format(uri))
end

local Server = luatest.Server:inherit({})

-- Adds the following options:
--
-- * config_file (string)
--
--   An argument of the `--config <...>` CLI option.
--
--   Used to deduce advertise URI to connect net.box to the
--   instance.
--
--   The special value '' means running without `--config <...>`
--   CLI option (but still pass `--name <alias>`).
-- * remote_config (table)
--
--   If `config_file` is not passed, this config value is used to
--   deduce the advertise URI to connect net.box to the instance.
Server.constructor_checks = fun.chain(Server.constructor_checks, {
    config_file = 'string',
    remote_config = '?table',
}):tomap()

function Server:initialize()
    if self.config_file ~= nil then
        self.command = arg[-1]
        self.args = {'--name', self.alias}

        if self.config_file ~= '' then
            table.insert(self.args, '--config')
            table.insert(self.args, self.config_file)

            local fh = fio.open(self.config_file, {'O_RDONLY'})
            self.config = yaml.decode(fh:read())
            fh:close()
        end

        if self.net_box_uri == nil then
            local config = self.config or self.remote_config
            self.net_box_uri = find_advertise_uri(config, self.alias,
                self.chdir)
        end
    end
    getmetatable(getmetatable(self)).initialize(self)
end

function Server:connect_net_box()
    getmetatable(getmetatable(self)).connect_net_box(self)

    if self.config_file == nil then
        return
    end

    if not self.net_box then
        return
    end

    -- Replace the ready condition.
    local saved_eval = self.net_box.eval
    self.net_box.eval = function(self, expr, args, opts)
        if expr == 'return _G.ready' then
            expr = "return require('config'):info().status == 'ready' or " ..
                          "require('config'):info().status == 'check_warnings'"
        end
        return saved_eval(self, expr, args, opts)
    end
end

-- Enable the startup waiting if the advertise URI of the instance
-- is determined.
function Server:start(opts)
    opts = opts or {}
    if self.config_file and opts.wait_until_ready == nil then
        opts.wait_until_ready = self.net_box_uri ~= nil
    end
    getmetatable(getmetatable(self)).start(self, opts)
end

return Server
