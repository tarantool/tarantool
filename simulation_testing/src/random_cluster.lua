local math = require('math')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local fio = require('fio')
local fio_utils = require('fio_utils')
local proxy_handling = require('proxy_handling')
local fiber = require('fiber')



math.randomseed(os.clock())



--- Random Configuration Generator
local function rand_cfg(cg, replica_count, replica_id)
    local uri_set = {}
    for i = 1, replica_count do
        if i == replica_id then
            table.insert(uri_set, server.build_listen_uri('replica_'..tostring(i), cg.cluster.id))
        elseif WITHOUT_PROXY ~= "true" then
            local proxy_uri = fio.abspath(
                server.build_listen_uri('proxy_'..tostring(replica_id)..'_to_'..tostring(i), cg.cluster.id)
            )
            table.insert(uri_set, proxy_uri)
        else
            table.insert(uri_set, server.build_listen_uri('replica_'..tostring(i), cg.cluster.id))
        end
    end

    local memtx_dir, wal_dir, log_dir = fio_utils.create_dirs_for_replica(replica_id)
    local log_file = fio.pathjoin(log_dir, 'replica_'..tostring(replica_id)..'.log')

    local box_cfg = {
        replication = uri_set,
        replication_synchro_quorum = math.random(math.floor(replica_count / 2) + 1, replica_count),
        replication_timeout = math.random(1, 10),
        checkpoint_count = 2,
        memtx_use_mvcc_engine = true,
        memtx_dir = memtx_dir,
        wal_dir = wal_dir,
        log = log_file,
        wal_mode = 'write'
    }

    if WITHOUT_BEST_EFFORT ~= "true" then
        box_cfg.txn_isolation = 'best-effort'
    end

    print("Configured replica:", replica_id,
          "\nReplication URIs:", table.concat(uri_set, ", "),
          "\nSynchro quorum:", box_cfg.replication_synchro_quorum,
          "\nReplication timeout:", box_cfg.replication_timeout)

    return box_cfg
end

--- Clear Cluster
local function clear_cluster(cg)
    if cg.cluster then
        cg.cluster:drop()
    end
    if cg.proxies and WITHOUT_PROXY ~= "true" then
        for _, proxy in ipairs(cg.proxies) do
            proxy:stop()
        end
    end
    cg = {}
    cg.replicas = {}
    cg.proxies = {}
end

local function make_cluster(replica_count)
    local cg = {}
    cg.replicas = {}
    cg.proxies = {}
    clear_cluster(cg)
    cg.cluster = cluster:new{}
    local candidates_count=0

    -- Creating nodes and their configurations
    for i = 1, replica_count do
        fio_utils.create_dirs_for_replica(i)
        -- Generating the configuration for each replica
        local box_cfg = rand_cfg(cg, replica_count, i)

        -- Random selection of selection_mode
        -- if you want to make nodes election_mode have equal probability to be 'voter' or 'candidate' make  > 0.5
        if math.random() > 0 then
            box_cfg.election_mode = 'candidate'
            candidates_count = candidates_count + 1
        else
            box_cfg.election_mode = 'voter'
        end

        if i == replica_count and candidates_count == 0 then
            box_cfg.election_mode = 'candidate'
        end
        -- Creating and adding a replica to a cluster
        cg.replicas[i] = cg.cluster:build_and_add_server{
            alias = 'replica_'..tostring(i),
            box_cfg = box_cfg,
        }
    end

    -- Create proxies only if they are enabled
    if WITHOUT_PROXY ~= "true" then
        for i_id = 1, replica_count do
            for j_id = 1, replica_count do
                if i_id ~= j_id then
                    local proxy = proxy_handling.create_proxy_for_connection(cg, i_id, j_id)
                    table.insert(cg.proxies, proxy)
                    proxy:start({force = true})
                    print(string.format("Proxy for replica_%d to replica_%d started.", i_id, j_id))
                end
            end
        end
    end

    -- Start the cluster
    cg.cluster:start()

    -- Wait for election leader on voter replicas
    for _, repl in ipairs(cg.replicas) do
        repl:wait_until_ready()
        repl:wait_until_election_leader_found()
    end
    return cg
end

--- Random Cluster Generator
local function rand_cluster(max_number_replicas)
    local replica_count = math.random(max_number_replicas, max_number_replicas)
    local cg = make_cluster(replica_count)
    fiber.sleep(20)
    return cg
end





return {
    rand_cluster = rand_cluster,
    clear_cluster = clear_cluster,
    rand_cfg = rand_cfg,
    make_cluster = make_cluster
}