local fiber = require('fiber')

box.cfg{listen = 3301} 

box.cfg{
    listen = 3301, -- Порт Leader
    replication = {'tcp://127.0.0.1:3302', 'tcp://127.0.0.1:3303'} 
}

box.cfg{
    listen = 3302, -- Порт Follower 1
    replication = {'tcp://127.0.0.1:3301'} 
}

box.cfg{
    listen = 3303, -- Порт Follower 2
    replication = {'tcp://127.0.0.1:3301'} 
}

print("\nConfiguring done!\n")

if not box.space.data_space then
    box.schema.space.create('data_space', {
        format = {
            {name = 'id', type = 'unsigned'},  
            {name = 'value', type = 'string'}, 
        }
    })
    box.space.data_space:create_index('primary', {parts = {'id'}})
end

box.space.data_space:truncate()


local MAX_SPACE_SIZE = 10
local function insert_with_limit()
    while true do
        local id = math.random(1, 1000)
        local value = 'Value_' .. id

        if box.space.data_space:count() >= MAX_SPACE_SIZE then
            local oldest = box.space.data_space.index.primary:min()
            if oldest then
                box.space.data_space:delete{oldest[1]}
                print('Deleted oldest:', oldest[1], oldest[2])
            end
        end

        box.space.data_space:insert({id, value})

        print('Inserted:', id, value)
        fiber.sleep(math.random(0.1, 1))
    end
end

local function read_random_tuple()
    while true do
        local total_count = box.space.data_space:count()
        
        if total_count > 0 then
            local offset = math.random(0, total_count - 1)
            
            local tuples = box.space.data_space:select({}, {limit = 1, offset = offset})
            local tuple = tuples[1] 
            
            if tuple then
                print('Random Read:', tuple[1], tuple[2])
            end
        else
            print("Space is empty")
        end
        
        fiber.sleep(math.random(0.1, 1))
    end
end

--[[
                 
data_space:truncate()
data_space:insert{1, 'Hello from Leader!'}

local result = data_space:select{1} 
for _, tuple in ipairs(result) do
    print(tuple[2]) 
end

--]]

fiber.create(insert_with_limit)
fiber.create(read_random_tuple)
