function iterate(space_no, index_no, f1, f2, ...)
	local sorted = (box.space[space_no].index[index_no].type == "TREE");
	local pkeys = {};
	local tkeys = {};
	local values = {};
	for v in box.space[space_no].index[index_no]:iterator(...) do
		local pk = v:slice(0, 1);
		local tk = '$';
		for f = f1, f2-1, 1 do tk = (tk..(v[f])..'$'); end;
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
end;
