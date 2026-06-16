local t = require("luatest")
local fiber = require("fiber")
local server = require("luatest.server")

local g = t.group()

local replica_count = 10
local ddl_worker_count = 10

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
        for worker_id = 1, ddl_worker_count do
            fiber.create(function()
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
                    fiber.yield()
                end
            end)
        end
    end, { ddl_worker_count })

    local fibers = {}
    for i = 1, replica_count do
        cg.replicas[i] = build_replica(cg, ("replica_%02d"):format(i))
        cg.replicas[i]:start({ wait_until_ready = false })
        fibers[i] = fiber.create(function()
            cg.replicas[i]:wait_until_ready()
        end)
        fibers[i]:set_joinable(true)
    end
    for _, f in ipairs(fibers) do
        local ok, err = f:join()
        if not ok then
            error(err)
        end
    end

    cg.master:exec(function()
        rawset(_G, "gh_11028_stop_ddl", true)
    end)
end
