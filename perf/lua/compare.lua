--
-- Usage: <script> file1 file2 ...
--
-- An input file is supposed to contain test results in the following format:
--
-- <test1> <result1>
-- <test2> <result1>
-- ...
--
-- Here <testN> is the name of a test case (string, no spaces) and <resultN>
-- is the test case result (number).
--
-- The script reads the result files and outputs the results in a convenient
-- for comparison form:
--
--                  <file1>                  <file2>
-- <test1>  <file1-result1>  <file2-result2> (+NNN%)
-- <test2>  <file1-result2>  <file2-result2> (+NNN%)
-- ...
--

-- Set of test names: <test-name> -> true.
local test_names = {}

-- Test names in the order of appearance.
local test_names_ordered = {}

-- Array of result columns.
local columns = {}

-- Width reserved for the first column (the one with the test names).
local column0_width = 0

-- Read the results from the input files.
for i, file_name in ipairs(arg) do
    local column = {
        name = file_name,
        values = {},
    }
    local width_extra = 2 -- space between columns
    if i > 1 then
        -- Reserve space for diff percentage: ' (+NNN%)'
        width_extra = width_extra + 8
    end
    column.width = string.len(column.name) + width_extra
    table.insert(columns, column)
    for line in io.lines(file_name) do
        local test_name, value = unpack(string.split(line))
        if not test_names[test_name] then
            table.insert(test_names_ordered, test_name)
            test_names[test_name] = true
        end
        column0_width = math.max(column0_width, string.len(test_name))
        column.values[test_name] = value
        column.width = math.max(column.width, string.len(value) + width_extra)
    end
end

-- Print the header row.
local line = string.rjust('', column0_width)
for _, column in ipairs(columns) do
    line = line .. string.rjust(column.name, column.width)
end
print(line)

-- Print the result rows.
for _, test_name in ipairs(test_names_ordered) do
    local line = string.rjust(test_name, column0_width)
    for i, column in ipairs(columns) do
        local value = column.values[test_name]
        if not value then
            value = 'NA'
        end
        if i > 1 then
            local diff = ' (+ NA%)'
            local curr = tonumber(value)
            local base = tonumber(columns[1].values[test_name])
            if base and curr then
                diff = 100 * (curr - base) / base
                diff = string.format(' (%s%3d%%)', diff >= 0 and '+' or '-',
                                     math.abs(diff))
            end
            value = value .. diff
        end
        line = line .. string.rjust(value, column.width)
    end
    print(line)
end
