function space_field_types(space_no)
	local types = {};
	for _, index in pairs(box.space[space_no].index) do
		for _,key_def in pairs(index.key_field) do
			types[key_def.fieldno] = key_def.type;
		end
	end
	return types;
end

function iterate(space_no, index_no, f1, f2, ...)
	local sorted = (box.space[space_no].index[index_no].type == "TREE");
	local pkeys = {};
	local tkeys = {};
	local values = {};
	local types = space_field_types(space_no);
	function get_field(tuple, field_no)
		local f = tuple[field_no]
		if (types[field_no] == 'NUM') then
			return string.format('%8d', box.unpack('i', f));
		elseif (types[field_no] == 'NUM64') then
			return string.format('%8ld', box.unpack('l', f));
		else
			return f
		end
	end
	for v in box.space[space_no].index[index_no]:iterator(...) do
		local pk = get_field(v, 0);
		local tk = '$';
		for f = f1, f2-1, 1 do tk = (tk..(get_field(v, f))..'$'); end;
		table.insert(values, tk);
		if pkeys[pk] ~= nil then
			print('Duplicate tuple (primary key): ', pk);
		end
		if box.space[space_no].index[index_no].unique and tkeys[tk] ~= nil then
			print('Duplicate tuple (test key): ', tk);
		end;
		tkeys[pk] = true;
		tkeys[tk] = true;
	end;

	if not sorted then
		table.sort(values);
		print('sorted output');
	end;

	for i,v in ipairs(values) do print(v) end;
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

function table.shuffle(t)
	local n = #t
	while n >= 2 do
		local k = math.random(n)
		t[k], t[n] = t[n], t[k]
		n = n - 1
	end
end

function table.generate(iter)
	local t = {};
	for k in iter do
		table.insert(t, k);
	end

	return t;
end
