
function push_collection(size, cid, ...)
	local append = { ... }
	local tuple = box.select(18, 0, cid)
	if tuple == nil then
		return box.insert(18, cid, unpack(append) )
	end
	if #append == 0 then
		return tuple
	end
	tuple = tuple:transform( #tuple, 0, unpack( append ) )
	if #tuple - 1 > tonumber(size) then
		tuple = tuple:transform( 1, #tuple - 1 - tonumber(size) )
	end
	return box.replace(18, tuple:unpack() )
end
