local fiber = require('fiber')
local math = require('math')
local cluster = require('luatest.replica_set')
local server = require('luatest.server')
local t = require('luatest')
local fio = require('fio')

math.randomseed(os.clock())



--- Utils functions for debugging
local function ensure_replica_dirs_exist()
    local replica_dirs_path = fio.abspath('./replicas_dirs')

    if not fio.path.exists(replica_dirs_path) then
        local ok, err = fio.mkdir(replica_dirs_path)
        if not ok then
            error(string.format("Failed to create directory '%s': %s", replica_dirs_path, err))
        end
        print(string.format("Directory '%s' successfully created.", replica_dirs_path))
    elseif not fio.path.is_dir(replica_dirs_path) then
        error(string.format("Path '%s' exists but is not a directory", replica_dirs_path))
    end
end

local function create_dirs_for_replica(replica_id)
    ensure_replica_dirs_exist()
    local base_dir = fio.abspath(string.format('./replicas_dirs/replica_%d', replica_id))
    local memtx_dir = fio.pathjoin(base_dir, 'memtx_dir')
    local wal_dir = fio.pathjoin(base_dir, 'wal_dir')
    local log_dir = fio.pathjoin(base_dir, 'log_dir')

    if not fio.path.exists(base_dir) then
        fio.mkdir(base_dir)
    end
    if not fio.path.exists(memtx_dir) then
        fio.mkdir(memtx_dir)
    end
    if not fio.path.exists(wal_dir) then
        fio.mkdir(wal_dir)
    end
    if not fio.path.exists(log_dir) then
        fio.mkdir(log_dir)
    end

    return memtx_dir, wal_dir, log_dir
end

local function clear_dirs_for_all_replicas()
    local base_dir = fio.abspath('./replicas_dirs')
    if fio.path.exists(base_dir) then
        fio.rmtree(base_dir) 
    end
end
---


--- Random Configuration Generator
local function rand_cfg(cg, replica_count, replica_id)
    local uri_set = {}
    for i = 1, replica_count do
        local replica_uri = server.build_listen_uri('replica_'..tostring(i), cg.cluster.id)
        uri_set[i] = replica_uri
    end

    local memtx_dir, wal_dir, log_dir = create_dirs_for_replica(replica_id)

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
        txn_isolation = 'best-effort',
        wal_mode = 'write',
    }

    print("Configured replicas:", replica_count,
          "\nReplication URIs:", table.concat(uri_set, ", "),
          "\nSynchro quorum:", box_cfg.replication_synchro_quorum,
          "\nReplication timeout:", box_cfg.replication_timeout)

    return replica_count, box_cfg
end

--- Clear Cluster
local function clear_cluster(cg)
    if cg.cluster then
        cg.cluster:drop()
    end
    cg = {}
    cg.replicas = {}
end

--- Random Cluster Generator
local function rand_cluster(max_number_replicas)
    local cg = {}
    cg.replicas = {}
    clear_cluster(cg)
    cg.cluster = cluster:new{}
    local replica_count = math.random(3, max_number_replicas)
    local candidates_count=0
    for i = 1, replica_count do
        create_dirs_for_replica(i)
        -- Генерация конфигурации для каждой реплики
        local _, box_cfg = rand_cfg(cg, replica_count, i)

        -- Случайный выбор election_mode
        if math.random() > 0.5 then
            box_cfg.election_mode = 'candidate'
            candidates_count = candidates_count + 1
        else
            box_cfg.election_mode = 'voter'
        end

        if i == replica_count and candidates_count == 0 then
            box_cfg.election_mode = 'candidate'
        end
        -- Создание и добавление реплики в кластер
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





return {
    rand_cluster = rand_cluster,
    clear_cluster = clear_cluster,
    rand_cfg = rand_cfg,
    clear_dirs_for_all_replicas = clear_dirs_for_all_replicas
}