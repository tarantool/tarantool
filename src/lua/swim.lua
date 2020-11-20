local ffi = require('ffi')
local uuid = require('uuid')
local buffer = require('buffer')
local msgpack = require('msgpack')
local crypto = require('crypto')
local fiber = require('fiber')
local internal = require('swim')
local schedule_task = fiber._internal.schedule_task

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

    enum swim_ev_mask {
        SWIM_EV_NEW             = 0b00000001,
        SWIM_EV_NEW_STATUS      = 0b00000010,
        SWIM_EV_NEW_URI         = 0b00000100,
        SWIM_EV_NEW_GENERATION  = 0b00001000,
        SWIM_EV_NEW_VERSION     = 0b00010000,
        SWIM_EV_NEW_INCARNATION = 0b00011000,
        SWIM_EV_NEW_PAYLOAD     = 0b00100000,
        SWIM_EV_UPDATE          = 0b00111110,
        SWIM_EV_DROP            = 0b01000000,
    };

    struct swim_incarnation {
        uint64_t generation;
        uint64_t version;
    };

    bool
    swim_is_configured(const struct swim *swim);

    int
    swim_cfg(struct swim *swim, const char *uri, double heartbeat_rate,
             double ack_timeout, enum swim_gc_mode gc_mode,
             const struct tt_uuid *uuid);

    int
    swim_set_payload(struct swim *swim, const char *payload, int payload_size);

    int
    swim_set_codec(struct swim *swim, enum crypto_algo algo,
                   enum crypto_mode mode, const char *key, int key_size);

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

    struct swim_incarnation
    swim_member_incarnation(const struct swim_member *member);

    const char *
    swim_member_payload(const struct swim_member *member, int *size);

    void
    swim_member_ref(struct swim_member *member);

    void
    swim_member_unref(struct swim_member *member);

    bool
    swim_member_is_dropped(const struct swim_member *member);

    bool
    swim_member_is_payload_up_to_date(const struct swim_member *member);
]]

-- Shortcut to avoid unnecessary lookups in 'ffi' table.
local capi = ffi.C

local swim_t = ffi.typeof('struct swim *')
local swim_member_t = ffi.typeof('struct swim_member *')

local swim_member_status_strs = {
    [capi.MEMBER_ALIVE] = 'alive',
    [capi.MEMBER_SUSPECTED] = 'suspected',
    [capi.MEMBER_DEAD] = 'dead',
    [capi.MEMBER_LEFT] = 'left'
}

local swim_incarnation_mt = {
    __eq = function(l, r)
        return l.version == r.version and l.generation == r.generation
    end,
    __lt = function(l, r)
        return l.generation < r.generation or
               l.generation == r.generation and l.version < r.version
    end,
    __le = function(l, r)
        return l.generation < r.generation or
               l.generation == r.generation and l.version <= r.version
    end,
    __tostring = function(i)
        return string.format('cdata {generation = %s, version = %s}',
                             i.generation, i.version)
    end,
}
ffi.metatype(ffi.typeof('struct swim_incarnation'), swim_incarnation_mt)

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
        if ffi.istype('struct tt_uuid', value) then
            return value
        end
        return error(func_name..': expected string UUID or struct tt_uuid')
    end
    value = uuid.fromstr(value)
    if not value then
        return error(func_name..': invalid UUID')
    end
    return value
end

--
-- Check if @a value is something that can be passed as const
-- char *, as binary data. It should be either string, or cdata.
-- @param value Value to check if can be converted to
--        const char *.
-- @param size Size of @a value in bytes. In case of cdata the
--        argument is mandatory. In case of string @a size is
--        optional and can be <= @a value length.
-- @param func_name Caller function name to include into an error
--        message.
-- @param param_name Parameter name to include into an error
--        message. Examples: 'payload', 'key'.
-- @return Value compatible with const char *, and size in bytes.
--
local function swim_check_const_char(value, size, func_name, param_name)
    if size ~= nil and type(size) ~= 'number' then
        return error(func_name..': expected number '..param_name..' size')
    end
    if type(value) == 'cdata' then
        if not size then
            return error(func_name..': size is mandatory for cdata '..
                         param_name)
        end
        value = ffi.cast('const char *', value)
    elseif type(value) == 'string' then
        if not size then
            size = value:len()
        elseif size > value:len() then
            return error(func_name..': explicit '..param_name..
                         ' size > string length')
        end
    elseif value == nil then
        if size then
            return error(func_name..': size can not be set without '..
                         param_name)
        end
        size = 0
    else
        return error(func_name..': '..param_name..' should be either string '..
                     'or cdata')
    end
    return value, size
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
-- The same for SWIM member.
--
local function swim_check_member(m, func_name)
    if type(m) == 'table' then
        local ptr = m.ptr
        if ffi.istype(swim_member_t, ptr) then
            return ptr
        end
    end
    return error(func_name..': first argument is not a SWIM member')
