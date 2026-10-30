local function push_collection(space, size, cid, ...)
	local append = { ... }
	local tuple = space:get{cid}
	if tuple == nil then
		return space:insert{cid, unpack(append)}
	end
	if #append == 0 then
		return tuple
	end
	tuple = tuple:transform( #tuple + 1, 0, unpack( append ) )
	if #tuple - 1 > tonumber(size) then
		tuple = tuple:transform( 2, #tuple - 1 - tonumber(size) )
	end
	return space:replace{tuple:unpack()}
end

return push_collection
