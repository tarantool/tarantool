local t = require('luatest')
local console = require('console')

local g = t.group()

local function tabcomplete(s)
    return console.completion_handler(s, 0, #s)
end

g.test_no_metatable = function()
    local tab = {first = true, second = true}
    rawset(_G, 'table11', tab)
    t.assert_items_equals(tabcomplete('table1'), {'table11', 'table11'})
    t.assert_items_equals(tabcomplete('table11.'), {'table11.',
                                                        'table11.first',
                                                        'table11.second',
                                                        })
end

g.test__index = function()
    local tab = {first = true, second = true}
    rawset(_G, 'table11', tab)
    setmetatable(tab, {__index = {index = true}})
    t.assert_items_equals(tabcomplete('table11.'), {'table11.',
                                                    'table11.first',
                                                    'table11.second',
                                                    'table11.index',
                                                    })
end

g.test__autocomplete = function()
    local tab = {first = true, second = true}
    rawset(_G, 'table11', tab)
    -- __autocomplete should always be a function, no extra completions
    setmetatable(tab, {__autocomplete = {auto = true}})
   t.assert_items_equals(tabcomplete('table11.'), {'table11.',
                                                   'table11.first',
                                                   'table11.second',
                                                   })
   -- __autocomplete function can throw an error - should be Ok
   setmetatable(tab, {__autocomplete = function() error('test error') end})
   t.assert_items_equals(tabcomplete('table11.'), {'table11.',
                                                   'table11.first',
                                                   'table11.second',
                                                   })
   -- the right way - should add 'auto' comletion
   setmetatable(tab, {__autocomplete = function() return {auto = true} end})
   t.assert_items_equals(tabcomplete('table11.'), {'table11.',
                                                   'table11.first',
                                                   'table11.second',
                                                   'table11.auto',
                                                   })
   -- __autocomplete supercedes __index, no completions from the latter
   setmetatable(tab, {__autocomplete = function() return {auto = true} end,
                      __index = {index = true}})
   t.assert_items_equals(tabcomplete('table11.'), {'table11.',
                                                   'table11.first',
                                                   'table11.second',
                                                   'table11.auto',
                                                   })
end
