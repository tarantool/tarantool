-- Use vshard's example with the following storage script.

-- Get instance name
local fio = require('fio')
local NAME = fio.basename(arg[0], '.lua')

-- Call a configuration provider
local cfg = {
    sharding = {
        ['cbf06940-0790-498b-948d-042b62cf3d29'] = { -- replicaset #1
            replicas = {
                ['8a274925-a26d-47fc-9e1b-af88ce939412'] = {
                    uri = 'storage:storage@127.0.0.1:3301',
                    name = 'storage_1_a',
                    master = true
                },
                ['3de2e3e1-9ebe-4d0d-abb1-26d301b84633'] = {
                    uri = 'storage:storage@127.0.0.1:3302',
                    name = 'storage_1_b'
                }
            },
        }, -- replicaset #1
        ['ac522f65-aa94-4134-9f64-51ee384f1a54'] = { -- replicaset #2
            replicas = {
                ['1e02ae8a-afc0-4e91-ba34-843a356b8ed7'] = {
                    uri = 'storage:storage@127.0.0.1:3303',
                    name = 'storage_2_a',
                    master = true
                },
                ['001688c3-66f8-4a31-8e19-036c17d489c2'] = {
                    uri = 'storage:storage@127.0.0.1:3304',
                    name = 'storage_2_b'
                }
            },
        }, -- replicaset #2
    }, -- sharding
}

-- Name to uuid map
local names = {
    ['storage_1_a'] = '8a274925-a26d-47fc-9e1b-af88ce939412',
    ['storage_1_b'] = '3de2e3e1-9ebe-4d0d-abb1-26d301b84633',
    ['storage_2_a'] = '1e02ae8a-afc0-4e91-ba34-843a356b8ed7',
    ['storage_2_b'] = '001688c3-66f8-4a31-8e19-036c17d489c2',
}

-- Start the database with sharding
local vshard = require('vshard')
vshard.storage.cfg(cfg, names[NAME])

box.once("testapp", function()
    box.schema.user.grant('guest', 'super')
    box.schema.func.create('put')
    box.schema.func.create('get')

    local format = {{'id', 'unsigned'}, {'bucket_id', 'unsigned'}}
    local a = box.schema.space.create('a', {format = format})
    a:create_index('id', {parts = {'id'}})
    a:create_index('bucket_id', {parts = {'bucket_id'}, unique = false})
end)
