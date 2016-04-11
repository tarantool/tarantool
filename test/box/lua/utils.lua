function space_field_types(space_no)
	local types = {};
	for _, index in pairs(box.space[space_no].index) do
		for _,key_def in pairs(index.parts) do
			types[key_def.fieldno] = key_def.type;
		end
	end
	return types;
end

function iterate(space_no, index_no, f1, f2, iterator, ...)
	local sorted = (box.space[space_no].index[index_no].type == "TREE");
	local pkeys = {};
	local tkeys = {};
	local values = {};
	local types = space_field_types(space_no);
	local function get_field(tuple, field_no)
		local f = tuple[field_no]
		if (types[field_no] == 'NUM') then
			return string.format('%8d', f);
		else
			return f
		end
	end
	local state, v
	for state, v in box.space[space_no].index[index_no]:pairs({...}, { iterator = iterator }) do
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

function arithmetic(d, count)
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

function table_shuffle(t)
	local n = #t
	while n >= 2 do
		local k = math.random(n)
		t[k], t[n] = t[n], t[k]
		n = n - 1
	end
end

function table_generate(iter)
	local t = {};
	for k in iter do
		table.insert(t, k);
	end

	return t;
end

-- sort all rows as strings(not for tables);
function sort(tuples)
    local function compare_tables(t1, t2)
        return (tostring(t1) < tostring(t2))
    end
    table.sort(tuples, compare_tables)
    return tuples
end;

-- return string tuple
function tuple_to_string(tuple, yaml)
    ans = '['
    for i = 0, #tuple - 1 do
        if #i == 4 then
            ans = ans..i
        elseif #i == 8 then
            ans = ans..i
        else
            ans = ans..'\''..tostring(i)..'\''
        end
        if not #i == #tuple -1 then
            ans = ans..', '
        end
    end
    ans = ans..']'
    if yaml then
        ans = ' - '..ans
    end
    return ans
end;

return {
    space_field_types = space_field_types;
    iterate = iterate;
    arithmetic = arithmetic;
    table_generate = table_generate;
    table_shuffle = table_shuffle;
    sort = sort;
    tuple_to_string = tuple_to_string;
};
