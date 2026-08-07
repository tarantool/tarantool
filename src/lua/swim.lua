local ffi = require('ffi')
local uuid = require('uuid')
local buffer = require('buffer')
local msgpack = require('msgpack')
local crypto = require('crypto')
local fiber = require('fiber')
local internal = require('swim.lib')
local cord_ibuf_take = buffer.internal.cord_ibuf_take
local cord_ibuf_put = buffer.internal.cord_ibuf_put

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

---@alias swim.gc_mode
---| 'on' # dead members are removed from the member table after one round
---| 'off' # dead members are never removed automatically

---@alias swim.status
---| 'alive'
---| 'suspected'
---| 'dead'
---| 'left'

---@alias swim.event_handler fun(member: swim.member,
---    event: swim.event, ctx?: any): any?

---@class swim.cfg
---@field heartbeat_rate? number rate of sending round messages, in seconds.
---    Default = 1.
---@field ack_timeout? number time in seconds after which a ping is
---    considered to be unacknowledged. Default = 30.
---@field gc_mode? swim.gc_mode dead member collection mode. Default = 'on'.
---@field uri? string|integer an 'ip:port' address, or just a port number.
---@field uuid? string|ffi.cdata* a value which should be unique among SWIM
---    instances.

---@class swim.new_cfg: swim.cfg
---@field generation? integer part of the incarnation. It can only be set
---    during new(), it cannot be specified later during cfg().

---@class swim.member_cfg
---@field uri string|integer the member URI: an 'ip:port' address or a port
---    number.
---@field uuid string|ffi.cdata* the member UUID.

---@class swim.codec
---@field algo string encryption algorithm name. All the names in module
---    crypto are supported: 'aes128', 'aes192', 'aes256', 'des'. Specify
---    'none' to disable encryption.
---@field mode? string encryption algorithm mode. All the modes in module
---    crypto are supported: 'ecb', 'cbc', 'cfb', 'ofb'. Default = 'cbc'.
---@field key string|ffi.cdata* a private secret key which is kept secret
---    and should never be stored hard-coded in source code.
---@field key_size? integer size of the key in bytes. Mandatory if key is
---    cdata.

---@class swim.incarnation: ffi.cdata*
---@field generation integer the persistent part of the incarnation.
---@field version integer the volatile part of the incarnation.

---@class swim.object
---@field cfg swim.cfg read-only table of the configured parameters;
---    callable as `swim_object:cfg(cfg)` to reconfigure.
---@field delete fun(self: swim.object) Delete a SWIM instance immediately.
---@field quit fun(self: swim.object) Leave the cluster gracefully.
---@field is_configured fun(self: swim.object): boolean Return true if the
---    SWIM instance is configured.
---@field size fun(self: swim.object): integer Return the size of the member
---    table.
---@field add_member fun(self: swim.object,
---    cfg: swim.member_cfg): boolean?, any? Add a member into the member
---    table.
---@field remove_member fun(self: swim.object,
---    uuid: string|ffi.cdata*): boolean?, any? Remove a member from the
---    member table.
---@field probe_member fun(self: swim.object,
---    uri: string|integer): boolean?, any? Send a ping request to the
---    specified URI.
---@field broadcast fun(self: swim.object,
---    port?: integer): boolean?, any? Broadcast a ping request to all
---    network interfaces.
---@field set_payload fun(self: swim.object,
---    payload: any): boolean?, any? Set a payload as formatted data.
---@field set_payload_raw fun(self: swim.object,
---    payload: string|ffi.cdata*, payload_size?: integer): boolean?, any? Set a
---    payload as raw data.
---@field set_codec fun(self: swim.object,
---    cfg: swim.codec): boolean?, any? Enable encryption for all following
---    messages.
---@field self fun(self: swim.object): swim.member Return the member
---    object of this SWIM instance.
---@field member_by_uuid fun(self: swim.object,
---    uuid: string|ffi.cdata*): swim.member? Return a member object by
---    UUID.
---@field pairs fun(self: swim.object):
---    fun(ctx: table, key: any): (string, swim.member)?, table?, nil Set up
---    an iterator over the member table.
---@field on_member_event fun(self: swim.object, ...): function? Set,
---    replace or delete an on-member event handler.

---@class swim.member
---@field status fun(self: swim.member): swim.status Return the member
---    status.
---@field uuid fun(self: swim.member): ffi.cdata* Return the member UUID.
---@field uri fun(self: swim.member): string Return the member URI.
---@field incarnation fun(self: swim.member): swim.incarnation Return the
---    member incarnation.
---@field payload_cdata fun(self: swim.member): ffi.cdata*, integer Return
---    the payload as cdata and its size.
---@field payload_str fun(self: swim.member): string? Return the payload as
---    a string.
---@field payload fun(self: swim.member): any Return the payload as a Lua
---    object.
---@field is_dropped fun(self: swim.member): boolean Return true if the
---    member is dropped.

