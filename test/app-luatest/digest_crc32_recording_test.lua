local digest = require('digest')
local log = require('log')
local net_box = require('net.box')
local server = require('test.luatest_helpers.server')
local t = require('luatest')
local g = t.group()
local TRIES = 3

-- {{{ from crud

local crud = {}
local sharding = {}
local utils = {}
local schema = {}

local const = {}

const.RELOAD_RETRIES_NUM = 1
const.RELOAD_SCHEMA_TIMEOUT = 3 -- 3 seconds
const.FETCH_SHARDING_KEY_TIMEOUT = 3 -- 3 seconds

function utils.extract_key(tuple, key_parts)
    local key = {}
    for i, part in ipairs(key_parts) do
        key[i] = tuple[part.fieldno]
    end
    return key
end
jit.off(utils.extract_key)

local function reload_schema(_)
    return true
end

function schema.wrap_func_reload(func, ...)
    local i = 0

    local res, err, need_reload
    while true do
        res, err, need_reload = func(...)

        if err == nil or not need_reload then
            break
        end

        local replicasets = nil
        local ok, reload_schema_err = reload_schema(replicasets)
        if not ok then
            log.warn("Failed to reload schema: %s", reload_schema_err)
            break
        end

        i = i + 1
        if i > const.RELOAD_RETRIES_NUM then
            break
        end
    end

    return res, err
end

function sharding.key_get_bucket_id(key, specified_bucket_id)
    if specified_bucket_id ~= nil then
        return specified_bucket_id
    end

    return rawget(_G, 'bucket_id_strcrc32_2')({}, key)
end

function sharding.tuple_get_bucket_id(tuple, _, specified_bucket_id)
    if specified_bucket_id ~= nil then
        return specified_bucket_id
    end

    local sharding_index_parts = {{fieldno = 1}}
    local sharding_index_parts_new = {}
    for i, x in ipairs(sharding_index_parts) do
        sharding_index_parts_new[i] = {fieldno = x.fieldno}
    end
    sharding_index_parts = sharding_index_parts_new
    local key = utils.extract_key(tuple, sharding_index_parts)

    return sharding.key_get_bucket_id(key)
end

function sharding.tuple_set_and_return_bucket_id(tuple, space, specified_bucket_id)
    local err
    local bucket_id_fieldno = 4

    if specified_bucket_id ~= nil then
        if tuple[bucket_id_fieldno] == nil then
            tuple[bucket_id_fieldno] = specified_bucket_id
        else
            if tuple[bucket_id_fieldno] ~= specified_bucket_id then
                local err_t = "Tuple and opts.bucket_id contain different " ..
                    "bucket_id values: %s and %s"
                return nil, {err = err_t:format(tuple[bucket_id_fieldno],
                    specified_bucket_id)}
            end
        end
    end

    local bucket_id = tuple[bucket_id_fieldno]
    if bucket_id == nil then
        bucket_id, err = sharding.tuple_get_bucket_id(tuple, space)
        if err ~= nil then
            return nil, err
        end
        tuple[bucket_id_fieldno] = bucket_id
    end

    return bucket_id
end
jit.off(sharding.tuple_set_and_return_bucket_id)


local function call_insert_on_router(_, tuple, opts)
    opts = opts or {}
    sharding.tuple_set_and_return_bucket_id(tuple, nil, opts.bucket_id)
    rawget(_G, 'r')()
end

local function call_delete_on_router(_, key, opts)
    opts = opts or {}
    sharding.key_get_bucket_id(key, opts.bucket_id)
end

function crud.insert(space_name, tuple, opts)
    return schema.wrap_func_reload(call_insert_on_router, space_name, tuple, opts)
end
jit.off(crud.insert)

function crud.delete(space_name, key, opts)
    return schema.wrap_func_reload(call_delete_on_router, space_name, key, opts)
end
jit.off(crud.delete)

-- }}} from crud

-- {{{ from vshard

local function strcrc32(shard_key)
    if type(shard_key) ~= 'table' then
        return digest.crc32(tostring(shard_key))
    else
        local crc32 = digest.crc32.new()
        for _, v in ipairs(shard_key) do
            crc32:update(tostring(v))
        end
        return crc32:result()
    end
end

local function bucket_id_strcrc32(_, key)
    local total_bucket_count = 30000
    return strcrc32(key) % total_bucket_count + 1
end
_G.bucket_id_strcrc32 = bucket_id_strcrc32
jit.off(bucket_id_strcrc32)

local function bucket_id_strcrc32_2(router, key)
    local total_bucket_count = router.total_bucket_count or 30000
    return strcrc32(key) % total_bucket_count + 1
end
_G.bucket_id_strcrc32_2 = bucket_id_strcrc32_2
jit.off(bucket_id_strcrc32_2)

-- }}} from vshard

g.before_all = function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end

local conn
local function r()
    pcall(function()
        if conn == nil then
            conn = net_box.connect(g.server.net_box_uri)
        end
        conn:reload_schema()
    end)
end
_G.r = r

local function test_bucket(iterations)
    require('jit').off()
    require('jit').flush()
    require('jit').on()

    for _ = 1, iterations do
        crud.insert('transfersScenarioContext', {'d', {a = 1}, 1689123123123})
        crud.delete('transfersScenarioContext', {'d'})
    end

    return bucket_id_strcrc32(nil, {'db095e64-9972-400b-a65b-d44047fcb812', nil})
end

g.test_recording = function()
    for _ = 1, TRIES do
        t.assert_equals(test_bucket(100), 29526)
    end
end
