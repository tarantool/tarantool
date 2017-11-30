-- checkpoint_daemon.lua (internal file)

local log = require 'log'
local fiber = require 'fiber'
local fio = require 'fio'
local yaml = require 'yaml'
local errno = require 'errno'
local digest = require 'digest'
local pickle = require 'pickle'

local PREFIX = 'checkpoint_daemon'

local daemon = {
    checkpoint_interval = 0;
    fiber = nil;
    control = nil;
}

-- create snapshot, return true if no errors
local function snapshot()
    log.info("making snapshot...")
    local s, e = pcall(function() box.snapshot() end)
    if s then
        return true
    end
    -- don't complain in the log if the snapshot already exists
    if errno() == errno.EEXIST then
        return false
    end
    log.error("error while creating snapshot: %s", e)
    return false
end

-- check filesystem and current time
local function process(self)

    if daemon.checkpoint_interval == nil then
        return false
    end

    if not(daemon.checkpoint_interval > 0) then
        return false
    end

    local checkpoints = box.internal.gc.info().checkpoints
    local last_checkpoint = checkpoints[#checkpoints]

    local last_snap = fio.pathjoin(box.cfg.memtx_dir,
            string.format('%020d.snap', last_checkpoint.signature))
    local snstat = fio.stat(last_snap)
    if snstat == nil then
        log.error("can't stat %s: %s", last_snap, errno.strerror())
        return false
    end
    if snstat.mtime + daemon.checkpoint_interval <= fiber.time() then
        return snapshot()
    end
end

local function daemon_fiber(self)
    fiber.name(PREFIX, {truncate = true})
    log.info("started")

    --
    -- Add random offset to the initial period to avoid simultaneous
    -- snapshotting when multiple instances of tarantool are running
    -- on the same host.
    -- See https://github.com/tarantool/tarantool/issues/732
    --
    local random = pickle.unpack('i', digest.urandom(4))
    local offset = random % self.checkpoint_interval
    while true do
        local period = self.checkpoint_interval + offset
        -- maintain next_snapshot_time as a self member for testing purposes
        self.next_snapshot_time = fiber.time() + period
        log.info("scheduled the next snapshot at %s",
                os.date("%c", self.next_snapshot_time))
        local msg = self.control:get(period)
        if msg == 'shutdown' then
            break
        elseif msg == 'reload' then
            log.info("reloaded") -- continue
        elseif msg == nil and box.info.status == 'running' then
            local s, e = pcall(process, self)
            if not s then
                log.error(e)
            end
            offset = 0
        end
    end
    self.next_snapshot_time = nil
    log.info("stopped")
end

local function reload(self)
    if self.checkpoint_interval > 0 then
        if self.control == nil then
            -- Start daemon
            self.control = fiber.channel()
            self.fiber = fiber.create(daemon_fiber, self)
            fiber.sleep(0)
        else
            -- Reload daemon
            self.control:put("reload")
            --
            -- channel:put() doesn't block the writer if there
            -- is a ready reader. Give daemon fiber way so that
            -- it can execute before reload() returns to the caller.
            --
            fiber.sleep(0)
        end
    elseif self.control ~= nil then
        -- Shutdown daemon
        self.control:put("shutdown")
        self.fiber = nil
        self.control = nil
        fiber.sleep(0) -- see comment above
    end
end

setmetatable(daemon, {
    __index = {
        set_checkpoint_interval = function()
            daemon.checkpoint_interval = box.cfg.checkpoint_interval
            reload(daemon)
            return
        end,
    }
})

if box.internal == nil then
    box.internal = { [PREFIX] = daemon }
else
    box.internal[PREFIX] = daemon
end
