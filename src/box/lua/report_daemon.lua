-- report_daemon.lua (internal file)

local log   = require('log')
local json  = require('json')
local fiber = require('fiber')
local http  = require('http.client')

local PREFIX = "report_daemon"

local daemon = {
    report_enabled  = false,
    report_interval = 0,
    report_host     = nil,
    fiber           = nil,
    control         = nil
}

local function fill_in_report(report)
    if box.info.status ~= "running" then
        return false
    end
    report.tarantool_version = box.info.version
    report.server_id         = box.info.uuid
    report.cluster_id        = box.info.cluster.uuid
    return report
end

local function version_report(self)
    fiber.name(PREFIX, { truncate = true })
    log.verbose("version reports started")

    local header = { report_type = "version", report_version = 1 }

    while true do
        local ok, report = pcall(fill_in_report, header)
        local msg = self.control:get(self.report_interval)
        if msg == "shutdown" then
            break
        elseif msg == "reload" then
            log.verbose("version reports reloaded")
        elseif msg == nil and ok then
            -- ignore any error
            if report ~= false then
                pcall(http.post, self.report_host, json.encode(report))
            end
        end
    end

    log.verbose("version reports stopped")
end

local function reload(self)
    if self.report_enabled == true then
        if self.control == nil then
            -- Start daemon
            self.control = fiber.channel()
            self.fiber = fiber.create(version_report, self)
            -- yeild and allow report fiber to start
            fiber.sleep(0)
        end
    elseif self.control ~= nil then
        -- Shutdown daemon
        self.control:put("shutdown")
        self.fiber = nil
        self.control = nil
        -- yeild and allow report fiber to exit
        fiber.sleep(0)
    end
end

setmetatable(daemon, {
    __index = {
        set_report_params = function()
            daemon.report_enabled  = box.cfg.report_enabled
            daemon.report_host     = box.cfg.report_host
            daemon.report_interval = box.cfg.report_interval
            reload(daemon)
            return
        end,
        generate_test_report = function()
            return fill_in_report({ report_type = 'test' })
        end,
        reload = function()
            daemon.control:put('reload')
        end,
    }
})

if box.internal == nil then
    box.internal = { [PREFIX] = daemon }
else
    box.internal[PREFIX] = daemon
end
