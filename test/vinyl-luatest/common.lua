local common = {}

common.default_vinyl_memory = 512 * 1024 * 1024

function common.default_box_cfg()
    return {
        vinyl_read_threads = 2,
        vinyl_write_threads = 3,
        vinyl_memory = common.default_vinyl_memory,
        vinyl_range_size = 1024 * 64,
        vinyl_page_size = 1024,
        vinyl_run_count_per_level = 1,
        vinyl_run_size_ratio = 2,
        vinyl_cache = 10240, -- 10kB
        vinyl_max_tuple_size = 1024 * 1024 * 6,
    }
end

-- Immutable information for bloom filter tests
common.default_test_space_name = "test"
common.default_primary_index_name = "pk"

common.small_lookup_cost_coeff_value = 0.2
common.middle_lookup_cost_coeff_value = 0.5
common.big_lookup_cost_coeff_value = 0.9
common.max_lookup_cost_coeff_value = 1
-- Big value is chosen to call dump
common.default_big_insert_value_count = 16000

-- Mutable. Should be changed only in server:exec() block
common.selected_lookup_cost_coeff = 0
common.counter = 0
common.bloom_size = 0
common.previous_bloom_size = 0

function common.update_bloom_size(size)
    common.previous_bloom_size = common.bloom_size
    common.bloom_size = size
end

function common.big_insert(s, insert_count_value)
    local digest = require('digest')

    for x = common.counter + 1, common.counter + insert_count_value, 1 do
        s:insert{x, digest.urandom(10000)}
    end
    common.counter = common.counter + insert_count_value
    return common.counter
end

function common.big_update(s)
    local digest = require('digest')

    for x = 1, common.counter + 1, 1 do
        s:update(x, {{'=', 2, digest.urandom(10000)}})
    end
end

return common
