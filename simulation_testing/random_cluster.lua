local fiber = require('fiber')
local math = require('math')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')

math.randomseed(os.clock())



--- Random Configuration Generator
--- 
local function rand_cfg(cg)
    local replica_count = math.random(3, 30)
    local uri_set = {}
    for i = 1, replica_count do
        local replica_uri = server.build_listen_uri('replica_'..tostring(i), cg.cluster.id)
        uri_set[i] = replica_uri
    end
    local box_cfg = {
        replication = uri_set,
        replication_synchro_quorum = math.random(1, math.ceil(replica_count / 2)),
        replication_timeout = math.random(1, 10),
    }
    print("Configured replicas:", replica_count,
          "\nReplication URIs:", table.concat(uri_set, ", "),
          "\nSynchro quorum:", box_cfg.replication_synchro_quorum,
          "\nReplication timeout:", box_cfg.replication_timeout)

    return replica_count, box_cfg
end



--- Clear Cluster
--- 
local function clear_cluster(cg)
    if cg.cluster then
        cg.cluster:drop()
    end
    cg = {}
    cg.replicas = {}
end




--- Random Cluster Generator
--- 
local function rand_cluster()
    local cg = {}
    cg.replicas = {}
    clear_cluster(cg)
    cg.cluster = cluster:new{}
    local replica_count, box_cfg = rand_cfg(cg)

    local bound = 1/math.random(1,100)

    for i = 1, replica_count do
        -- Randomize election mode
        if 1/math.random(1,100) < bound then
            box_cfg.election_mode = 'candidate'
        else
            box_cfg.election_mode = 'voter'
        end

        -- Create and add replica to cluster
        cg.replicas[i] = cg.cluster:build_and_add_server{
            alias = 'replica_'..tostring(i),
            box_cfg = box_cfg,
        }
        print("replica_"..tostring(i).." added to cluster as ", box_cfg.election_mode)
    end

    -- Start the cluster
    cg.cluster:start()

    -- Wait for election leader on voter replicas
    for _, repl in ipairs(cg.replicas) do
        repl:wait_until_ready()
        if repl.box_cfg.election_mode == 'voter' then
            repl:wait_until_election_leader_found()
        end
    end
    return cg
end


-- ---  TODO: must add checking not dropped nodes
-- --- Random Server Drop
-- ---
-- local function rand_server_drop()
--     local idx = math.random(1, #cg.cluster.servers)
--     local selected_server = cg.cluster.servers[idx]

--     print("Dropping server:", selected_server.alias)

--     -- Check if the server is the leader
--     local is_leader = selected_server:exec(function()
--         return box.info.election.state == 'leader'
--     end)

--     -- Drop server and handle leadership re-election if needed
--     if is_leader then
--         print("Server is leader. Waiting for new leader...")
--         cg.cluster:wait_until_election_leader_found()
--     else
--         selected_server:drop()
--         print("Dropped server:", selected_server.alias)
--     end
-- end


return {
    rand_cluster = rand_cluster,
    clear_cluster = clear_cluster,
    rand_cfg = rand_cfg,
    -- rand_server_drop = rand_server_drop,
}