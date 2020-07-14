local utils = require('utils')

local function create_space()
    local space = box.schema.create_space('tweedledum')
    space:create_index('primary', { type = 'hash', parts = {1, 'unsigned'}, unique = true })
    space:create_index('bitset', { type = 'bitset', parts = {2, 'unsigned'}, unique = false })
end

local function fill(...)
    local space = box.space['tweedledum']
    local nums = utils.table_generate(utils.arithmetic(...))
    utils.table_shuffle(nums)
    for _, v in ipairs(nums) do
        space:insert{v, v}
    end
end

local function delete(...)
    local space = box.space['tweedledum']
    local nums = utils.table_generate(utils.arithmetic(...))
    utils.table_shuffle(nums)
    for _, v in ipairs(nums) do
        space:delete{v}
    end
end

local function clear()
    box.space['tweedledum']:truncate()
end

local function drop_space()
    box.space['tweedledum']:drop()
end

local function dump(...)
	return utils.iterate('tweedledum', 'bitset', 1, 2, ...)
end

local function test_insert_delete(n)
	local t = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
		59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127}

	utils.table_shuffle(t)

	clear()
	fill(1, n)

	for _, v in ipairs(t) do delete(v, n / v) end
	return dump(box.index.BITS_ALL)
end

return {
    clear = clear,
    create_space = create_space,
    delete = delete,
    drop_space = drop_space,
    dump = dump,
    fill = fill,
    test_insert_delete = test_insert_delete,
}