end

--
-- Member methods. Most of them are one-liners, not much to
-- comment.
--

local function swim_member_status(m)
    local ptr = swim_check_member(m, 'member:status()')
    return swim_member_status_strs[tonumber(capi.swim_member_status(ptr))]
end

local function swim_member_uri(m)
    local ptr = swim_check_member(m, 'member:uri()')
    return ffi.string(capi.swim_member_uri(ptr))
end

local function swim_member_incarnation(m)
    local ptr = swim_check_member(m, 'member:incarnation()')
    return capi.swim_member_incarnation(ptr)
end

local function swim_member_is_dropped(m)
    local ptr = swim_check_member(m, 'member:is_dropped()')
    return capi.swim_member_is_dropped(ptr)
end

local function swim_member_payload_raw(ptr)
    local int = buffer.reg1.ai
    local cdata = capi.swim_member_payload(ptr, int)
    return cdata, int[0]
end

--
-- Payload can be bigger than KB, and probably it is undesirable
-- to copy it into a Lua string or decode MessagePack into a
-- Lua object. This method is the cheapest way of taking payload.
--
local function swim_member_payload_cdata(m)
    local ptr = swim_check_member(m, 'member:payload_cdata()')
    return swim_member_payload_raw(ptr)
end

--
-- Cdata requires to keep explicit size, besides not all user
-- methods can be able to work with cdata. This is why it may be
-- needed to take payload as a string - text or binary.
--
local function swim_member_payload_str(m)
    local ptr = swim_check_member(m, 'member:payload_str()')
    local _, size = swim_member_payload_raw(ptr)
    if size > 0 then
        return ffi.string(swim_member_payload_raw(ptr))
    end
    return nil
end

--
-- Since this is a Lua module, a user is likely to use Lua objects
-- as a payload - tables, numbers, string etc. And it is natural
-- to expect that member:payload() should return the same object
-- which was passed into swim:set_payload() on another instance.
-- This member method tries to interpret payload as MessagePack,
-- and if fails, returns the payload as a string.
--
-- This function caches its result. It means, that only first call
-- actually decodes cdata payload. All the next calls return
-- pointer to the same result, until payload is changed with a new
-- incarnation.
--
local function swim_member_payload(m)
    local ptr = swim_check_member(m, 'member:payload()')
    -- Two keys are needed. Incarnation is not enough, because a
    -- new incarnation can be disseminated earlier than a new
    -- payload. For example, via ACK messages.
    local key1 = capi.swim_member_incarnation(ptr)
    local key2 = capi.swim_member_is_payload_up_to_date(ptr)
    if m.p_key1 and key1 == m.p_key1 and key2 == m.p_key2 then
        return m.p
    end
    local cdata, size = swim_member_payload_raw(ptr)
    local ok, result
    if size == 0 then
        result = nil
    else
        ok, result = pcall(msgpack.decode, cdata, size)
        if not ok then
            result = ffi.string(cdata, size)
        end
    end
    -- Member bans new indexes. Only rawset() can be used.
    rawset(m, 'p', result)
    rawset(m, 'p_key1', key1)
    rawset(m, 'p_key2', key2)
    return result
end

--
-- Cdata UUID. It is ok to return cdata, because struct tt_uuid
-- type has strong support by 'uuid' Lua module with nice
-- metatable, serialization, string conversions etc.
--
local function swim_member_uuid(m)
    local ptr = swim_check_member(m, 'member:uuid()')
    local u = m.u
    if not u then
        ptr = capi.swim_member_uuid(ptr)
        u = ffi.new('struct tt_uuid')
        ffi.copy(u, ptr, ffi.sizeof('struct tt_uuid'))
        rawset(m, 'u', u)
    end
    return u
