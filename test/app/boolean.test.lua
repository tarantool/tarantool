#!/usr/bin/env tarantool

--
-- Check that Tarantool creates configuration accepts boolean values
-- 
box.cfg{logger_nonblock=true,
    panic_on_wal_error=true,
    logger="tarantool.log"
}
print(box.cfg.logger_nonblock)
print(box.cfg.panic_on_wal_error)
os.exit()
