-- snapshot_daemon.lua (internal file)

do
    local log = require 'log'
    local fiber = require 'fiber'
    local fio = require 'fio'
    local yaml = require 'yaml'
    local errno = require 'errno'

    local PREFIX = 'snapshot_daemon'

    local daemon = { status = 'stopped' }

    local function sprintf(fmt, ...) return string.format(fmt, ...) end

    -- create snapshot, return true if no errors
    local function snapshot()
        log.info("making snapshot...")
        local s, e = pcall(function() box.snapshot() end)
        if s then
            return true
        end
        -- don't complain log if snapshot is already exists
        if errno() == errno.EEXIST then
            return false
        end
        log.error("error while creating snapshot: %s", e)
        return false
    end

    -- create snapshot by several options
    local function make_snapshot(last_snap)

        if box.cfg.snapshot_period == nil then
            return false
        end

        if not(box.cfg.snapshot_period > 0) then
            return false
        end


        if last_snap == nil then
            return snapshot()
        end

        local vclock = box.info.vclock
        local lsn = 0
        for i, vlsn in pairs(vclock) do
            lsn = lsn + vlsn
        end

        local snap_name = sprintf('%020d.snap', tonumber(lsn))
        if fio.basename(last_snap) == snap_name then
            log.debug('snapshot file %s already exists', last_snap)
            return false
        end

        local snstat = fio.stat(last_snap)
        if snstat == nil then
            log.error("can't stat %s: %s", snaps[#snaps], errno.strerror())
            return false
        end
        if snstat.mtime <= fiber.time() + box.cfg.snapshot_period then
            return snapshot(snaps)
        end
    end

    -- check filesystem and current time
    local function process()
        local snaps = fio.glob(fio.pathjoin(box.cfg.snap_dir, '*.snap'))

        if snaps == nil then
            log.error("can't read snap_dir %s: %s", box.cfg.snap_dir,
                      errno.strerror())
            return
        end

        if not make_snapshot(snaps[#snaps]) then
            return
        end

        -- cleanup code
        if box.cfg.snapshot_count == nil then
            return
        end

        if not (box.cfg.snapshot_count > 0) then
            return
        end


        -- reload snap list after snapshot
        snaps = fio.glob(fio.pathjoin(box.cfg.snap_dir, '*.snap'))
        local xlogs = fio.glob(fio.pathjoin(box.cfg.wal_dir, '*.xlog'))
        if xlogs == nil then
            log.error("can't read wal_dir %s: %s", box.cfg.wal_dir,
                      errno.strerror())
            return
        end

        while #snaps > box.cfg.snapshot_count do
            local rm = snaps[1]
            table.remove(snaps, 1)

            log.info("removing old snapshot %s", rm)
            if not fio.unlink(rm) then
                log.error("error while removing %s: %s",
                          rm, errno.strerror())
                return
            end
        end


        local snapno = fio.basename(snaps[1], '.snap')

        while #xlogs > 0 do
            if #xlogs < 2 then
                break
            end

            if fio.basename(xlogs[1], '.xlog') > snapno then
                break
            end

            if fio.basename(xlogs[2], '.xlog') > snapno then
                break
            end


            local rm = xlogs[1]
            table.remove(xlogs, 1)
            log.info("removing old xlog %s", rm)

            if not fio.unlink(rm) then
                log.error("error while removing %s: %s",
                          rm, errno.strerror())
                return
            end
        end
    end


    local function next_snap_interval()
        local interval
        
        if box.cfg.snapshot_period == nil or box.cfg.snapshot_period <= 0 then
            return interval
        end

        local interval = box.cfg.snapshot_period / 10

        local time = fiber.time()
        local snaps = fio.glob(fio.pathjoin(box.cfg.snap_dir, '*.snap'))
        if snaps == nil or #snaps == 0 then
            return interval
        end

        local last_snap = snaps[ #snaps ]
        local stat = fio.stat(last_snap)

        if stat == nil then
            return interval
        end


        -- there is no activity in xlogs
        if box.cfg.snapshot_period * 2 + stat.mtime < time then
            return interval
        end

        local time_left = box.cfg.snapshot_period + stat.mtime - time
        if time_left > 0 then
            return time_left
        end

        return interval

    end

    local function daemon_fiber(self)
        fiber.name(PREFIX)
        log.info("%s", self.status)
        while true do
            local interval = next_snap_interval()

            if interval ~= nil then
                fiber.sleep(interval)
                if self.status ~= 'started' then
                    break
                end

                local s, e = pcall(process)

                if not s then
                    log.error(e)
                end
            else
                -- daemon is disabled
                -- do nothing, waiting for change event wakes us up
                fiber.sleep(3600 * 24 * 365 * 100)
            end
        end
        log.info("%s", self.status)
        log.info("finished daemon fiber")
    end

    setmetatable(daemon, {
        __index = {
            start = function()
                local daemon = box.internal[PREFIX] or daemon
                if daemon.status == 'started' then
                    error("snapshot daemon has already been started")
                end
                daemon.status = 'started'
                daemon.fiber = fiber.create(daemon_fiber, daemon)
            end,

            stop = function()
                local daemon = box.internal[PREFIX] or daemon
                if daemon.status == 'stopped' then
                    error("snapshot daemon has already been stopped")
                end
                daemon.status = 'stopped'
                if daemon.fiber ~= nil then
                    daemon.fiber:wakeup()
                end
                daemon.fiber = nil
            end,

            set_snapshot_period = function(snapshot_period)
                local daemon = box.internal[PREFIX] or daemon
                log.info("new snapshot period is %s",
                         tostring(snapshot_period))
                if daemon.fiber ~= nil then
                    daemon.fiber:wakeup()
                end
                return snapshot_period
            end,

            set_snapshot_count = function(snapshot_count)
                if math.floor(snapshot_count) ~= snapshot_count then
                    box.error(box.error.PROC_LUA,
                        "snapshot_count must be integer")
                end
                local daemon = box.internal[PREFIX] or daemon
                log.info("new snapshot count is %s", tostring(snapshot_count))

                if daemon.fiber ~= nil then
                    daemon.fiber:wakeup()
                end
                return snapshot_period
            end
        }
    })

    if box.internal == nil then
        box.internal = { [PREFIX] = daemon }
    else
        box.internal[PREFIX] = daemon
    end
end