end

local function swim_member_serialize(m)
    local _, size = swim_member_payload_raw(m.ptr)
    return {
        status = swim_member_status(m),
        uuid = swim_member_uuid(m),
        uri = swim_member_uri(m),
        incarnation = swim_member_incarnation(m),
        -- There are many ways to interpret a payload, and it is
        -- not a job of a serialization method. Only binary size
        -- here is returned to allow a user to detect, whether a
        -- payload exists.
        payload_size = size,
    }
end

local swim_member_mt = {
    __index = {
        status = swim_member_status,
        uuid = swim_member_uuid,
        uri = swim_member_uri,
        incarnation = swim_member_incarnation,
        payload_cdata = swim_member_payload_cdata,
        payload_str = swim_member_payload_str,
        payload = swim_member_payload,
        is_dropped = swim_member_is_dropped,
    },
    __serialize = swim_member_serialize,
    __newindex = function(self)
        return error('swim_member is a read-only object')
    end
}

--
-- Wrap a SWIM member into a table with proper metamethods. The
-- table-wrapper stores not only a pointer, but also cached
-- decoded payload.
--
local function swim_wrap_member(s, ptr)
    -- Lua tables can't normally work with cdata keys. Even when
    -- cdata is a simple number, table can't do search by it.
    local key = tonumber(ffi.cast('unsigned long', ptr))
    local cache = s.cache_table
    local wrapped = cache[key]
    if wrapped == nil then
        capi.swim_member_ref(ptr)
        ffi.gc(ptr, capi.swim_member_unref)
        wrapped = setmetatable({ptr = ptr}, swim_member_mt)
        cache[key] = wrapped
    end
    return wrapped
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
    internal.swim_delete(ffi.gc(ptr, nil))
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
    internal.swim_quit(ffi.gc(ptr, nil))
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
-- Ping a member probably located at @a uri.
--
local function swim_probe_member(s, uri)
    local func_name = 'swim:probe_member'
    local ptr = swim_check_instance(s, func_name)
    uri = swim_check_uri(uri, func_name)
    if capi.swim_probe_member(ptr, uri) ~= 0 then
        return nil, box.error.last()
    end
    return true
end

--
-- Add a new member to the member table explicitly.
--
local function swim_add_member(s, cfg)
    local func_name = 'swim:add_member'
    local ptr = swim_check_instance(s, func_name)
    if type(cfg) ~= 'table' then
        return error(func_name..': expected table member definition')
    end
    local uri = swim_check_uri(cfg.uri, func_name)
    local uuid = swim_check_uuid(cfg.uuid, func_name)
    if capi.swim_add_member(ptr, uri, uuid) ~= 0 then
        return nil, box.error.last()
    end
    return true
end

--
-- Remove a member by @a uuid immediately from the local member
-- table.
--
local function swim_remove_member(s, uuid)
    local func_name = 'swim:remove_member'
    local ptr = swim_check_instance(s, func_name)
    uuid = swim_check_uuid(uuid, func_name)
    if capi.swim_remove_member(ptr, uuid) ~= 0 then
        return nil, box.error.last()
    end
    return true
end

--
-- Broadcast a ping message on all interfaces with @a port
-- destination. Port can be omitted, then currently bound is used.
--
local function swim_broadcast(s, port)
    local func_name = 'swim:broadcast'
    local ptr = swim_check_instance(s, func_name)
    if port == nil then
        port = -1
    else
        if type(port) == 'string' then
            port = tonumber(port)
        end
        if type(port) ~= 'number' then
            return error(func_name..': expected number port')
        end
    end
    if capi.swim_broadcast(ptr, port) ~= 0 then
        return nil, box.error.last()
    end
    return true
end

--
-- Shortcut to get the self member in O(1) not making a lookup
-- into the member table.
--
local function swim_self(s)
    local ptr = swim_check_instance(s, 'swim:self')
    return swim_wrap_member(s, capi.swim_self(ptr))
end

--
-- Find a member by UUID in the local member table.
--
local function swim_member_by_uuid(s, uuid)
    local func_name = 'swim:member_by_uuid'
    local ptr = swim_check_instance(s, func_name)
    uuid = swim_check_uuid(uuid, func_name)
    local m = capi.swim_member_by_uuid(ptr, uuid)
    if m == nil then
        return nil
    end
    return swim_wrap_member(s, m)
