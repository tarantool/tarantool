local fun = require('fun')
local yaml = require('yaml')
local urilib = require('uri')
local fio = require('fio')
local luatest = require('luatest')

-- Extract a password of the given user from the given config.
--
-- If the user is 'guest', there is no password (nil is returned)
-- disregarding of the config content. The 'guest' user cannot
-- have a password.
--
-- If the user is not found, an error is raised. This is incorrect
-- configuration.
--
-- If the user is found, but has no plain text password, assume
-- that it is OK to connect without a password.
--
-- If the plain text password is found for the given user, return
-- it.
local function find_password(config, username)
    if username == nil or username == 'guest' then
        return nil
    end

    local err_msg = ('Unable to find user %s to read its password'):format(
        username)
    if config.credentials == nil or config.credentials.users == nil then
        error(err_msg)
    end
    local user_def = config.credentials.users[username]
    if user_def == nil then
        error(err_msg)
    end

    if user_def.password ~= nil then
        return user_def.password.plain
    end
    return nil
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
                advertise = advertise or instance.iproto.advertise
                listen = listen or instance.iproto.listen
            end
            if replicaset.iproto ~= nil then
                advertise = advertise or replicaset.iproto.advertise
                listen = listen or replicaset.iproto.listen
            end
            if group.iproto ~= nil then
                advertise = advertise or group.iproto.advertise
                listen = listen or group.iproto.listen
            end
        end
    end

    if config.iproto ~= nil then
        advertise = advertise or config.iproto.advertise
        listen = listen or config.iproto.listen
    end

    -- The advertise option has one of the following formats:
    --
    -- * user@
    --
    --   Get password from the 'credentials' section.
    --   Get host:port from the  listen option.
    -- * user:pass@
    --
    --   Get host:port from the listen option.
    -- * user:pass@host:port
    --
    --   Just use this URI.
    -- * user@host:port
    --
    --   Get password from the 'credentials' section.
    --
    -- Note: the host:port part may represent a Unix domain
    -- socket: host = 'unix/', port = '/path/to/socket'.
    local login
    local password
    local uri

    if advertise ~= nil and advertise:endswith('@') then
        if advertise:find(':') then
            -- user:pass@ -> set login and password
            local login_password = advertise:sub(1, -2):split(':', 1)
            login = login_password[1]
            password = login_password[2]
        else
            -- user@ -> set login
            --
            -- The password is set in the code below.
            login = advertise:sub(1, -2)
        end
        -- Use listen as the host:port part.
        --
        -- The listen option can contain several URIs, it is
        -- handled below.
        uri = listen
    else
        -- The advertise parameter is either nil or contains
        -- an URI.
        uri = advertise or listen
    end

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
            -- Assume that if a login or a password is part of the
            -- URI, it comes from the advertise option.
            -- box.cfg({listen = <...>}) accepts an URI with user
            -- or user:pass, but it has no sense.
            --
            -- So, prefer login/password from the URI if present.
            u.login = u.login or login
            u.password = u.password or password or
                find_password(config, u.login)
            return urilib.format(u, true)
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
            expr = "return require('config'):info().status == 'ready'"
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
