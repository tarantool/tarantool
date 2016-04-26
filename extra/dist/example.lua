-- This is default tarantool initialization file
-- with easy to use configuration examples including
-- replication, sharding and all major features
-- Complete documentation available in:  http://tarantool.org/doc/
--
-- To start this example just run "sudo tarantoolctl start example" or
-- use init scripts provided by binary packages.
-- To connect to the instance, use "sudo tarantoolctl enter example"
-- Features:
-- 1. Database configuration
-- 2. Binary logging and snapshots
-- 3. Replication
-- 4. Automatinc sharding
-- 5. Message queue
-- 6. Data expiration

-----------------
-- Configuration
-----------------
box.cfg {
    --------------------
    -- Basic parameters
    --------------------

    -- An absolute path to directory where write-ahead log (.xlog) files are
    -- stored. If not specified, defaults to /var/lib/tarantool/INSTANCE
    -- wal_dir = nil;

    -- An absolute path to directory where snapshot (.snap) files are stored.
    -- If not specified, defaults to /var/lib/tarantool/INSTANCE
    -- snap_dir = nil;

    -- An absolute path to directory where phia files are stored.
    -- If not specified, defaults to /var/lib/tarantool/INSTANCE
    -- phia_dir = nil;

    -- The read/write data port number or URI
    -- Has no default value, so must be specified if
    -- connections will occur from remote clients 
    -- that do not use “admin address”
    listen = 3301;

    -- Inject the given string into server process title
    -- custom_proc_title = 'example';

    -------------------------
    -- Storage configuration
    -------------------------

    -- How much memory Tarantool allocates
    -- to actually store tuples, in gigabytes
    slab_alloc_arena = 0.5;

    -- Size of the smallest allocation unit
    -- It can be tuned up if most of the tuples are not so small
    slab_alloc_minimal = 16;

    -- Size of the largest allocation unit
    -- It can be tuned up if it is necessary to store large tuples
    slab_alloc_maximal = 1048576;

    -- Use slab_alloc_factor as the multiplier for computing
    -- the sizes of memory chunks that tuples are stored in
    slab_alloc_factor = 1.06;

    -------------------
    -- Snapshot daemon
    -------------------

    -- The interval between actions by the snapshot daemon, in seconds
    snapshot_period = 0;

    -- The maximum number of snapshots that the snapshot daemon maintans
    snapshot_count = 6;

    --------------------------------
    -- Binary logging and snapshots
    --------------------------------

    -- Abort if there is an error while reading
    -- the snapshot file (at server start)
    panic_on_snap_error = true;

    -- Abort if there is an error while reading a write-ahead
    -- log file (at server start or to relay to a replica)
    panic_on_wal_error = true;

    -- How many log records to store in a single write-ahead log file
    rows_per_wal = 5000000;

    -- Reduce the throttling effect of box.snapshot() on
    -- INSERT/UPDATE/DELETE performance by setting a limit
    -- on how many megabytes per second it can write to disk
    snap_io_rate_limit = nil;

    -- Specify fiber-WAL-disk synchronization mode as:
    -- "none": write-ahead log is not maintained;
    -- "write": fibers wait for their data to be written to the write-ahead log;
    -- "fsync": fibers wait for their data, fsync follows each write;
    wal_mode = "none";

    -- Number of seconds between periodic scans of the write-ahead-log
    -- file directory
    wal_dir_rescan_delay = 2.0;

    ---------------
    -- Replication
    ---------------

    -- The server is considered to be a Tarantool replica
    -- it will try to connect to the master
    -- which replication_source specifies with a URI
    -- for example konstantin:secret_password@tarantool.org:3301
    -- by default username is "guest"
    -- replication_source="127.0.0.1:3102";

    --------------
    -- Networking
    --------------

    -- The server will sleep for io_collect_interval seconds
    -- between iterations of the event loop
    io_collect_interval = nil;

    -- The size of the read-ahead buffer associated with a client connection
    readahead = 16320;

    ----------
    -- Logging
    ----------

    -- How verbose the logging is. There are six log verbosity classes:
    -- 1 – SYSERROR
    -- 2 – ERROR
    -- 3 – CRITICAL
    -- 4 – WARNING
    -- 5 – INFO
    -- 6 – DEBUG
    log_level = 5;

    -- By default, the log is sent to /var/log/tarantool/INSTANCE.log
    -- If logger is specified, the log is sent to the file named in the string
    -- logger = "example.log";

    -- If true, tarantool does not block on the log file descriptor
    -- when it’s not ready for write, and drops the message instead
    logger_nonblock = true;

    -- If processing a request takes longer than
    -- the given value (in seconds), warn about it in the log
    too_long_threshold = 0.5;
}

local function bootstrap()
    local space = box.schema.create_space('example')
    space:create_index('primary')
    -- Comment this if you need fine grained access control (without it, guest
    -- will have access to everything)
    box.schema.user.grant('guest', 'read,write,execute', 'universe')

    -- Keep things safe by default
    --  box.schema.user.create('example', { password = 'secret' })
    --  box.schema.user.grant('example', 'replication')
    --  box.schema.user.grant('example', 'read,write,execute', 'space', 'example')
end

-- for first run create a space and add set up grants
box.once('example-1.0', bootstrap)

-----------------------
-- Automatinc sharding
-----------------------
-- N.B. you need install tarantool-shard package to use shadring
-- Docs: https://github.com/tarantool/shard/blob/master/README.md
-- Example:
--local shard = require('shard')
--local shards = {
--    servers = {
--        { uri = [[host1.com:4301]]; zone = [[0]]; };
--        { uri = [[host2.com:4302]]; zone = [[1]]; };
--    };
--    login = 'tester';
--    password = 'pass';
--    redundancy = 2;
--    binary = '127.0.0.1:3301';
--    monitor = false;
--}
--shard.init(cfg)

-----------------
-- Message queue
-----------------
-- N.B. you need to install tarantool-queue package to use queue
-- Docs: https://github.com/tarantool/queue/blob/master/README.md
-- Example:
--queue = require('queue')
--queue.start()
--queue.create_tube(tube_name, 'fifottl')

-------------------
-- Data expiration
-------------------
-- N.B. you need to install tarantool-expirationd package to use expirationd
-- Docs: https://github.com/tarantool/expirationd/blob/master/README.md
-- Example:
--job_name = 'clean_all'
--expirationd = require('expirationd')
--function is_expired(args, tuple)
--  return true
--end
--function delete_tuple(space_id, args, tuple)
--  box.space[space_id]:delete{tuple[1]}
--end
--expirationd.run_task(job_name, space.id, is_expired, {
--    process_expired_tuple = delete_tuple, args = nil,
--    tuple_per_item = 50, full_scan_time = 3600
--})