end

--
-- Set raw payload without any preprocessing nor encoding. It can
-- be anything, not necessary MessagePack.
--
local function swim_set_payload_raw(s, payload, payload_size)
    local func_name = 'swim:set_payload_raw'
    local ptr = swim_check_instance(s, func_name)
    payload, payload_size =
        swim_check_const_char(payload, payload_size, func_name, 'payload')
    if capi.swim_set_payload(ptr, payload, payload_size) ~= 0 then
        return nil, box.error.last()
    end
    return true
end

--
-- Set Lua object as a payload. It is encoded into MessagePack.
--
local function swim_set_payload(s, payload)
    local func_name = 'swim:set_payload'
    local ptr = swim_check_instance(s, func_name)
    local payload_size = 0
    if payload ~= nil then
        local buf = buffer.IBUF_SHARED
        buf:reset()
        payload_size = msgpack.encode(payload, buf)
        payload = buf.rpos
    end
    if capi.swim_set_payload(ptr, payload, payload_size) ~= 0 then
        return nil, box.error.last()
    end
    return true
end

--
-- Set encryption algorithm, mode, and a private key.
--
local function swim_set_codec(s, cfg)
    local func_name = 'swim:set_codec'
    local ptr = swim_check_instance(s, func_name)
    if type(cfg) ~= 'table' then
        error(func_name..': expected table codec configuration')
    end
    local algo = crypto.cipher_algo[cfg.algo]
    if algo == nil then
        error(func_name..': unknown crypto algorithm')
    end
    local mode = cfg.mode
    if mode == nil then
        mode = crypto.cipher_mode.cbc
    else
        mode = crypto.cipher_mode[mode]
        if mode == nil then
            error(func_name..': unknown crypto algorithm mode')
        end
    end
    local key, key_size =
        swim_check_const_char(cfg.key, cfg.key_size, func_name, 'key')
    if capi.swim_set_codec(ptr, algo, mode, key, key_size) ~= 0 then
        return nil, box.error.last()
    end
    return true
end

--
-- Lua pairs() or similar function should return 3 values:
-- iterator function, iterator object, a key before first. This is
-- iterator function. On each iteration it returns UUID as a key,
-- member object as a value.
--
local function swim_pairs_next(ctx)
    local s = ctx.swim
    if s.ptr == nil then
        return swim_error_deleted()
    end
    local iterator = ctx.iterator
    local m = capi.swim_iterator_next(iterator)
    if m ~= nil then
        m = swim_wrap_member(s, m)
        return m:uuid(), m
    end
    capi.swim_iterator_close(ffi.gc(iterator, nil))
    ctx.iterator = nil
end

--
-- Pairs() to use in 'for' cycles.
--
local function swim_pairs(s)
    local ptr = swim_check_instance(s, 'swim:pairs')
    local iterator = capi.swim_iterator_open(ptr)
    if iterator == nil then
        return nil, box.error.last()
    end
    ffi.gc(iterator, capi.swim_iterator_close)
    return swim_pairs_next, {swim = s, iterator = iterator}, nil
end

local swim_member_event_index = {
    is_new = function(self)
        return bit.band(self[1], capi.SWIM_EV_NEW) ~= 0
    end,
    is_drop = function(self)
        return bit.band(self[1], capi.SWIM_EV_DROP) ~= 0
    end,
    is_update = function(self)
        return bit.band(self[1], capi.SWIM_EV_UPDATE) ~= 0
    end,
    is_new_status = function(self)
        return bit.band(self[1], capi.SWIM_EV_NEW_STATUS) ~= 0
    end,
    is_new_uri = function(self)
        return bit.band(self[1], capi.SWIM_EV_NEW_URI) ~= 0
    end,
    is_new_incarnation = function(self)
        return bit.band(self[1], capi.SWIM_EV_NEW_INCARNATION) ~= 0
    end,
    is_new_generation = function(self)
        return bit.band(self[1], capi.SWIM_EV_NEW_GENERATION) ~= 0
    end,
    is_new_version = function(self)
        return bit.band(self[1], capi.SWIM_EV_NEW_VERSION) ~= 0
    end,
    is_new_payload = function(self)
        return bit.band(self[1], capi.SWIM_EV_NEW_PAYLOAD) ~= 0
    end,
}

