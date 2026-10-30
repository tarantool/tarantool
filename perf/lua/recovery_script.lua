local function run(sort_threads, use_sort_data, work_dir, engine,
                   index, nohint, sk_count, column_count, row_count,
                   wal_row_count, wal_replace_count)
    local log = require('log')

    box.cfg{
        memtx_use_sort_data = use_sort_data,
        memtx_sort_threads = sort_threads,
        memtx_memory = 4*1024*1024*1024,
        work_dir = work_dir,
        log = 'tarantool.log',
        checkpoint_count = 1,
    }

    box.once('init', function()
        -- Enable the sort data to write it to the snapshot.
        box.cfg{memtx_use_sort_data = true}
        log.info('Creating the test space...')
        local format = {}
        for i = 1, column_count do
            table.insert(format, {'field_' .. i, 'unsigned'})
        end
        local s = box.schema.space.create('test', {
            engine = engine,
            field_count = #format,
            format = format,
        })
        s:create_index('pk', {type = index, hint = not nohint})
        for i = 1, sk_count do
            s:create_index('sk' .. i, {parts = {{i + 1, 'unsigned'}},
                                       type = index, hint = not nohint,
                                       unique = false})
        end
        local tuple = {}
        local function write_rows(n, base)
            local pct_complete = 0
            box.begin()
            for i = 1, n do
                tuple[1] = base + i
                for j = 2, column_count do
                    tuple[j] = math.random(1, 1000000)
                end
                s:replace(tuple)
                if i % 1000 == 0 then
                    box.commit()
                    local pct = math.floor(100 * i / n)
                    if pct ~= pct_complete then
                        log.info('%d%% complete', pct)
                        pct_complete = pct
                    end
                    box.begin()
                end
            end
            box.commit()
        end
        log.info('Generating the test data set...')
        write_rows(row_count, 0)
        log.info('Writing a snapshot...')
        box.snapshot()
        log.info('Writing WAL (inserts)...')
        write_rows(wal_row_count, row_count)
        log.info('Writing WAL (replaces)...')
        write_rows(wal_replace_count, 0)
        log.info('Done.')
    end)

    os.exit(0)
end

return {run = run}
