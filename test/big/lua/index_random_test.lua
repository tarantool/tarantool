function index_random_test(space_no, index_no)
	local COUNT = 128; -- enough to resize both sptree and mhash
	-- clear the space
	box.space[space_no]:truncate();
	-- randomize
	math.randomseed(box.time())
	-- insert values into the index
	for k=1,COUNT,1 do box.insert(space_no, k);  end
	-- delete some values from the index
	for i=1,COUNT/2,1 do
		local k = math.random(COUNT);
		local tuple = box.delete(space_no, k);
		if tuple ~= nil then COUNT = COUNT - 1; end
	end

	local rnd_start = math.random(4294967296)
	-- try to get all values from the index using index.random
	local tuples = {}
	local found = 0;
	while found < COUNT do
		local rnd = math.random(4294967296)
		if rnd == rnd_start then
			print('too many iterations');
			return nil;
		end

		local tuple = box.space[space_no].index[index_no]:random(rnd)
		if tuple == nil then
			print('nil returned');
			return nil;
		end

		local k = box.unpack('i', tuple[0]);
		if tuples[k] == nil then
			found = found + 1;
		end
		tuples[k] = 1
	end

	print('all values have been found');
	return true
end
