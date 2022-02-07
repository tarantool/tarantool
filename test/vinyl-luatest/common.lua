local common = {}

function common.default_box_cfg()
    return {
        vinyl_read_threads = 2,
        vinyl_write_threads = 3,
        vinyl_memory = 512 * 1024 * 1024,
        vinyl_range_size = 1024 * 64,
        vinyl_page_size = 1024,
        vinyl_run_count_per_level = 1,
        vinyl_run_size_ratio = 2,
        vinyl_cache = 10240, -- 10kB
        vinyl_max_tuple_size = 1024 * 1024 * 6,
    }
end

return common
