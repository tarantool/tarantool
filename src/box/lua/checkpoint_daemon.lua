-- checkpoint_daemon.lua (internal file)

local log = require 'log'
local fiber = require 'fiber'
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

local function snapshot()
    log.info("making snapshot...")
    local s, e = pcall(function() box.snapshot() end)
    if not s then
        log.error("error while creating snapshot: %s", e)
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
            offset = random % self.checkpoint_interval
            log.info("reloaded") -- continue
        elseif msg == nil and box.info.status == 'running' then
            snapshot()
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