---@class swim.event
---@field is_new fun(self: swim.event): boolean Return true if the member
---    is new.
---@field is_drop fun(self: swim.event): boolean Return true if the member
---    is dropped.
---@field is_update fun(self: swim.event): boolean Return true if the
---    member is updated.
---@field is_new_status fun(self: swim.event): boolean Return true if the
---    member status is new.
---@field is_new_uri fun(self: swim.event): boolean Return true if the
---    member URI is new.
---@field is_new_incarnation fun(self: swim.event): boolean Return true if
---    the member incarnation is new.
---@field is_new_generation fun(self: swim.event): boolean Return true if
---    the generation part of the incarnation is new.
---@field is_new_version fun(self: swim.event): boolean Return true if the
---    version part of the incarnation is new.
---@field is_new_payload fun(self: swim.event): boolean Return true if the
---    member payload is new.

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

local function swim_check_event(event, func_name)
    if type(event) == 'table' then
        local value = event[1]
        if type(value) == 'number' then
            return value
        end
    end
    return error(func_name..': first argument is not a SWIM event')
end

--
-- Member methods. Most of them are one-liners, not much to
-- comment.
--

---Return the status, which may be 'alive', 'suspected', 'left', or 'dead'.
---
---@param m swim.member
---@return swim.status status
local function swim_member_status(m)
    local ptr = swim_check_member(m, 'member:status()')
    return swim_member_status_strs[tonumber(capi.swim_member_status(ptr))]
end

---Return the URI as a string 'ip:port'.
---
---Via this method a user can learn a real assigned port, if port = 0 was
---specified in `swim_object:cfg()`.
---
---@param m swim.member
---@return string uri
local function swim_member_uri(m)
    local ptr = swim_check_member(m, 'member:uri()')
    return ffi.string(capi.swim_member_uri(ptr))
end

---Return a cdata object with the incarnation.
---
---The cdata object has two attributes: incarnation().generation and
---incarnation().version. Incarnations can be compared to each other with
---any comparison operator (==, <, >, <=, >=, ~=). Incarnations, when
---printed, will appear as strings with both generation and version.
---
---@param m swim.member
---@return swim.incarnation incarnation
local function swim_member_incarnation(m)
    local ptr = swim_check_member(m, 'member:incarnation()')
    return capi.swim_member_incarnation(ptr)
end

---Returns true if this member object is a stray reference to a member which
---has already been dropped from the member table.
---
---@param m swim.member
---@return boolean result true if member is dropped, otherwise false
local function swim_member_is_dropped(m)
    local ptr = swim_check_member(m, 'member:is_dropped()')
    return capi.swim_member_is_dropped(ptr)
end

local function swim_member_payload_raw(ptr)
    local int = ffi.new('int[1]')
    local cdata = capi.swim_member_payload(ptr, int)
    return cdata, int[0]
end

---Return member's payload.
---
---Payload can be bigger than KB, and probably it is undesirable to copy it
---into a Lua string or decode MessagePack into a Lua object. This method is
---the cheapest way of taking payload.
---
---@param m swim.member
---@return ffi.cdata* payload
---@return integer size payload size in bytes
local function swim_member_payload_cdata(m)
    local ptr = swim_check_member(m, 'member:payload_cdata()')
    local payload, size = swim_member_payload_raw(ptr)
    return payload, size
end

---Return payload as a string object.
---
---Payload is not decoded. It is just returned as a string instead of cdata.
---If payload was not specified by `swim_object:set_payload()` or by
---`swim_object:set_payload_raw()`, then its size is 0 and nil is returned.
---
---Cdata requires to keep explicit size, besides not all user methods can be
---able to work with cdata. This is why it may be needed to take payload as
---a string - text or binary.
---
---@param m swim.member
---@return string? payload
local function swim_member_payload_str(m)
    local ptr = swim_check_member(m, 'member:payload_str()')
    local _, size = swim_member_payload_raw(ptr)
    if size > 0 then
        return ffi.string(swim_member_payload_raw(ptr))
    end
    return nil
end

---Return member's payload, trying to decode it from MessagePack.
---
---Since the `swim` module is a Lua module, a user is likely to use Lua
---objects as a payload - tables, numbers, strings etc. And it is natural to
---expect that `swim_member_object:payload()` should return the same object
---which was passed into `swim_object:set_payload()` by another instance.
---`swim_member_object:payload()` tries to interpret payload as MessagePack,
---and if that fails then it returns the payload as a string.
---
---`swim_member_object:payload()` caches its result. Therefore only the
---first call actually decodes cdata payload. All following calls return a
---pointer to the same result, unless payload is changed with a new
---incarnation. If payload was not specified (its size is 0), then nil is
---returned.
---
---@param m swim.member
---@return any payload decoded payload, or nil if there is no payload
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

