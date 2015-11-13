
function string_function()
    local random_number
    local random_string
    random_string = ""
    for x = 1,20,1 do
        random_number = math.random(65, 90)
        random_string = random_string .. string.char(random_number)
    end
    return random_string
end

function delete_replace_update(engine_name)
    local string_value
    if (box.space._space.index.name:select{'tester'}[1] ~= nil) then
        box.space.tester:drop()
    end
    box.schema.space.create('tester', {engine=engine_name})
    box.space.tester:create_index('primary',{type = 'tree', parts = {1, 'STR'}})

    local random_number
    local string_value_2
    local string_value_3
    local counter = 1
    while counter < 100000 do
        local string_value = string_function()

        local string_table = box.space.tester.index.primary:select({string_value}, {iterator = 'GE', limit = 1})
        if string_table[1] == nil then
            box.space.tester:insert{string_value, counter}
            string_value_2 = string_value
        else
            string_value_2 = string_table[1][1]
        end

        if string_value_2 == nil then
            box.space.tester:insert{string_value, counter}
            string_value_2 = string_value
        end

        random_number = math.random(1,6)

        string_value_3 = string_function()
--      print('<'..counter..'> [' ..  random_number .. '] value_2: ' .. string_value_2 .. ' value_3: ' .. string_value_3)
        if random_number == 1 then
            box.space.tester:delete{string_value_2}
        end
        if random_number == 2 then
            box.space.tester:replace{string_value_2, counter, string_value_3}
        end
        if random_number == 3 then
            box.space.tester:delete{string_value_2}
            box.space.tester:insert{string_value_2, counter}
        end
        if random_number == 4 then
            if counter < 1000000 then
                box.space.tester:delete{string_value_3}
                box.space.tester:insert{string_value_3, counter, string_value_2}
            end
        end
        if random_number == 5 then
            box.space.tester:update({string_value_2}, {{'=', 2, string_value_3}})
        end
        if random_number == 6 then
            box.space.tester:update({string_value_2}, {{'=', 2, string_value_3}})
        end
        counter = counter + 1
    end

    box.space.tester:drop()
    return {counter, random_number, string_value_2, string_value_3}
end

function delete_insert(engine_name)
    local string_value
    if (box.space._space.index.name:select{'tester'}[1] ~= nil) then
        box.space.tester:drop()
    end
    box.schema.space.create('tester', {engine=engine_name})
    box.space.tester:create_index('primary',{type = 'tree', parts = {1, 'STR'}})
    local string_value_2
    local counter = 1
    while counter < 100000 do
        local string_value = string_function()
        local string_table = box.space.tester.index.primary:select({string_value}, {iterator = 'GE', limit = 1})

        if string_table[1] == nil then
            -- print (1, ' insert', counter, string_value)
            box.space.tester:insert{string_value, counter}
            string_value_2 = string_value
        else
            string_value_2 = string_table[1][1]
        end

        if string_value_2 == nil then
            -- print (2, ' insert', counter, string_value)
            box.space.tester:insert{string_value, counter}
            string_value_2 = string_value
        end

        -- print (3, ' delete', counter, string_value_2)
        box.space.tester:delete{string_value_2}

        -- print (4, ' insert', counter, string_value_2)
        box.space.tester:insert{string_value_2, counter}

        counter = counter + 1
    end
    box.space.tester:drop()
    return {counter, string_value_2}
end
