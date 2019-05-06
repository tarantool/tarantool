local ffi = require('ffi')
local uuid = require('uuid')

ffi.cdef[[
    struct swim;
    struct tt_uuid;
    struct swim_iterator;
    struct swim_member;

    enum swim_gc_mode {
        SWIM_GC_DEFAULT = -1,
        SWIM_GC_OFF = 0,
        SWIM_GC_ON = 1,
    };

    enum swim_member_status {
        MEMBER_ALIVE = 0,
        MEMBER_SUSPECTED,
        MEMBER_DEAD,
        MEMBER_LEFT,
    };

    struct swim *
    swim_new(void);

    bool
    swim_is_configured(const struct swim *swim);

    int
    swim_cfg(struct swim *swim, const char *uri, double heartbeat_rate,
             double ack_timeout, enum swim_gc_mode gc_mode,
             const struct tt_uuid *uuid);

    int
    swim_set_payload(struct swim *swim, const char *payload, int payload_size);

    void
    swim_delete(struct swim *swim);

    int
    swim_add_member(struct swim *swim, const char *uri,
                    const struct tt_uuid *uuid);

    int
    swim_remove_member(struct swim *swim, const struct tt_uuid *uuid);

    int
    swim_probe_member(struct swim *swim, const char *uri);

    int
    swim_broadcast(struct swim *swim, int port);

    int
    swim_size(const struct swim *swim);

    void
    swim_quit(struct swim *swim);

    struct swim_member *
    swim_self(struct swim *swim);

    struct swim_member *
    swim_member_by_uuid(struct swim *swim, const struct tt_uuid *uuid);

    enum swim_member_status
    swim_member_status(const struct swim_member *member);

    struct swim_iterator *
    swim_iterator_open(struct swim *swim);

    struct swim_member *
    swim_iterator_next(struct swim_iterator *iterator);

    void
    swim_iterator_close(struct swim_iterator *iterator);

    const char *
    swim_member_uri(const struct swim_member *member);

    const struct tt_uuid *
    swim_member_uuid(const struct swim_member *member);

    uint64_t
    swim_member_incarnation(const struct swim_member *member);

    const char *
    swim_member_payload(const struct swim_member *member, int *size);

    void
    swim_member_ref(struct swim_member *member);

    void
    swim_member_unref(struct swim_member *member);

    bool
    swim_member_is_dropped(const struct swim_member *member);
]]

-- Shortcut to avoid unnecessary lookups in 'ffi' table.
local capi = ffi.C

local swim_t = ffi.typeof('struct swim *')

--
-- Check if @a value is something that can be passed as a
-- URI parameter. Note, it does not validate URI, because it is
-- done in C module. Here only dynamic typing errors are checked.
-- Throws on invalid type.
--
-- @param value Value to check and probably convert.
-- @param func_name Caller function name to include into an error
--        message.
-- @return String that can be passed as a URI parameter.
--
local function swim_check_uri(value, func_name)
    if value == nil then
        return nil
    end
    if type(value) == 'string' then
        return value
    end
    if type(value) == 'number' then
        return tostring(value)
    end
    return error(func_name..': expected string URI or port number')
end

--
-- Check if @a value is a number, that can be passed as a timeout.
-- Throws on invalid type.
--
-- @param value Value to check.
-- @param func_name Caller function name to include into an error
--        message.
-- @param param_name Timeout parameter name to include into an
--        error message. Examples: 'heartbeat_rate',
--        'ack_timeout'.
-- @return Timeout value. Can be negative - SWIM treats negative
--         value as an instruction to keep an old timeout.
--
local function swim_check_timeout(value, func_name, param_name)
    if value == nil then
        return -1
    end
    if type(value) ~= 'number' then
        return error(func_name..': expected number '..param_name)
    end
    return value
end

--
-- Check if @a value is a valid garbage collection mode.
-- Throws on invalid type and unknown mode.
--
-- @param value Value to check and convert.
-- @param func_name Caller function name to include into an error
--        message.
-- @return GC mode cdata enum value.
--
local function swim_check_gc_mode(value, func_name)
    if value == nil then
        return capi.SWIM_GC_DEFAULT
    end
    if value == 'on' then
        return capi.SWIM_GC_ON
    elseif value == 'off' then
        return capi.SWIM_GC_OFF
    else
        return error(func_name..': unknown gc_mode')
    end
end

--
-- Check if @a value is a valid UUID. Throws on invalid type and
-- UUID.
--
-- @param value Value to check.
-- @param func_name Caller function name to include into an error
--        message.
-- @return Struct UUID cdata.
--
local function swim_check_uuid(value, func_name)
    if value == nil then
        return nil
    end
    if type(value) ~= 'string' then
        return error(func_name..': expected string UUID')
    end
    value = uuid.fromstr(value)
    if not value then
        return error(func_name..': invalid UUID')
    end
    return value
end

--
-- Check if @a s is a SWIM instance. It should be a table with
-- cdata struct swim in 'ptr' attribute. Throws on invalid type.
--
-- @param value Value to check.
-- @param func_name Caller function name to include into an error
--        message.
-- @return Pointer to struct swim.
--
local function swim_check_instance(s, func_name)
    if type(s) == 'table' then
        local ptr = s.ptr
        if ffi.istype(swim_t, ptr) then
            return ptr
        end
    end
    return error(func_name..': first argument is not a SWIM instance')
end

--
-- When a SWIM instance is deleted or has quited, it can't be used
-- anymore. This function replaces all methods of a deleted
-- instance to throw an error on a usage attempt.
--
local function swim_error_deleted()
    return error('the swim instance is deleted')
