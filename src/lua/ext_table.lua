local function table_deepcopy(orig)
    local copy = orig
    if type(orig) == 'table' then
        local copy_function = getmetatable(orig).__copy
        if copy_function == nil then
            copy = {}
            for orig_key, orig_value in pairs(orig) do
                copy[orig_key] = deepcopy(orig_value)
            end
        else
            copy = copy_function(orig)
        end
    end
    return copy
end

-- table library extension
table.deepcopy = table_deepcopy