---Return the UUID as cdata struct tt_uuid.
---
---It is ok to return cdata, because struct tt_uuid type has strong support
---by the 'uuid' Lua module with a nice metatable, serialization, string
---conversions etc.
---
---@param m swim.member
---@return ffi.cdata* uuid UUID as cdata struct tt_uuid
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
    local ptr = swim_check_member(m, 'member:__serialize()')
    local _, size = swim_member_payload_raw(ptr)
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

---Delete a SWIM instance immediately.
---
---Its memory is freed, its member table entry is deleted, and it can no
---longer be used. Other members will treat this member as 'dead'.
---
---After `swim_object:delete()` any attempt to use the deleted instance will
---cause an exception to be thrown.
---
---@param s swim.object
---@return nil # none, this method does not fail
local function swim_delete(s)
    local ptr = swim_check_instance(s, 'swim:delete')
    internal.swim_delete(ffi.gc(ptr, nil))
    s.ptr = nil
    setmetatable(s, swim_mt_deleted)
end

---Leave the cluster.
---
---This is a graceful equivalent of `swim_object:delete()` -- the instance
---is deleted, but before deletion it sends to each member in its member
---table a message, that this instance has left the cluster, and should not
---be considered dead.
---
---Other instances will mark such a member in their tables as 'left', and
---drop it after one round of dissemination.
---
---Consequences to the caller are the same as after `swim_object:delete()`
----- the instance is no longer usable, and an error will be thrown if there
---is an attempt to use it.
---
---@param s swim.object
---@return nil # none, the method does not fail
local function swim_quit(s)
    local ptr = swim_check_instance(s, 'swim:quit')
    internal.swim_quit(ffi.gc(ptr, nil))
    s.ptr = nil
    setmetatable(s, swim_mt_deleted)
end

---Return the size of the member table.
---
---It will be at least 1 because the "self" member is included.
---
---@param s swim.object
---@return integer size
local function swim_size(s)
    return capi.swim_size(swim_check_instance(s, 'swim:size'))
end

---Return false if a SWIM instance was created via `swim.new()` without an
---optional `cfg` argument, and was not configured with
---`swim_object:cfg()`. Otherwise return true.
---
---@param s swim.object
---@return boolean result true if configured, otherwise false
local function swim_is_configured(s)
    return capi.swim_is_configured(swim_check_instance(s, 'swim:is_configured'))
end

--
-- Configuration options are printed when a SWIM instance is
-- serialized, for example, in a console.
--
local function swim_serialize(s)
    swim_check_instance(s, 'swim:__serialize()')
    return s.cfg.index
end

---Send a ping request to the specified `uri` address.
---
---If another member is listening at that address, it will receive the ping,
---and respond with an ACK (acknowledgment) message containing information
---such as UUID. That information will be added to the member table.
---
---`swim_object:probe_member()` is similar to `swim_object:add_member()`, but
---it does not require UUID, and it is not reliable because it uses UDP.
---
---@param s swim.object
---@param uri string|integer URI. Format is the same as for `uri` in
---    `swim_object:cfg()`.
---@return boolean? ok true if member is pinged
---@return any? err an error object if an error occurred
local function swim_probe_member(s, uri)
    local func_name = 'swim:probe_member'
    local ptr = swim_check_instance(s, func_name)
    uri = swim_check_uri(uri, func_name)
    if capi.swim_probe_member(ptr, uri) ~= 0 then
        return nil, box.error.last()
    end
    return true
end

---Explicitly add a member into the member table.
---
---This method is useful when a new member is joining the cluster and does
---not yet know what members already exist. In that case it can start
---interaction explicitly by adding the details about an already-existing
---member into its member table. Subsequently SWIM will discover other
---members automatically via messages from the already-existing member.
---
---@param s swim.object
---@param cfg swim.member_cfg description of the member
---@return boolean? ok true if member is added
---@return any? err an error object if an error occurred
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

---Explicitly and immediately remove a member from the member table.
---
---@param s swim.object
---@param uuid string|ffi.cdata* UUID
---@return boolean? ok true if member is removed
---@return any? err an error object if an error occurred
local function swim_remove_member(s, uuid)
    local func_name = 'swim:remove_member'
    local ptr = swim_check_instance(s, func_name)
    uuid = swim_check_uuid(uuid, func_name)
    if capi.swim_remove_member(ptr, uuid) ~= 0 then
        return nil, box.error.last()
    end
    return true
end

