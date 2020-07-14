local function space_field_types(space_no)
	local types = {};
	for _, index in pairs(box.space[space_no].index) do
		for _,key_def in pairs(index.parts) do
			types[key_def.fieldno] = key_def.type;
		end
	end
	return types;
end

local function iterate(space_no, index_no, f1, f2, iterator, ...)
	local sorted = (box.space[space_no].index[index_no].type == "TREE");
	local pkeys = {};
	local tkeys = {};
	local values = {};
	local types = space_field_types(space_no);
	local function get_field(tuple, field_no)
		local f = tuple[field_no]
		if (types[field_no] == 'unsigned') then
			return string.format('%8d', f);
		else
			return f
		end
	end
	for _, v in box.space[space_no].index[index_no]:pairs({...}, { iterator = iterator }) do
		local pk = get_field(v, 1);
		local tk = '$';
		for f = f1 + 1, f2, 1 do tk = (tk..(get_field(v, f))..'$'); end;
		table.insert(values, tk);
		if pkeys[pk] ~= nil then
			error('Duplicate tuple (primary key): '..pk);
		end
		if box.space[space_no].index[index_no].unique and tkeys[tk] ~= nil then
			error('Duplicate tuple (test key): '..tk);
		end;
		tkeys[pk] = true;
		tkeys[tk] = true;
	end;

	if not sorted then
		table.sort(values);
	end;

	return values
end

local function arithmetic(d, count)
	if not d then d = 1 end
	local a = 0;
	local i = 0;

	return function()
		if count and (i >= count) then
			return nil;
		end

		i = i + 1;
		a = a + d;
		return a;
	end
end

local function table_shuffle(t)
	local n = #t
	while n >= 2 do
		local k = math.random(n)
		t[k], t[n] = t[n], t[k]
		n = n - 1
	end
end

local function table_generate(iter)
	local t = {};
	for k in iter do
		table.insert(t, k);
	end

	return t;
end

-- sort all rows as strings(not for tables);
local function sort(tuples)
    local function compare_tables(t1, t2)
        return (tostring(t1) < tostring(t2))
    end
    table.sort(tuples, compare_tables)
    return tuples
end;

local function check_space(space, N)
    local errors = {}

    --
    -- Insert
    --
    local keys = {}
    math.randomseed(0)
    for i=1,N do
        local key = math.random(2147483647)
        keys[i] = key
        space:insert({key, 0})
    end

    --
    -- Select
    --
    table_shuffle(keys)
    for i=1,N do
        local key = keys[i]
        local tuple = space:get({key})
        if tuple == nil or tuple[1] ~= key then
            table.insert(errors, {'missing key after insert', key})
        end
    end

    --
    -- Delete some keys
    --
    table_shuffle(keys)
    for i=1,N,3 do
        local key = keys[i]
        space:delete({key})
    end

    --
    -- Upsert
    --
    for k=1,2 do
        -- Insert/update valuaes
        table_shuffle(keys)
        for i=1,N do
            local key = keys[i]
            space:upsert({key, 1}, {{'+', 2, 1}})
        end
        -- Check values
        table_shuffle(keys)
        for i=1,N do
            local key = keys[i]
            local tuple = space:get({key})
            if tuple == nil or tuple[1] ~= key then
                table.insert(errors, {'missing key after upsert', key})
            end
            if tuple[2] ~= k then
                table.insert(errors, {'invalid value after upsert', key,
                             'found', tuple[2], 'expected', k})
            end
        end
    end

    --
    -- Delete
    --
    table_shuffle(keys)
    for i=1,N do
        local key = keys[i]
        space:delete({key})
    end

    for i=1,N do
        local key = keys[i]
        if space:get({key}) ~= nil then
            table.insert(errors, {'found deleted key', key})
        end
    end

    local count = #space:select()
    -- :len() doesn't work on vinyl
    if count ~= 0 then
        table.insert(errors, {'invalid count after delete', count})
    end

    return errors
end

local function space_bsize(s)
    local bsize = 0
    for _, t in s:pairs() do
        bsize = bsize + t:bsize()
    end

    return bsize
end

local function create_iterator(obj, key, opts)
    local iter, key, state = obj:pairs(key, opts)
    local res = {iter = iter, key = key, state = state}
    res.next = function()
        local _, tp = iter.gen(key, state)
        return tp
    end
    res.iterate_over = function()
        local ret = {}
        local i = 0
        local tp = res.next()
        while tp do
            ret[i] = tp
            i = i + 1
            tp = res.next()
        end
        return ret
    end
    return res
end

local function setmap(tab)
    return setmetatable(tab, { __serialize = 'map' })
end

return {
    iterate = iterate;
    arithmetic = arithmetic;
    table_generate = table_generate;
    table_shuffle = table_shuffle;
    sort = sort;
    check_space = check_space;
    space_bsize = space_bsize;
    create_iterator = create_iterator;
    setmap = setmap;
};
