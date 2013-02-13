local SPACE_NO = 24
local INDEX_NO = 1

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

function dump(...)
	iterate(SPACE_NO, INDEX_NO, 1, 2, ...);
end

function test_insert_delete(n)
	local t = {2, 3, 5, 7, 11, 13, 17, 19, 23, 29, 31, 37, 41, 43, 47, 53,
		59, 61, 67, 71, 73, 79, 83, 89, 97, 101, 103, 107, 109, 113, 127}

	table.shuffle(t);

	clear();
	fill(1, n);

	for _, v in ipairs(t) do delete(v, n / v) end
	dump(box.index.BITS_ALL)
end
