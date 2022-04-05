-- compat.lua -- internal file

local ffi = require('ffi')
ffi.cdef[[
	extern void json_escape_forward_slash_change(bool value);
]]

local options = {
	json_escape_forward_slash = {
		old = true,
		new = false,
		default = true,
		brief = "<...>",
		doc  = "https://github.com/tarantool/tarantool/wiki/compat_json_escape_forward_slash"
	},
	option_2 = {
		old = false,
		new = true,
		default = true,
		brief = "<...>",
		doc  = "https://github.com/tarantool/tarantool/wiki/option_2"
	}
}

local postaction = {
	json_escape_forward_slash = ffi.C.json_escape_forward_slash_change,
	option_2 = function (value)
			print("option_2 postaction was called!")
		end
}

local cfg = { }

for name, elem in pairs(options) do
	cfg[name] = { }
 	cfg[name].value = elem.default
	cfg[name].selected = false
	postaction[name](elem.default)
end

local function serialize_policy(key, policy)
	assert(policy ~= nil)
	local result = { }
	for f in pairs(policy) do
		result[f] = policy[f]
	end
	result.value = cfg[key].value
	return result
end

local function serialize_compat(compat)
	local result = { }
	for key, val in pairs(options) do
		result[key] = serialize_policy(key, val)
	end
	return result
end

compat = setmetatable({
			candidates = function()
				local result = { }
				for key, val in pairs(options) do
					if not cfg[key].selected then
						result[key] = serialize_policy(key, val)
					end
				end
				return result
			end,
			dump = function()
				local result = "require('tarantool').compat({"
				local isFirst = true
				for key, val in pairs(options) do
					if cfg[key].selected then
						if not isFirst then
							result = result .. ", "
						end
						result = result .. key ..  " = " .. tostring(cfg[key].value)
						isFirst = false
					end
				end
				return result .. "})"
			end,
			reset = function()
				for name, elem in pairs(options) do
 					cfg[name].value = elem.default
					cfg[name].selected = false
				end
			end,
			restore = function(list)								-- TODO mention that it receives only {'option_1', 'option_2'} lists
				if type(list) ~= 'table' then
					error(('Invalid argument %s'):format(list))
				end
				for i, key in pairs(list) do
					if not options[key] then
						error(('Invalid option %s'):format(key))
					end
					cfg[key].value = options[key].default
					cfg[key].selected = false
					postaction[key](cfg[key].value)
				end
			end
		}, {
			__call = function(compat, list)
				if type(list) ~= 'table' then
					error(('Invalid argument %s'):format(list))
				end
				for key, val in pairs(list) do
					if not options[key] then
						error(('Invalid option %s'):format(key))
					end
					cfg[key].value = val
					cfg[key].selected = true
					postaction[key](val)
				end
			end,
			__newindex = function(compat, key, val)					-- TODO debate if setters should be 'old'/'new' or true/false
				if not options[key] then
					error(('Invalid option %s'):format(key))
				end
				cfg[key].value = val
				cfg[key].selected = true
				postaction[key](val)
			end,
			__index = function(compat, key)
				local policy = options[key]
				if not policy then
					error(('Invalid option %s'):format(key))
				end
				return serialize_policy(key, policy);
			end,
			__serialize = serialize_compat,
			__tostring = serialize_compat
		}
	)

return compat
