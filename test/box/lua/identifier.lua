local max_len_string = string.rep('a', box.schema.NAME_MAX)

local valid_testcases = {
    --[[ Symbols from various unicode groups ,, --]]
    "1", "_", "sd", "я", "Ё",
    ".", "@", "#" , "⁋", "☢",
    "☒", "↹", "〄", "㐤", "곉",
    "꒮", "ʘ", '￼', "𐎆", "⤘",
    "𐑿", "𝀷","勺", "◉", "༺",
    "Ԙ","Ⅷ","⅘", "℃", "∉",
    "∰","⨌","␡", "⑆", "⑳",
    "╈", "☎", "✇", "⟌", "⣇",
    "⧭", "⭓", max_len_string
}

local invalid_testcases = {
    --[[ Invalid and non printable unicode sequences --]]
    --[[ 1-3 ASCII control, C0 --]]
    "\x01", "\x09", "\x1f",
    --[[ 4-4 ISO/IEC 2022 --]]
    "\x7f",
    --[[ 5-7 C1 --]]
    "\xc2\x80", "\xc2\x90", "\xc2\x9f",
    --[[ 8- zl line separator --]]
    "\xE2\x80\xA8",
    --[[ 9-16 other invalid --]]
    "\x20\x0b",
    "\xE2\x80",
    "\xFE\xFF",
    "\xC2",
    "\xED\xB0\x80",
    "\xE2\x80\xA9",
    "",
    max_len_string..'1'
}

local function run_test(create_func, cleanup_func)
    local bad_tests = {}
    for i, identifier in ipairs(valid_testcases) do
        local ok, res = pcall(create_func,identifier)
        if ok == false then
            table.insert(bad_tests,
	                 string.format("valid_testcases %s: %s",
			               i, tostring(res)))
        else
            cleanup_func(identifier)
        end
    end
    for i, identifier in ipairs(invalid_testcases) do
        local ok = pcall(create_func,identifier)
        if ok then
            table.insert(bad_tests, "invalid_testcases: "..i)
        end
    end
    local res
    if (#bad_tests == 0) then
        res = string.format("All tests passed")
    else
        res = "Errors:\n"..table.concat(bad_tests, "\n")
    end
    return res
end

return {
    run_test = run_test;
};
