local t = require("luatest")
local server = require("luatest.server")

local g = t.group()

local replica_count = 8
local ddl_worker_count = 8

g.before_all(function(cg)
    cg.replicas = {}
    cg.master = server:new({
        alias = "master",
        box_cfg = {
            bootstrap_strategy = "config",
            bootstrap_leader = "master",
            instance_name = "master",
            replication_timeout = 1,
        },
    })
    cg.master:start()
end)

g.after_all(function(cg)
    cg.master:exec(function()
        rawset(_G, "gh_11028_stop_ddl", true)
        rawset(_G, "gh_11028_ddl_fibers", nil)
    end)
    for _, replica in ipairs(cg.replicas) do
        replica:drop()
    end
    cg.master:drop()
end)

local function build_replica(cg, alias)
    local box_cfg = table.deepcopy(cg.master.box_cfg)
    box_cfg.instance_name = alias
    box_cfg.replication = { cg.master.net_box_uri }
    box_cfg.read_only = true
    return server:new({
        alias = alias,
        box_cfg = box_cfg,
    })
end

g.test_join_replicas_while_master_runs_ddl = function(cg)
    cg.master:exec(function(ddl_worker_count)
        local fiber = require("fiber")
        rawset(_G, "gh_11028_stop_ddl", false)
        rawset(_G, "gh_11028_ddl_fibers", {})
        for worker_id = 1, ddl_worker_count do
            local f = fiber.create(function()
                local prefix = "test"
                local iteration = 0
                while not _G.gh_11028_stop_ddl do
                    iteration = iteration + 1
                    local space_name = string.format("%s%03d_%020d", prefix,
                                                     worker_id, iteration)
                    local s = box.schema.space.create(space_name)
                    s:create_index("pk")
                    s:create_index("bucket", {
                        unique = false,
                        parts = { { 2, "unsigned" } },
                    })
                    s:drop()
                end
            end)
            f:set_joinable(true)
            table.insert(_G.gh_11028_ddl_fibers, f)
        end
    end, { ddl_worker_count })

    for i = 1, replica_count do
        cg.replicas[i] = build_replica(cg, ("replica_%02d"):format(i))
        cg.replicas[i]:start({ wait_until_ready = false })
    end
    for _, replica in ipairs(cg.replicas) do
        replica:wait_until_ready()
    end

    cg.master:exec(function()
        rawset(_G, "gh_11028_stop_ddl", true)
        for _, f in ipairs(_G.gh_11028_ddl_fibers) do
            assert(f:join())
        end
    end)
end
