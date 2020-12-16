-- Let's test following case: right before data selection tuple is
-- inserted into space. It passes first stage of commit procedure,
-- i.e. it is prepared to be committed but still not yet reached WAL.
-- Meanwhile we are starting to read the same key. At this moment
-- prepared statement is already inserted to in-memory tree. So,
-- read iterator fetches this statement and proceeds to disk scan.
-- In turn, disk scan yields and in this moment WAL fails to write
-- statement on disk, so it is rolled back. Read iterator must
-- recognize this situation and handle it properly.
--
test_run = require('test_run').new()
fiber = require('fiber')
errinj = box.error.injection

test_run:cmd("push filter 'Invalid VYLOG file: Slice [0-9]+ deleted but not registered'" .. \
             " to 'Invalid VYLOG file: Slice <NUM> deleted but not registered'")

s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
sk = s:create_index('sk', {parts = {{2, 'unsigned'}}, unique = false})
s:replace{3, 2}
s:replace{2, 2}
box.snapshot()

c = fiber.channel(1)

function do_write() s:replace{1, 2} end
function init_read() end
function do_read() local ret = sk:select{2} c:put(ret) end

-- Since we have tuples stored on disk, read procedure may
-- yield, opening window for WAL thread to commit or rollback
-- statements. In our case, WAL_WRITE will lead to rollback
-- of {1, 2} statement. We make sure that WAL thread has time
-- to rollback the statement. In this case {1, 2} will be deleted
-- from mem (L0 lsm level) and we'll fall back into read iterator
-- restoration (since rollback bumps mem's version, but iterator's
-- version remains unchanged).
--
test_run:cmd("setopt delimiter ';'");

function read_prepared_with_delay()
    errinj.set("ERRINJ_WAL_DELAY", true)
    fiber.create(do_write, s)
    init_read()
    errinj.set("ERRINJ_VY_READ_PAGE_DELAY", true)
    fiber.create(do_read, sk, c)
    errinj.set("ERRINJ_WAL_WRITE", true)
    errinj.set("ERRINJ_WAL_DELAY", false)
    fiber.sleep(0.1)
    errinj.set("ERRINJ_VY_READ_PAGE_DELAY", false)
    local res = c:get()
    errinj.set("ERRINJ_WAL_WRITE", false)

    return res
end;

test_run:cmd("setopt delimiter ''");

-- Prepared tuple is invisible to read iterator since WAL
-- has enough time and rollback procedure is finished.
--
read_prepared_with_delay()

sk:select{2}

s:drop()

-- A bit more sophisticated case: tuple to be inserted is
-- not the first in result set.
--
s = box.schema.create_space('test', {engine = 'vinyl'})
pk = s:create_index('pk')
-- Set page_size to minimum so that accessing each tuple results
-- in page cache miss and disk access - we need it to set
-- VY_READ_PAGE_DELAY injection.
--
sk = s:create_index('sk', {parts = {{2, 'unsigned'}}, unique = false, page_size = 5})

s:replace{1, 10}
s:replace{2, 20}
s:replace{4, 20}
s:replace{5, 30}
box.snapshot()

gen = nil
param = nil
state = nil
function do_write() s:replace{3, 20} end
function init_read() gen, param, state = sk:pairs({20}, {iterator = box.index.EQ}) gen(param, state) end
function do_read() local _, ret = gen(param, state) c:put(ret) end

read_prepared_with_delay()

-- Read view is aborted due to rollback of prepared statement.
--
gen(param, state)

sk:select{20}

s:drop()
