local SPACE_NO = 0 
local INDEX_NO = 1

function create_space()
    box.insert(box.schema.SPACE_ID, SPACE_NO, 0, 'tweedledum')
    box.insert(box.schema.INDEX_ID, 0, 0, 'primary', 'hash', 1, 1, 0, 'num')
    box.insert(box.schema.INDEX_ID, 0, INDEX_NO, 'bitset', 'bitset', 0, 1, 1, 'num')
end

function fill(...)
	local nums = table.generate(arithmetic(...));
	table.shuffle(nums);
	for _k, v in ipairs(nums) do
		box.insert(SPACE_NO, v, v);
	end
end

function delete(...)
	local nums = table.generate(arithmetic(...));
	table.shuffle(nums);
	for _k, v in ipairs(nums) do
		box.delete(SPACE_NO, v);
	end
end

function clear()
	box.space[SPACE_NO]:truncate()
end

function drop_space()
	box.space[SPACE_NO]:drop()
end

function dump(...)
	return iterate(SPACE_NO, INDEX_NO, 1, 2, ...);
end

function test_insert_delete(n)
	local t = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
		59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127}

	table.shuffle(t);

	clear();
	fill(1, n);

	for _, v in ipairs(t) do delete(v, n / v) end
	return dump(box.index.BITS_ALL)
end