---Broadcast a ping request to all the network interfaces in the system.
---
---`swim_object:broadcast()` is like `swim_object:probe_member()` to many
---members at once.
---
---@param s swim.object
---@param port? integer all the sent ping requests have this port as
---    destination port in their UDP headers. By default a currently bound
---    port is used.
---@return boolean? ok true if broadcast is sent
---@return any? err an error object if an error occurred
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fiber = require('fiber')
--- ---
--- ...
--- tarantool> swim = require('swim')
--- ---
--- ...
--- tarantool> s1 = swim.new({uri = 3333,
---         >     uuid = '00000000-0000-1000-8000-000000000001',
---         >     heartbeat_rate = 0.1})
--- ---
--- ...
--- tarantool> s2 = swim.new({uri = 3334,
---         >     uuid = '00000000-0000-1000-8000-000000000002',
---         >     heartbeat_rate = 0.1})
--- ---
--- ...
--- tarantool> s1:size()
--- ---
--- - 1
--- ...
--- tarantool> s1:add_member({uri = s2:self():uri(), uuid = s2:self():uuid()})
--- ---
--- - true
--- ...
--- tarantool> s1:size()
--- ---
--- - 1
--- ...
--- tarantool> s2:size()
--- ---
--- - 1
--- ...
---
--- tarantool> fiber.sleep(0.2)
--- ---
--- ...
--- tarantool> s1:size()
--- ---
--- - 2
--- ...
--- tarantool> s2:size()
--- ---
--- - 2
--- ...
--- tarantool> s1:remove_member(s2:self():uuid())
--- ---
--- ...
--- tarantool> s2:remove_member(s1:self():uuid())
--- ---
--- ...
--- tarantool> s1:size()
--- ---
--- - 1
--- ...
--- tarantool> s2:size()
--- ---
--- - 1
--- ...
---
--- tarantool> s1:probe_member(s2:self():uri())
--- ---
--- - true
--- ...
--- tarantool> fiber.sleep(0.1)
--- ---
--- ...
--- tarantool> s1:size()
--- ---
--- - 2
--- ...
--- tarantool> s2:size()
--- ---
--- - 2
--- ...
--- tarantool> s1:remove_member(s2:self():uuid())
--- ---
--- ...
--- tarantool> s2:remove_member(s1:self():uuid())
--- ---
--- ...
--- tarantool> s1:size()
--- ---
--- - 1
--- ...
--- tarantool> s2:size()
--- ---
--- - 1
--- ...
--- tarantool> s1:broadcast(3334)
--- ---
--- - true
--- ...
--- tarantool> fiber.sleep(0.1)
--- ---
--- ...
--- tarantool> s1:size()
--- ---
--- - 2
--- ...
---
--- tarantool> s2:size()
--- ---
--- - 2
--- ...
--- ```
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

---Return a swim member object (of self) from the member table, or from a
---cache containing earlier results of `swim_object:self()` or
---`swim_object:member_by_uuid()` or `swim_object:pairs()`.
---
---@param s swim.object
---@return swim.member self_member not nil because self() will not fail
local function swim_self(s)
    local ptr = swim_check_instance(s, 'swim:self')
    return swim_wrap_member(s, capi.swim_self(ptr))
end

---Return a swim member object (given UUID) from the member table, or from a
---cache containing earlier results of `swim_object:self()` or
---`swim_object:member_by_uuid()` or `swim_object:pairs()`.
---
---@param s swim.object
---@param uuid string|ffi.cdata* UUID
---@return swim.member? member or nil if not found
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

---Set a payload, as raw data.
---
---Sometimes a payload does not need to be a Lua object. For example, a user
---may already have a well formatted MessagePack object and just wants to set
---it as a payload. Or cdata needs to be exposed. `set_payload_raw` allows
---setting a payload as is, without MessagePack serialization.
---
---@param s swim.object
---@param payload string|ffi.cdata* any value
---@param payload_size? integer payload size in bytes. If `payload` is a
---    string then `size` is optional, and if specified, then should not be
---    larger than the actual `payload` size. If `size` is less than the
---    actual `payload` size, then only the first `size` bytes of `payload`
---    are used. If `payload` is cdata then `size` is mandatory.
---@return boolean? ok true if payload is set
---@return any? err an error object if an error occurred
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> ffi = require('ffi')
--- ---
--- ...
--- tarantool> fiber = require('fiber')
--- ---
--- ...
--- tarantool> swim = require('swim')
--- ---
--- ...
--- tarantool> s1 = swim.new({uri = 0,
---         >     uuid = '00000000-0000-1000-8000-000000000001',
---         >     heartbeat_rate = 0.1})
--- ---
--- ...
--- tarantool> s2 = swim.new({uri = 0,
---         >     uuid = '00000000-0000-1000-8000-000000000002',
---         >     heartbeat_rate = 0.1})
--- ---
--- ...
--- tarantool> s1:add_member({uri = s2:self():uri(), uuid = s2:self():uuid()})
--- ---
--- - true
--- ...
--- tarantool> s1:set_payload({a = 100, b = 200})
--- ---
--- - true
--- ...
--- tarantool> s2:set_payload('any payload')
--- ---
--- - true
--- ...
--- tarantool> fiber.sleep(0.2)
--- ---
--- ...
--- tarantool> s1_view = s2:member_by_uuid(s1:self():uuid())
--- ---
--- ...
--- tarantool> s2_view = s1:member_by_uuid(s2:self():uuid())
--- ---
--- ...
--- tarantool> s1_view:payload()
--- ---
--- - {'a': 100, 'b': 200}
--- ...
--- tarantool> s2_view:payload()
--- ---
--- - any payload
--- ...
--- tarantool> cdata = ffi.new('char[?]', 2)
--- ---
--- ...
--- tarantool> cdata[0] = 1
--- ---
--- ...
--- tarantool> cdata[1] = 2
--- ---
--- ...
--- tarantool> s1:set_payload_raw(cdata, 2)
--- ---
--- - true
--- ...
--- tarantool> fiber.sleep(0.2)
--- ---
--- ...
--- tarantool> cdata, size = s1_view:payload_cdata()
--- ---
--- ...
--- tarantool> cdata[0]
--- ---
--- - 1
--- ...
--- tarantool> cdata[1]
--- ---
--- - 2
--- ...
--- tarantool> size
--- ---
--- - 2
--- ...
--- ```
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