local swim_member_event_mt = {
    __index = swim_member_event_index,
    __serialize = function(self)
        local res = {}
        for k, v in pairs(swim_member_event_index) do
            v = v(self)
            if v then
                res[k] = v
            end
        end
        return res
    end,
}

--
-- Create a closure function for preprocessing raw SWIM member
-- event trigger parameters.
-- @param s SWIM instance.
-- @param callback User functions to call.
-- @param ctx An optional parameter for @a callback passed as is.
-- @return A function to set as a trigger.
--
local function swim_on_member_event_new(s, callback, ctx)
    -- Do not keep a hard reference to a SWIM instance. Otherwise
    -- it is a cyclic reference, and both the instance and the
    -- trigger will never be GC-ed.
    s = setmetatable({s}, {__mode = 'v'})
    return function(member_ptr, event_mask)
        local s = s[1]
        if s then
            local m = swim_wrap_member(s, member_ptr)
            local event = setmetatable({event_mask}, swim_member_event_mt)
            return callback(m, event, ctx)
        end
    end
end

--
-- Add or/and delete a trigger on member event. Possible usages:
--
-- * on_member_event(new[, ctx]) - add a new trigger. It should
--   accept 3 arguments: an updated member, an events object, an
--   optional @a ctx parameter passed as is.
--
-- * on_member_event(new, old[, ctx]) - add a new trigger @a new
--   if not nil, in place of @a old trigger.
--
-- * on_member_event() - get a list of triggers.
--
local function swim_on_member_event(s, new, old, ctx)
    local ptr = swim_check_instance(s, 'swim:on_member_event')
    if type(old) ~= 'function' then
        ctx = old
        old = nil
    end
    if new ~= nil then
        new = swim_on_member_event_new(s, new, ctx)
    end
    return internal.swim_on_member_event(ptr, new, old)
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
        probe_member = swim_probe_member,
        add_member = swim_add_member,
        remove_member = swim_remove_member,
        broadcast = swim_broadcast,
        self = swim_self,
        member_by_uuid = swim_member_by_uuid,
        set_payload_raw = swim_set_payload_raw,
        set_payload = swim_set_payload,
        set_codec = swim_set_codec,
        pairs = swim_pairs,
        on_member_event = swim_on_member_event,
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
        set_codec = swim_set_codec,
        on_member_event = swim_on_member_event,
    },
    __serialize = swim_serialize
}

local swim_cfg_not_configured_mt = table.deepcopy(swim_cfg_mt)
swim_cfg_not_configured_mt.__call = swim_cfg_first_call

-- Member cache stores week references so as to do not care about
-- removed members erasure - GC drops them automatically.
local cache_table_mt = { __mode = 'v' }

--
-- SWIM garbage collection function. It can't delete the SWIM
-- instance immediately, because it is invoked by Lua GC. Firstly,
-- it is not safe to yield in FFI - Jit can't survive a yield.
-- Secondly, it is not safe to yield in any GC function, because
-- it stops garbage collection. Instead, here GC is delayed, works
-- at the end of the event loop, and deletes the instance
-- asynchronously.
--
local function swim_gc(ptr)
    schedule_task(internal.swim_delete, ptr)
end

--
-- Create a new SWIM instance, and configure if @a cfg is
-- provided.
--
local function swim_new(cfg)
    local generation
    if cfg and type(cfg) == 'table' and cfg.generation then
        generation = cfg.generation
        if type(generation) ~= 'number' or generation < 0 or
           math.floor(generation) ~= generation then
            return error('swim.new: generation should be non-negative integer')
        end
        cfg = table.copy(cfg)
        -- Nullify in order to do not raise errors in the
        -- following swim:cfg() about unknown parameters.
        cfg.generation = nil
        -- When only 'generation' is specified, empty config will
        -- raise an error at swim:cfg(). It should not.
        if next(cfg) == nil then
            cfg = nil
        end
    else
        generation = fiber.time64()
    end
    local ptr = internal.swim_new(generation)
    if ptr == nil then
        return nil, box.error.last()
    end
    ffi.gc(ptr, swim_gc)
    local s = setmetatable({
        ptr = ptr,
        cfg = setmetatable({index = {}}, swim_cfg_not_configured_mt),
        cache_table = setmetatable({}, cache_table_mt)
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
