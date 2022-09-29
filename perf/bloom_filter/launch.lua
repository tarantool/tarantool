-- Benchmark is split into two sources: Lua (this one) and C. The reason is
-- following: on the one hand C API does not provide convenient interface to
-- create spaces and define its indexes and format. On the other hand Lua may
-- spoil benchmark results e.g. due to the GC.

local function greetings()
    io.write("\n*****************************************************************\n")
    io.write("\n*********          Bloom filter Perf Test            *******\n")
    io.write("\n*****************************************************************\n")
end

local bench_run_func = "bench.run"
local bench_init_func = "bench.init"
local bench_stop_func = "bench.stop"
local bench_funcs = { bench_run_func, bench_init_func, bench_stop_func }
local primary_index_name = "pk"
local bench_run_tuple_counts = { 2 ^ 14, 2^ 14, 2 ^ 17, 2^ 17, 2 ^ 18, 2 ^ 18 }
local spaces_lookup_cost_coeffs_pairs = { read1VeryFast = 0.05, read2Fast = 0.1, read3Middle = 0.5, read4Slow = 0.9, read5VerySlow = 1 }

local function bench_module_load()
    for _, func_name in pairs(bench_funcs) do
        box.schema.func.create(func_name, { language = "C" })
        box.schema.user.grant('guest', 'execute', 'function', func_name)
    end
end

local function bench_module_unload()
    for _, func_name in pairs(bench_funcs) do
        box.schema.user.revoke('guest', 'execute', 'function', func_name)
        box.schema.func.drop(func_name)
    end
end

local function schema_create()
    -- made range big enough so we can benefit from working on different levels
    -- 1073741824 is the default value for vinyl_range_size for Tarantool < 1.10.2
    local range_size_value = 1073741824 * 3

    for space_name, lookup_coeff in pairs(spaces_lookup_cost_coeffs_pairs) do
        box.schema.space.create(space_name, {
            engine = 'vinyl'
        })
        box.space[space_name]:create_index(primary_index_name, {
            lookup_cost_coeff = lookup_coeff,
            range_size = range_size_value
        })
    end
end

local function removeSpaces()
    local counter = 512
    local removalString = "rm -rf *.snap *.xlog *.vylog"

    for _ in pairs(spaces_lookup_cost_coeffs_pairs) do
        for _ in pairs(bench_run_tuple_counts) do
            removalString = removalString .. " " .. tostring(counter)
            counter = counter + 1
        end
    end
    os.execute(removalString)
end

local function setup()
    -- Cleanup remaining data in case any.
    removeSpaces()

    box.cfg {
        -- make is small in order to call dump more often
        vinyl_memory = 100 * 1024 * 1024,

        -- Do not spam logs with any auxiliary messages.
        log_level = 0,

        -- Disable tuple cache to check bloom hit/miss ratio.
        vinyl_cache = 0
    }
    bench_module_load()
    schema_create()
end

local function schema_drop()
    for space_name, _ in pairs(spaces_lookup_cost_coeffs_pairs) do
        box.space[space_name]:drop()
    end
end

local function cleanup()
    removeSpaces()
    bench_module_unload()
    schema_drop()
end

local function bench_run(tuple_count)
    box.func[bench_init_func]:call({ "Master", tuple_count })
    for space_name, _ in pairs(spaces_lookup_cost_coeffs_pairs) do
        box.func[bench_run_func]:call({ space_name, box.space[space_name].id, tuple_count })
    end
    box.func[bench_stop_func]:call()
end

local function main()
    greetings()
    for _, tuple_count in pairs(bench_run_tuple_counts) do
        setup()
        bench_run(tuple_count)
        cleanup()
    end
    os.exit()
end

main()