---Set a payload, as formatted data.
---
---Payload is arbitrary user defined data up to 1200 bytes in size and
---disseminated over the cluster. So each cluster member will eventually
---learn what is the payload of other members in the cluster, because it is
---stored in the member table and can be queried with
---`swim_member_object:payload()`.
---
---Different members may have different payloads.
---
---@param s swim.object
---@param payload any arbitrary Lua object to disseminate. Set to nil to
---    remove the payload, in which case it will be eventually removed on
---    other instances. The object is serialized in MessagePack.
---@return boolean? ok true if payload is set
---@return any? err an error object if an error occurred
---
---**Example:**
---
--- ```lua
--- swim_object:set_payload({field1 = 100, field2 = 200})
--- ```
local function swim_set_payload(s, payload)
    local func_name = 'swim:set_payload'
    local ptr = swim_check_instance(s, func_name)
    local rc
    if payload == nil then
        rc = capi.swim_set_payload(ptr, nil, 0)
    else
        local buf = cord_ibuf_take()
        local payload_size = msgpack.encode(payload, buf)
        payload = buf.rpos
        rc = capi.swim_set_payload(ptr, payload, payload_size)
        cord_ibuf_put(buf)
    end
    if rc ~= 0 then
        return nil, box.error.last()
    end
    return true
end

