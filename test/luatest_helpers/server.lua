local fun = require('fun')
local yaml = require('yaml')
local urilib = require('uri')
local fio = require('fio')
local luatest = require('luatest')

-- Join paths in an intuitive way.
--
-- If a component is nil, it is skipped.
--
-- If a component is an absolute path, it skips all the previous
-- components.
--
-- The wrapper is written for two components for simplicity.
local function pathjoin(a, b)
    -- No first path -- skip it.
    if a == nil then
        return b
    end
    -- No second path -- skip it.
    if b == nil then
        return a
    end
    -- The absolute path is checked explicitly due to gh-8816.
    if b:startswith('/') then
        return b
    end
    return fio.pathjoin(a, b)
end

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

    local uris
    if advertise ~= nil then
        uris = {{uri = advertise}}
    else
        uris = listen
    end

    for _, uri in ipairs(uris or {}) do
        uri = table.copy(uri)
        uri.uri = uri.uri:gsub('{{ *instance_name *}}', instance_name)
        uri.uri = uri.uri:gsub('unix/:%./', ('unix/:%s/'):format(dir))
        local u = urilib.parse(uri)
        if u.ipv4 ~= '0.0.0.0' and u.ipv6 ~= '::' and u.service ~= '0' then
            return uri
        end
    end
    error('No suitable URI to connect is found')
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

        self.args = fun.chain(self.args or {}, {
            '--name', self.alias
        }):totable()

        if self.config_file ~= '' then
            table.insert(self.args, '--config')
            table.insert(self.args, self.config_file)

            -- Take into account self.chdir to calculate a config
            -- file path.
            local config_file_path = pathjoin(self.chdir, self.config_file)

            -- Read the provided config file.
            local fh, err = fio.open(config_file_path, {'O_RDONLY'})
            if fh == nil then
                error(('Unable to open file %q: %s'):format(config_file_path,
                    err))
            end
            self.config = yaml.decode(fh:read())
            fh:close()
        end

        if self.net_box_uri == nil then
            local config = self.config or self.remote_config

            -- NB: listen and advertise URIs are relative to
            -- process.work_dir, which, in turn, is relative to
            -- self.chdir.
            local work_dir
            if config.process ~= nil and config.process.work_dir ~= nil then
                work_dir = config.process.work_dir
            end
            local dir = pathjoin(self.chdir, work_dir)
            self.net_box_uri = find_advertise_uri(config, self.alias, dir)
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

-- Eval a string similar to `Server:eval()` and reconnect to the server
-- in case the connection has been dropped.
function Server:eval_reconnect(code, ...)
    if self.net_box == nil then
        error('net_box is not connected', 2)
    end
    local ok, err = pcall(self.net_box.eval, self.net_box, code, ...)
    if not ok and tostring(err):find('Peer closed') then
        self:wait_until_ready()
    elseif not ok then
        error(err)
    end
    return err
end

-- Prepare for iproto_lockdown caused by integrity check failure.
function Server:prepare_integrity_lockdown()
    -- Drop connection to the server ahead of time, before it is dropped
    -- uncontrollingly in test-body. If the iproto_lockdown is already on,
    -- it won't drop connections until turned off and on again.

    self:eval_reconnect("box.ctl.iproto_lockdown(true)")
end

return Server
