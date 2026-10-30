-- Generate a function that calls two given functions in a row.
--
-- If one of the arguments is nil, return the other one.
--
-- If both arguments are nil, returns nil.
local function chain2(f1, f2)
    if f1 == nil then
        return f2
    end
    if f2 == nil then
        return f1
    end
    return function(...)
        f1(...)
        f2(...)
    end
end

return {
    chain2 = chain2,
}
