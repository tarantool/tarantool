local socket = require('socket')
local fio = require('fio')
local fiber = require('fiber')
local uri = require('uri')
local Proxy = require('luatest.replica_proxy')
local server = require('luatest.server')

local tmp_dir = fio.tempdir()
fio.mktree(tmp_dir)

local master_socket = fio.pathjoin(tmp_dir, 'master.sock')
local replica_socket = fio.pathjoin(tmp_dir, 'replica.sock')
local proxy_to_master = fio.pathjoin(tmp_dir, 'proxy_to_master.sock')
local proxy_to_replica = fio.pathjoin(tmp_dir, 'proxy_to_replica.sock')

local master = server:new({
    alias = 'master',
    box_cfg = {
        listen = master_socket,
        replication = {proxy_to_master},
    }
})
master:start()

local replica = server:new({
    alias = 'replica',
    box_cfg = {
        listen = replica_socket,
        replication = {proxy_to_replica}, 
    }
})
replica:start()

local proxy_to_master_obj = Proxy:new({
    client_socket_path = proxy_to_master, 
    server_socket_path = master_socket, 
})
proxy_to_master_obj:start({force = true})

local proxy_to_replica_obj = Proxy:new({
    client_socket_path = proxy_to_replica, 
    server_socket_path = replica_socket, 
})
proxy_to_replica_obj:start({force = true})



