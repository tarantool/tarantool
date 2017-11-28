-- report_daemon.lua (internal file)

local log   = require('log')
local json  = require('json')
local fiber = require('fiber')
local http  = require('http.client')

local PREFIX = "report_daemon"

local daemon = {
    reports_enabled  = false,
    reports_interval = 0,
    reports_host     = nil,
    fiber            = nil,
    control          = nil
}

local function fill_in_report(report)
    report.tarantool_version = box.info.version
    report.server_id         = box.info.uuid
    report.cluster_id        = box.info.cluster.uuid
    report.timestamp         = fiber.time64()
    return report
end

local function version_report(self)
    fiber.name(PREFIX, {truncate = true})
    log.debug("version reports started")

    local header = { report_type = "version", report_version = 1 }

    while true do
        local msg = self.control:get(self.reports_interval)
        if msg == "reload" then
            log.debug("version reports reloaded")
        elseif msg == "shutdown" then
            break
        elseif msg == nil and box.info.status == "running" then
            local ok, report = pcall(fill_in_report, header)
            if ok then
                http.post(self.reports_host, json.encode(report))
            end
        end
    end

    log.debug("version reports stopped")
end

local function reload(self)
    if self.reports_enabled == true then
        if self.control == nil then
            -- Start daemon
            self.control = fiber.channel()
            self.fiber = fiber.create(version_report, self)
            fiber.sleep(0)
        else
            -- Reload daemon
            self.control:put("reload")
            fiber.sleep(0)
        end
    elseif self.control ~= nil then
        -- Shutdown daemon
        self.control:put("shutdown")
        self.fiber = nil
        self.control = nil
        fiber.sleep(0)
    end
end

setmetatable(daemon, {
    __index = {
        set_report_params = function()
            daemon.reports_enabled  = box.cfg.reports_enabled
            daemon.reports_host     = box.cfg.reports_host
            daemon.reports_interval = box.cfg.reports_interval
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
