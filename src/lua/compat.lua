-- compat.lua -- internal file

local options = {
	json_escape_forward_slash = {
		old = false,
		new = true,
		default = false,
		brief = "<...>",
		name = "json_escape_forward_slash",
		doc  = "https://github.com/tarantool/tarantool/wiki/compat_json_escape_forward_slash"
	},
	option_2 = {
		old = true,
		new = false,
		default = false,
		brief = "<...>",
		name = "option_2",
		doc  = "https://github.com/tarantool/tarantool/wiki/option_2"
	}
}

local postaction = {
	json_escape_forward_slash = function (value)
			print("json_escape_forward_slash postaction was called!")
		end,
	option_2 = function (value)
			print("option_2 postaction was called!")
		end
}

local cfg = { }

for name, elem in pairs(options) do
	cfg[name] = { }
 	cfg[name].value = elem.default
	cfg[name].selected = false
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
			dump = function()							-- TODO check how dump should look like
				local result = { }
				local i = 1
				for key, val in pairs(options) do
					if cfg[key].selected then
						local str = key ..  " = " .. tostring(cfg[key].value)
						result[i] = str
						i = i + 1
					end
				end
				return result
			end,
			restore = function()
				for name, elem in pairs(options) do
 					cfg[name].value = elem.default
					cfg[name].selected = false
				end
			end
		}, {
			__call = function(compat, list)
				if type(list) ~= table then
					error(('Invalid argument %s'):format(list))
				end
				for key, val in pairs(list) do
					if not options[key] then
						error(('Invalid option %s'):format(key))
					end
					cfg[key].value = val
					postaction[key](val)
				end
			end,
			__newindex = function(compat, key, val)
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
			__to_string = serialize_compat
		}
	)

return compat
