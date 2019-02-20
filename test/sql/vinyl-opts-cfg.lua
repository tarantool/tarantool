#!/usr/bin/env tarantool

-- Set of custom vinyl params, which are used in the test
-- of the same name (vinyl-opts.test.lua).
--
box.cfg {
    vinyl_bloom_fpr = 0.1,
    vinyl_page_size = 32 * 1024,
    vinyl_range_size = 512 * 1024 * 1024,
    vinyl_run_size_ratio = 5,
    vinyl_run_count_per_level = 3
}

require('console').listen(os.getenv('ADMIN'))
