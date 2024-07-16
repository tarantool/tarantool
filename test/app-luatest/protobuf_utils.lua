local function strings_deepequal(str1, str2)
    if string.len(str1) ~= string.len(str2) then
        error(string.format('Expected and result strings are not ' ..
            'equal by length: %s, %s', str1:hex(), str2:hex()))
    end
    local charCount = {}
    for i = 1, #str1 do
        local char = str1:sub(i, i)
        charCount[char] = (charCount[char] or 0) + 1
    end
    for i = 1, #str2 do
        local char = str2:sub(i, i)
        if not charCount[char] or charCount[char] == 0 then
            error(string.format('Expected and result strings are not ' ..
                'equal: %s, %s', str1:hex(), str2:hex()))
        end
        charCount[char] = charCount[char] - 1
    end
end

return{
    strings_deepequal = strings_deepequal
}