---Enable encryption for all following messages.
---
---When encryption is enabled, all the messages are encrypted with a chosen
---private key, and a randomly generated and updated public key.
---
---All of `algo`, `mode`, `key`, and `key_size` should be the same for all
---SWIM instances, so that members can understand each others' messages.
---
---@param s swim.object
---@param cfg swim.codec description of the encryption
---@return boolean? ok true if codec is set
---@return any? err an error object if an error occurred
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> swim = require('swim')
--- ---
--- ...
--- tarantool> s1 = swim.new({uri = 0,
---         >     uuid = '00000000-0000-1000-8000-000000000001'})
--- ---
--- ...
--- tarantool> s1:set_codec({algo = 'aes128', mode = 'cbc',
---         >     key = '1234567812345678'})
--- ---
--- - true
--- ...
--- ```
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

---Set up an iterator for returning swim member objects from the member
---table, or from a cache containing earlier results of `swim_object:self()`
---or `swim_object:member_by_uuid()` or `swim_object:pairs()`.
---
---`swim_object:pairs()` should be in a 'for' loop, and there should only be
---one iterator in operation at one time. (The iterator is implemented in an
---extra light fashion so only one iterator object is available per SWIM
---instance.)
---
---@param s swim.object
---@return fun(ctx: table, key: any): (string, swim.member)? iter
---    generator function, as for any Lua pairs() iterators
---@return table? state iterator object
---@return nil key initial key (a UUID)
---
---**Example:**
---
--- ```tarantoolsession
--- tarantool> fiber = require('fiber')
--- ---
--- ...
--- tarantool> swim = require('swim')
--- ---
--- ...
--- tarantool> s1 = swim.new({uri = 0,
---         >     uuid = '00000000-0000-1000-8000-000000000001',
---         >     heartbeat_rate = 0.1})
--- ---
--- ...
--- tarantool> s2 = swim.new({uri = 0,
---         >     uuid = '00000000-0000-1000-8000-000000000002',
---         >     heartbeat_rate = 0.1})
--- ---
--- ...
--- tarantool> s1:add_member({uri = s2:self():uri(), uuid = s2:self():uuid()})
--- ---
--- - true
--- ...
--- tarantool> fiber.sleep(0.2)
--- ---
--- ...
--- tarantool> s1:self()
--- ---
--- - uri: 127.0.0.1:55845
---   status: alive
---   incarnation: cdata {generation = 1569353431853325ULL, version = 1ULL}
---   uuid: 00000000-0000-1000-8000-000000000001
---   payload_size: 0
--- ...
--- tarantool> s1:member_by_uuid(s1:self():uuid())
--- ---
--- - uri: 127.0.0.1:55845
---   status: alive
---   incarnation: cdata {generation = 1569353431853325ULL, version = 1ULL}
---   uuid: 00000000-0000-1000-8000-000000000001
---   payload_size: 0
--- ...
--- tarantool> s1:member_by_uuid(s2:self():uuid())
--- ---
--- - uri: 127.0.0.1:53666
---   status: alive
---   incarnation: cdata {generation = 1569353431865138ULL, version = 1ULL}
---   uuid: 00000000-0000-1000-8000-000000000002
---   payload_size: 0
--- ...
--- tarantool> t = {}
--- ---
--- ...
--- tarantool> for k, v in s1:pairs() do table.insert(t, {k, v}) end
--- ---
--- ...
--- tarantool> t
--- ---
--- - - - 00000000-0000-1000-8000-000000000002
---     - uri: 127.0.0.1:53666
---       status: alive
---       incarnation: cdata {generation = 1569353431865138ULL, version = 1ULL}
---       uuid: 00000000-0000-1000-8000-000000000002
---       payload_size: 0
---   - - 00000000-0000-1000-8000-000000000001
---     - uri: 127.0.0.1:55845
---       status: alive
---       incarnation: cdata {generation = 1569353431853325ULL, version = 1ULL}
---       uuid: 00000000-0000-1000-8000-000000000001
---       payload_size: 0
--- ...
--- ```
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
    ---True if the member is new.
    ---@param self swim.event
    ---@return boolean
    is_new = function(self)
        local value = swim_check_event(self, 'event:is_new()')
        return bit.band(value, capi.SWIM_EV_NEW) ~= 0
    end,
    ---True if the member is dropped.
    ---@param self swim.event
    ---@return boolean
    is_drop = function(self)
        local value = swim_check_event(self, 'event:is_drop()')
        return bit.band(value, capi.SWIM_EV_DROP) ~= 0
    end,
    ---True if the member is updated.
    ---@param self swim.event
    ---@return boolean
    is_update = function(self)
        local value = swim_check_event(self, 'event:is_update()')
        return bit.band(value, capi.SWIM_EV_UPDATE) ~= 0
    end,
    ---True if the member status is updated.
    ---@param self swim.event
    ---@return boolean
    is_new_status = function(self)
        local value = swim_check_event(self, 'event:is_new_status()')
        return bit.band(value, capi.SWIM_EV_NEW_STATUS) ~= 0
    end,
    ---True if the member URI is updated.
    ---@param self swim.event
    ---@return boolean
    is_new_uri = function(self)
        local value = swim_check_event(self, 'event:is_new_uri()')
        return bit.band(value, capi.SWIM_EV_NEW_URI) ~= 0
    end,
    ---True if either the generation or the version part of the incarnation
    ---changes.
    ---@param self swim.event
    ---@return boolean
    is_new_incarnation = function(self)
        local value = swim_check_event(self, 'event:is_new_incarnation()')
        return bit.band(value, capi.SWIM_EV_NEW_INCARNATION) ~= 0
    end,
    ---True if the generation part of the incarnation changes.
    ---@param self swim.event
    ---@return boolean
    is_new_generation = function(self)
        local value = swim_check_event(self, 'event:is_new_generation()')
        return bit.band(value, capi.SWIM_EV_NEW_GENERATION) ~= 0
    end,
    ---True if the version part of the incarnation changes.
    ---@param self swim.event
    ---@return boolean
    is_new_version = function(self)
        local value = swim_check_event(self, 'event:is_new_version()')
        return bit.band(value, capi.SWIM_EV_NEW_VERSION) ~= 0
    end,
    ---True if the member payload is updated.
    ---@param self swim.event
    ---@return boolean
    is_new_payload = function(self)
        local value = swim_check_event(self, 'event:is_new_payload()')
        return bit.band(value, capi.SWIM_EV_NEW_PAYLOAD) ~= 0
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

---Create an "on_member trigger".
---
---The `trigger-function` will be executed when a member in the member table
---is updated.
---
---@param s swim.object
---@param ... any the trigger function and optional arguments
---@return function? trigger a pointer to the trigger, or nil
---
---**Usage:**
---
---* `on_member_event(new[, ctx][, name])` - add a new trigger. It should
---  accept 3 arguments: an updated member, an events object, an optional
---  `ctx` parameter passed as is. If name is passed as the third parameter,
---  the new trigger is set by name.
---
---* `on_member_event(new, old[, ctx])` - add a new trigger `new` if not
---  nil, in place of the `old` trigger.
---
---* `on_member_event(new, old, name[, ctx])` - add a new trigger `new` if
---  not nil by passed name, `old` is ignored. Name must be a string,
---  otherwise it is considered to be a ctx.
---
---* `on_member_event{func = new, name = name, ctx = ctx}` - add a new
---  trigger `new` if not nil by passed name.
---
---* `on_member_event()` - get a list of triggers.
---
---The **trigger-function** should have three parameter declarations
---(Tarantool will pass values for them when it invokes the function):
---
---* the member which is having the member event,
---* the event object,
---* the `ctx` which will be the same value as what is passed to
---  `swim_object:on_member_event`.
---
---A **member event** is any of:
---
---* appearance of a new member,
---* drop of an existing member, or
---* update of an existing member.
---
---An **event object** is an object which the trigger-function can use for
---determining what type of member event has happened. The object's methods
----- such as `is_new_status()`, `is_new_uri()`, `is_new_incarnation()`,
---`is_new_payload()`, `is_drop()` -- return boolean values.
---
---A member event may have more than one associated **trigger**. Triggers
---are executed sequentially. Therefore if a trigger function causes yields
---or sleeps, other triggers may be forced to wait. However, since trigger
---execution is done in a separate fiber, SWIM itself is not forced to wait.
---
---`is_new_generation()` will be true if the generation part of the
---incarnation changes. `is_new_version()` will be true if the version part
---of the incarnation changes. `is_new_incarnation()` will be true if either
---the generation part or the version part of the incarnation changes. For
---example a combination of these methods can be used within a user-defined
---trigger to check whether a process has restarted, or a member has
---changed ...
---
---**Example:**
---
--- ```lua
--- local function on_event(member, event, ctx)
---     if event:is_new() then
---         ...
---     elseif event:is_drop() then
---         ...
---     end
---
---     if event:is_update() then
---         -- All next conditions can be true simultaneously.
---         if event:is_new_status() then
---             ...
---         end
---         if event:is_new_uri() then
---             ...
---         end
---         if event:is_new_incarnation() then
---             ...
---         end
---         if event:is_new_payload() then
---             ...
---         end
---     end
--- end
--- ```
---
---The old-trigger value should be the value returned by
---`on_member_event(trigger-function[, ctx])`.
local function swim_on_member_event(s, ...)
    local ptr = swim_check_instance(s, 'swim:on_member_event')
    local new, old, name, ctx =
        internal.swim_on_member_event_normalize_arguments(...)
    if new ~= nil then
        new = swim_on_member_event_new(s, new, ctx)
    end
    return internal.swim_on_member_event(ptr, new, old, name)
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

---Configure or reconfigure a SWIM instance.
---
---The `cfg` table may have these components:
---
---* `heartbeat_rate` (double) -- rate of sending round messages, in seconds.
---  Setting `heartbeat_rate` to X does not mean that every member will be
---  checked every X seconds, instead X is the protocol speed. Protocol
---  period depends on member count and heartbeat_rate. Default = 1.
---
---* `ack_timeout` (double) -- time in seconds after which a ping is
---  considered to be unacknowledged. Default = 30.
---
---* `gc_mode` (enum) -- dead member collection mode. If `gc_mode == 'off'`
---  then SWIM never removes dead members from the member table (though
---  users may remove them with `swim_object:remove_member()`), and SWIM
---  will continue to ping them as if they were alive. If `gc_mode == 'on'`
---  then SWIM removes dead members from the member table after one round.
---  Default = 'on'.
---
---* `uri` (string or number) -- either an 'ip:port' address, or just a port
---  number (if ip is omitted then 127.0.0.1 is assumed). If port == 0, then
---  the kernel will select any free port for the IP address.
---
---* `uuid` (string or cdata struct tt_uuid) -- a value which should be
---  unique among SWIM instances. Users may choose any value but the
---  recommendation is: use `box.cfg.instance_uuid`, the Tarantool
---  instance's UUID.
---
---All the `cfg` components are dynamic -- `swim_object:cfg()` may be called
---more than once. If it is not being called for the first time and a
---component is not specified, then the component retains its previous
---value. If it is being called for the first time then uri and uuid are
---mandatory, since a SWIM instance cannot operate without URI and UUID.
---
---`swim_object:cfg()` is atomic -- if there is an error, then nothing
---changes.
---
---@param c swim.cfg the 'cfg' attribute, a callable table
---@param s swim.object
---@param cfg swim.cfg the options to describe instance behavior
---@return boolean? ok true if configuration succeeds
---@return any? err an error object if an error occurred
---
---**Example:**
---
--- ```lua
--- swim_object:cfg({heartbeat_rate = 0.5})
--- ```
---
---After `swim_object:cfg()`, all other `swim_object` methods are callable.
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

---Create a new SWIM instance.
---
---A SWIM instance maintains a member table and interacts with other
---members. Multiple SWIM instances can be created in a single Tarantool
---process.
---
---If `cfg` is not specified or is nil, then the new SWIM instance is not
---bound to a socket and has nil attributes, so it cannot interact with
---other members and only a few methods are valid until
---`swim_object:cfg()` is called.
---
---If `cfg` is specified, then the effect is the same as calling
---`s = swim.new(); s:cfg()`, except for generation.
---
---The generation part of `cfg` can only be specified during `new()`, it
---cannot be specified later during `cfg()`. Generation is part of the
---incarnation. Usually generation is not specified because the default
---value (a timestamp) is sufficient, but if there is reason to mistrust
---timestamps (because the time is changed or because the instance is
---started on a different machine), then users may say
---`swim.new({generation = <number>})`. In that case the latest value should
---be persisted somehow (for example in a file, or in a space, or in a
---global service), and the new value must be greater than any previous
---value of generation.
---
---@param cfg? swim.new_cfg an optional configuration parameter
---@return swim.object? swim_object
---@return any? err an error object if an error occurred
---
---**Example:**
---
--- ```lua
--- swim_object = swim.new({
---     uri = 3333,
---     uuid = '00000000-0000-1000-8000-000000000001',
---     heartbeat_rate = 0.1,
--- })
--- ```
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
    ffi.gc(ptr, internal.swim_gc)
    local s = setmetatable({
        ptr = ptr,
        cfg = setmetatable({index = {}}, swim_cfg_not_configured_mt),
        cache_table = setmetatable({}, cache_table_mt)
    }, swim_not_configured_mt)
    if cfg then
        local ok, err = s:cfg(cfg)
        if not ok then
            s:delete()
            return nil, err
        end
    end
    return s
end

---The `swim` module contains Tarantool's implementation of SWIM --
---Scalable Weakly-consistent Infection-style Process Group Membership
---Protocol. It is recommended for any type of Tarantool cluster where the
---number of nodes can be large. Its job is to discover and monitor the
---other members in the cluster and keep their information in a "member
---table". It works by sending and receiving, in a background event loop,
---periodically, via UDP, messages.
---
---Each message has several parts, including:
---
---* the ping such as "I am checking whether you are alive",
---* the event such as "I am joining",
---* the anti-entropy such as "I know that another member exists",
---* the payload such as "I or another member could have user-generated
---  data".
---
---The maximum message size is about 1500 bytes.
---
---SWIM sends messages periodically to a random subset of the member table.
---SWIM processes replies from those members asynchronously.
---
---Each entry in the member table has:
---
---* a UUID,
---* a status ("alive", "suspected", "dead", or "left").
---
---When a member fails to acknowledge a certain number of pings, its status
---is changed from "alive" to "suspected", that is, suspected of being dead.
---But SWIM tries to **avoid false positives** (misidentifying members as
---dead) which could happen when a member is overloaded and responds to
---pings too slowly, or when there is network trouble and packets can not go
---through some channels. When a member is suspected, SWIM randomly chooses
---other members and sends requests to them: "please ping this suspected
---member". This is called an **indirect ping**. Thus via different routes
---and additional hops the suspected member gets additional chances to
---reply, and thus "refute" the suspicion.
---
---Because selection is random there is an **even network load** of about
---one message per member per protocol step, regardless of the cluster size.
---This is a major feature of SWIM. Because the protocol depends on members
---passing information on, also known as "gossiping", members do not need to
---broadcast messages to every member, which would cause a network load of N
---messages per member per protocol step, where N is the number of members
---in the cluster. However, selection is not entirely random, there is a
---preference for selecting least-recently-pinged members, like a
---round-robin.
---
---Regarding the **anti-entropy** part of a message: this is necessary for
---maintaining the status in entries of the member table. Consider an
---example where two members, #1 and #2, are both alive. No events happen
---so only pings are being sent periodically. Then a third member, #3
---appears. It knows about one of the existing members, #2. How can it
---discover the other member? Certainly #1 could notify #2 and #2 could
---notify #3, but messages go via UDP, so any notification event can be
---lost. However, regular messages containing "ping" and/or "event" also can
---contain an "anti-entropy" section, which is taken from a randomly-chosen
---part of the member table. So for this example, #2 will eventually
---randomly add to a regular message the anti-entropy note that #1 is alive,
---and thus #3 will discover #1 even though it did not receive a direct
---"I am alive" event message from #1.
---
---Regarding the **UUID** part of an entry in the member table: this is
---necessary for stable identification, because UUID changes more rarely
---than URI (a combination of IP and port number). But if the UUID does
---change, SWIM will include both the new and old UUID in messages, so all
---other members will eventually learn about the new UUID and change the
---member table accordingly.
---
---Regarding the **payload** part of a message: this is not always
---necessary, it is a feature which allows passing user-generated
---information via SWIM instead of via node-to-node communication. The swim
---module has methods for specifying a "payload", which is arbitrary user
---data with a maximum size of about 1.2 KB. The payload can be anything,
---and it will be eventually disseminated over the cluster and available at
---other members. Each member can have its own payload.
---
---Messages can be **encrypted**. Encryption may not be necessary in a
---closed network but is necessary for safety if the cluster is on the
---public Internet. Users can specify an encryption algorithm, an encryption
---mode, and a private key. All parts of all messages (including ping,
---acknowledgment, event, payload, URI, and UUID) will be encrypted with
---that private key, as well as a random public key generated for each
---message to prevent pattern attacks.
---
---In theory the event dissemination speed (the number of hops to pass
---information throughout the cluster) is `O(log(cluster_size))`. For that
---and other theoretical information see the Cornell University
---[paper](https://www.cs.cornell.edu/projects/Quicksilver/public_pdfs/SWIM.pdf)
---which originally described SWIM.
---
local swim = {
    new = swim_new,
}
return swim
