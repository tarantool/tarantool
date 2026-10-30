local function index_random_test(space, index_no)
	local COUNT = 128 -- enough to resize both sptree and mhash
	-- clear the space
	space:truncate()
	-- randomize
	math.randomseed(os.time())
	-- insert values into the index
	for k=1,COUNT,1 do space:insert{k}  end
	-- delete some values from the index
	for _=1,COUNT/2,1 do
		local k = math.random(COUNT)
		local tuple = space:delete{k}
		if tuple ~= nil then COUNT = COUNT - 1 end
	end

	local rnd_start = math.random(4294967296)
	-- try to get all values from the index using index.random
	local tuples = {}
	local found = 0
	while found < COUNT do
		local rnd = math.random(4294967296)
		if rnd == rnd_start then
			error('too many iterations')
			return nil
		end

		local tuple = space.index[index_no]:random(rnd)
		if tuple == nil then
			error('nil returned')
			return nil
		end

		local k = tuple[1]
		if tuples[k] == nil then
			found = found + 1
		end
		tuples[k] = 1
	end

	return true
end

return index_random_test