end
-- This is done without 'if' in the original methods, but rather
-- via metatable replacement after deletion has happened.
local swim_mt_deleted = {
    __index = swim_error_deleted
}

--
-- Delete a SWIM instance immediately, do not notify cluster
-- members about that.
--
local function swim_delete(s)
    local ptr = swim_check_instance(s, 'swim:delete')
    capi.swim_delete(ffi.gc(ptr, nil))
    s.ptr = nil
    setmetatable(s, swim_mt_deleted)
end

--
-- Quit from a cluster gracefully, notify other members. The SWIM
-- instance is considered deleted immediately after this function
-- returned, and can't be used anymore.
--
local function swim_quit(s)
    local ptr = swim_check_instance(s, 'swim:quit')
    capi.swim_quit(ffi.gc(ptr, nil))
    s.ptr = nil
    setmetatable(s, swim_mt_deleted)
end

--
-- Size of the local member table.
--
local function swim_size(s)
    return capi.swim_size(swim_check_instance(s, 'swim:size'))
end

--
-- Check if a SWIM instance is configured already. Not configured
-- instance does not provide any methods.
--
local function swim_is_configured(s)
    return capi.swim_is_configured(swim_check_instance(s, 'swim:is_configured'))
end

--
-- Configuration options are printed when a SWIM instance is
-- serialized, for example, in a console.
--
local function swim_serialize(s)
    return s.cfg.index
end

--
-- Normal metatable of a configured SWIM instance.
--
local swim_mt = {
    __index = {
        delete = swim_delete,
        quit = swim_quit,
        size = swim_size,
        is_configured = swim_is_configured,
    },
    __serialize = swim_serialize
}

local swim_cfg_options = {
    uri = true, heartbeat_rate = true, ack_timeout = true,
    gc_mode = true, uuid = true
}

--
-- SWIM 'cfg' attribute is not a trivial table nor function. It is
-- a callable table. It allows to strongly restrict use cases of
-- swim.cfg down to 2 applications:
--
-- - Cfg-table. 'swim.cfg' is a read-only table of configured
--   parameters. A user can't write 'swim.cfg.<key> = value' - an
--   error is thrown. But it can be indexed like
--   'opt = swim.cfg.<key>'. Configuration is cached in Lua, so
--   the latter example won't even call SWIM C API functions.
--
-- - Cfg-function. 'swim:cfg(<new config>)' reconfigures a SWIM
--   instance and returns normal values: nil+error or true. The
--   new configuration is cached for further indexing.
--
-- All the other combinations are banned. The function below
-- implements configuration call.
--
local function swim_cfg_call(c, s, cfg)
    local func_name = 'swim:cfg'
    local ptr = swim_check_instance(s, func_name)
    if type(cfg) ~= 'table' then
        return error(func_name..': expected table configuration')
    end
    for k in pairs(cfg) do
        if not swim_cfg_options[k] then
            return error(func_name..': unknown option '..k)
        end
    end
    local uri = swim_check_uri(cfg.uri, func_name)
    local heartbeat_rate = swim_check_timeout(cfg.heartbeat_rate,
                                              func_name, 'heartbeat_rate')
    local ack_timeout = swim_check_timeout(cfg.ack_timeout, func_name,
                                           'ack_timeout');
    local gc_mode = swim_check_gc_mode(cfg.gc_mode, func_name)
    local uuid = swim_check_uuid(cfg.uuid, func_name)
    if capi.swim_cfg(ptr, uri, heartbeat_rate, ack_timeout,
                     gc_mode, uuid) ~= 0 then
        return nil, box.error.last()
    end
    local index = c.index
    for k, v in pairs(cfg) do
        index[k] = v
    end
    return true
end

local swim_cfg_mt = {
    __call = swim_cfg_call,
    __index = function(c, k)
        return c.index[k]
    end,
    __serialize = function(c)
        return c.index
    end,
    __newindex = function()
        return error('please, use swim:cfg{key = value} instead of '..
                     'swim.cfg.key = value')
    end
}

--
-- First 'swim:cfg()' call is different from others. On success it
-- replaces swim metatable with a full one, where all the variety
-- of methods is available.
--
local function swim_cfg_first_call(c, s, cfg)
    local ok, err = swim_cfg_call(c, s, cfg)
    if not ok then
        return ok, err
    end
    -- Update 'cfg' metatable as well to never call this
    -- function again and use ordinary swim_cfg_call() directly.
    setmetatable(c, swim_cfg_mt)
    setmetatable(s, swim_mt)
    return ok
end

local swim_not_configured_mt = {
    __index = {
        delete = swim_delete,
        is_configured = swim_is_configured,
    },
    __serialize = swim_serialize
}

local swim_cfg_not_configured_mt = table.deepcopy(swim_cfg_mt)
swim_cfg_not_configured_mt.__call = swim_cfg_first_call

--
-- Create a new SWIM instance, and configure if @a cfg is
-- provided.
--
local function swim_new(cfg)
    local ptr = capi.swim_new()
    if ptr == nil then
        return nil, box.error.last()
    end
    ffi.gc(ptr, capi.swim_delete)
    local s = setmetatable({
        ptr = ptr,
        cfg = setmetatable({index = {}}, swim_cfg_not_configured_mt)
    }, swim_not_configured_mt)
    if cfg then
        local ok, err = s:cfg(cfg)
        if not ok then
            s:delete()
            return ok, err
        end
    end
    return s
end

return {
    new = swim_new,
}
