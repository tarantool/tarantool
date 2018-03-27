-- feedback_daemon.lua (internal file)
--
local log   = require('log')
local json  = require('json')
local fiber = require('fiber')
local http  = require('http.client')

local PREFIX = "feedback_daemon"

local daemon = {
    enabled  = false,
    interval = 0,
    host     = nil,
    fiber    = nil,
    control  = nil,
    guard    = nil,
    shutdown = nil
}

local function get_fiber_id(f)
    local fid = 0
    if f ~= nil and f:status() ~= "dead" then
        fid = f:id()
    end
    return fid
end

local function fill_in_feedback(feedback)
    if box.info.status ~= "running" then
        return nil, "not running"
    end
    feedback.tarantool_version = box.info.version
    feedback.server_id         = box.info.uuid
    feedback.cluster_id        = box.info.cluster.uuid
    return feedback
end

local function feedback_loop(self)
    fiber.name(PREFIX, { truncate = true })
    local header = { feedback_type = "version", feedback_version = 1 }

    while true do
        local feedback = fill_in_feedback(header)
        local msg = self.control:get(self.interval)
        -- if msg == "send" then we simply send feedback
        if msg == "stop" then
            break
        elseif feedback ~= nil then
            pcall(http.post, self.host, json.encode(feedback), {timeout=1})
        end
    end
    self.shutdown:put("stopped")
end

local function guard_loop(self)
    fiber.name(string.format("guard of %s", PREFIX), {truncate=true})

    while true do

        if get_fiber_id(self.fiber) == 0 then
            self.fiber = fiber.create(feedback_loop, self)
            log.verbose("%s restarted", PREFIX)
        end
        local st, err = pcall(fiber.sleep, self.interval)
        if not st then
            -- fiber was cancelled
            break
        end
    end
    self.shutdown:put("stopped")
end

-- these functions are used for test purposes only
local function start(self)
    self:stop()
    if self.enabled then
        self.control = fiber.channel()
        self.shutdown = fiber.channel()
        self.guard = fiber.create(guard_loop, self)
    end
    log.verbose("%s started", PREFIX)
end

local function stop(self)
    if (get_fiber_id(self.guard) ~= 0) then
        self.guard:cancel()
        self.shutdown:get()
    end
    if (get_fiber_id(self.fiber) ~= 0) then
        self.control:put("stop")
        self.shutdown:get()
    end
    self.guard = nil
    self.fiber = nil
    self.control = nil
    self.shutdown = nil
    log.verbose("%s stopped", PREFIX)
end

local function reload(self)
    self:stop()
    self:start()
end

setmetatable(daemon, {
    __index = {
        set_feedback_params = function()
            daemon.enabled  = box.cfg.feedback_enabled
            daemon.host     = box.cfg.feedback_host
            daemon.interval = box.cfg.feedback_interval
            reload(daemon)
            return
        end,
        -- this function is used in saving feedback in file
        generate_feedback = function()
            return fill_in_feedback({ feedback_type = "version", feedback_version = 1 })
        end,
        start = function()
            start(daemon)
        end,
        stop = function()
            stop(daemon)
        end,
        reload = function()
            reload(daemon)
        end,
        send_test = function()
            if daemon.control ~= nil then
                daemon.control:put("send")
            end
        end
    }
})

if box.internal == nil then
    box.internal = { [PREFIX] = daemon }
else
    box.internal[PREFIX] = daemon
end
