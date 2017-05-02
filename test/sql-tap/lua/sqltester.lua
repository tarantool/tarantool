local tap = require('tap')

local test = tap.test("errno")

local function flatten(arr)
    local result = { }

    local function flatten(arr)
	for _, v in ipairs(arr) do
	    if type(v) == "table" then
		flatten(v)
	    else
		table.insert(result, v)
	    end
	end
    end
    flatten(arr)
    return result
end

-- Goal of this routine is to update expected result
-- to be comparable with expected.
-- Right now it converts logical values to numbers.
-- Input must be a table.
local function fix_result(arr)
    for i, v in ipairs(arr) do
	if type(v) == 'table' then
	    fix_expect(v)
	else
	    if type(v) == 'boolean' then
		if v then
		    arr[i] = 1
		else
		    arr[i] = 0
		end
	    end
	end
    end
end

local function finish_test()
    test:check()
    os.exit()
end
test.finish_test = finish_test

local function do_test(self, label, func, expect)
    local ok, result = pcall(func)
    if ok then
	if result == nil then result = { } end
	-- Convert all trues and falses to 1s and 0s
	fix_result(result)
	-- If expected result is single line of a form '/ ... /' - then
	-- search for string in the result
	if table.getn(expect) == 1
	    and string.sub(expect[1], 1, 1) == '/'
	    and string.sub(expect[1], -1) == '/' then
	    local exp = expect[1]
	    local exp_trimmed = string.sub(exp, 2, string.len(exp) - 2)
	    for _, v in ipairs(result) do
		if string.find(v, exp_trimmed) then
		    return test:ok(self, label)
		end
	    end
	    return test:fail(self, label)
	else
	    self:is_deeply(result, expect, label)
	end
    else
	self:fail(label)
       --io.stderr:write(string.format('%s: ERROR\n', label))
    end
end
test.do_test = do_test

local function execsql(self, sql)
    local result = box.sql.execute(sql)
    if type(result) ~= 'table' then return end

    result = flatten(result)
    for i, c in ipairs(result) do
	if c == nil then
	    result[i] = ""
	end
    end
    return result
end
test.execsql = execsql

local function catchsql(self, sql)
    r = {pcall(execsql, self, sql)}
    if r[1] == true then
	r[1] = 0
	r[2] = table.concat(r[2], " ") -- flatten result
    else
	r[1] = 1
    end
    return r
end
test.catchsql = catchsql

local function do_catchsql_test(self, label, sql, expect)
    if expect[1] == 1 then
	-- expect[2] = table.concat(expect[2], " ")
    end
    return do_test(self, label, function() return catchsql(self, sql) end, expect)
end
test.do_catchsql_test = do_catchsql_test

local function do_catchsql2_test(self, label, sql)
    return do_test(self, label, function() return catchsql2(self, sql) end)
end
test.do_catchsql2_test = do_catchsql2_test

local function do_execsql_test(self, label, sql, expect)
    return do_test(self, label, function() return execsql(self, sql) end, expect)
end
test.do_execsql_test = do_execsql_test

local function do_execsql2_test(self, label, sql)
    return do_test(self, label, function() return execsql2(self, sql) end)
end
test.do_execsql2_test = do_execsql2_test

local function execsql2(self, sql)
    local result = execsql(self, sql)
    if type(result) ~= 'table' then return end
    -- shift rows down, revealing column names
    for i = #result,0,-1 do
        result[i+1] = result[i]
    end
    local colnames = result[1]
    for i,colname in ipairs(colnames) do
        colnames[i] = colname:gsub('^sqlite_sq_[0-9a-fA-F]+','sqlite_subquery')
    end
    result[0] = nil
    return result
end
test.execsql2 = execsql2

local function sortsql(self, sql)
    local result = execsql(self, sql)
    table.sort(result, function(a,b) return a[2] < b[2] end)
    return result
end
test.sortsql = sortsql

local function catchsql2(self, sql)
    return {pcall(execsql2, self, sql)}
end
test.catchsql2 = catchsql2

local function db(self, cmd, ...)
    if cmd == 'eval' then
        return execsql(self, ...)
    end
end
test.db = db

local function lsearch(self, input, seed)
    local result = 0

    local function search(arr)
	if type(arr) == 'table' then
	    for _, v in ipairs(arr) do
		search(v)
	    end
	else
	    if type(arr) == 'string' and arr:find(seed) ~= nil then
		result = result + 1
	    end
	end
    end

    search(input)
    return result
end
test.lsearch = lsearch

--function capable()
--    return true
--end

setmetatable(_G, nil)
os.execute("rm -f *.snap *.xlog*")

-- start the database
box.cfg()

return test
