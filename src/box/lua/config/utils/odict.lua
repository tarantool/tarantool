-- Ordered dictionary.
--
-- The collection tracks the order in which the items are added
-- and provides odict.pairs() function to get them in this order.
--
-- The ordered dictionary is a usual Lua table with a specific
-- metatable. All the table operations are applicable.
--
-- It is similar to Python's collections.OrderedDict.
--
-- Example:
--
--  | local od = odict.new()
--  |
--  | od.a = 1
--  | od.b = 2
--  | od.c = 3
--  |
--  | print('od.a', od.a) -- 1
--  | print('od.b', od.b) -- 2
--  | print('od.c', od.c) -- 3
--  |
--  | for k, v in odict.pairs(od) do
--  |     print(k, v)
--  | end
--  | -- print: a, 1
--  | -- print: b, 2
--  | -- print: c, 3
--
-- If an element is deleted and added again, it is added to the
-- end.
--
--  | local od = odict.new()
--  |
--  | od.a = 1
--  | od.b = 2
--  | od.c = 3
--  |
--  | od.b = nil
--  | od.b = 4
--  |
--  | for k, v in odict.pairs(od) do
--  |     print(k, v)
--  | end
--  | -- print: a, 1
--  | -- print: c, 3
--  | -- print: b, 4
--
-- If an element is changed (without prior deletion), it remains
-- on the same position.
--
-- Beware: console shows the fields as unordered. The same for the
-- serialization into JSON/YAML/MessagePack formats. It should be
-- solved after gh-9747.

local REINDEX_THRESHOLD = 1000

local registry = setmetatable({}, {__mode = 'k'})

-- Generator function for odict.pairs().
--
-- Returns (key, value).
local function gen(od, prev_key)
    local ctx = registry[od]

    -- The previous key is nil only on the first call of the
    -- generator function.
    local id = prev_key == nil and 0 or ctx.key2id[prev_key]
    -- NB: This assert doesn't catch all the kinds of changes
    -- during an iteration. It rather verifies a precondition
    -- for the following cycle.
    assert(id ~= nil, 'ordered dictionary is changed during iteration')

    while id <= ctx.max_id do
        id = id + 1
        local key = ctx.id2key[id]
        -- id2key may have holes for IDs <= max_id in place of
        -- deleted items.
        --
        -- id2key may contain a stalled entry, because __newindex
        -- is not called on assignment of an existing field,
        -- including assignment to nil.
        if key ~= nil and od[key] ~= nil then
            return key, od[key]
        end
    end
end

-- Returns a Lua iterator triplet (gen, param, state).
--
-- Usage:
--
--  | for k, v in odict.pairs(od) do
--  |     <...>
--  | end
--
-- The iterator stability guarantees are the same as for usual
-- pairs().
--
-- Quote from the Lua 5.1 reference manual:
--
-- > The behavior of `next` is undefined if, during the traversal,
-- > you assign any value to a non-existent field in the table.
-- > You may however modify existing fields. In particular, you
-- > may clear existing fields.
local function _pairs(od)
    return gen, od, nil
end

-- Delete the key from the id<->key mappings.
local function release(ctx, key)
    local id = ctx.key2id[key]
    if id ~= nil then
        ctx.key2id[key] = nil
        ctx.id2key[id] = nil

        -- No need to grow the IDs if the same key is assigned and
        -- deleted in a loop.
        --
        -- for <...> do
        --     od.x = 'x'
        --     od.x = nil
        -- end
        if id == ctx.max_id then
            ctx.max_id = ctx.max_id - 1
        end
    end
end

-- Track the new key in the id<->key mappings.
local function track(ctx, key)
    local id = ctx.max_id + 1
    ctx.id2key[id] = key
    ctx.key2id[key] = id
    ctx.max_id = id
end

-- Renew the id<->key mappings to eliminate holes.
local function reindex(od, ctx)
    -- This cycle reassigns IDs in the following way.
    --
    --    1     2     3     4
    -- | foo | nil | bar | baz |
    --          ^    | ^    |
    --          +----+ +----+
    local old_max_id = ctx.max_id
    ctx.max_id = 0
    for id = 1, old_max_id do
        local key = ctx.id2key[id]
        -- Drop the given key-id pair from the key<->id mappings.
        if key ~= nil then
            release(ctx, key)
        end
        -- Add the new key-id pair into the key<->id mappings.
        if key ~= nil and od[key] ~= nil then
            track(ctx, key)
        end
    end
end

local mt = {
    __newindex = function(self, k, v)
        local ctx = registry[self]

        -- __newindex is never called on an existing field of a
        -- table.
        assert(rawget(self, k) == nil)

        -- If a field with this key had existed, we should update
        -- the key<->id mappings to let gen() know the new order.
        release(ctx, k)

        -- If a non-existing field is assigned to nil, we have
        -- nothing to do.
        if type(v) == 'nil' then
            return
        end

        -- Add the value.
        track(ctx, k)
        rawset(self, k, v)

        ctx.reindex_countdown = ctx.reindex_countdown - 1
        assert(ctx.reindex_countdown >= 0)

        -- We can't track density, because __newindex is not
        -- called on assigning an existing field to nil.
        --
        -- Let's reindex every N assignments of a new non-nil
        -- field, where N is the size of the dictionary (but not
        -- less than REINDEX_THRESHOLD).
        if ctx.reindex_countdown == 0 then
            reindex(self, ctx)
            ctx.reindex_countdown = math.max(ctx.max_id, REINDEX_THRESHOLD)
        end
    end,
}

local function new()
    local res = setmetatable({}, mt)
    registry[res] = {
        id2key = {},
        key2id = {},
        max_id = 0,
        reindex_countdown = REINDEX_THRESHOLD,
    }
    return res
end

return {
    new = new,
    pairs = _pairs,
}
