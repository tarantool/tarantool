-- snapshot_daemon.lua (internal file)

do
    local log = require 'log'
    local fiber = require 'fiber'
    local fio = require 'fio'
    local yaml = require 'yaml'
    local errno = require 'errno'
    local digest = require 'digest'
    local pickle = require 'pickle'

    local PREFIX = 'snapshot_daemon'

    local daemon = {
        snapshot_period = 0;
        snapshot_period_bias = 0;
        snapshot_count = 6;
    }

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

        if daemon.snapshot_period == nil then
            return false
        end

        if not(daemon.snapshot_period > 0) then
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
            if daemon.last_snap_name ~= snap_name then
                daemon.last_snap_name = snap_name
                log.debug('snapshot file %s already exists', last_snap)
            end
            return false
        end
        daemon.last_snap_name = snap_name

        local snstat = fio.stat(last_snap)
        if snstat == nil then
            log.error("can't stat %s: %s", last_snap, errno.strerror())
            return false
        end
        if snstat.mtime <= fiber.time() + daemon.snapshot_period then
            return snapshot()
        end
    end

    -- check filesystem and current time
    local function process(self)
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
        if daemon.snapshot_count == nil then
            return
        end

        if not (self.snapshot_count > 0) then
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

        while #snaps > self.snapshot_count do
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


    local function next_snap_interval(self)

        -- don't do anything in hot_standby mode
        if box.info.status ~= 'running' or
            self.snapshot_period == nil or
            self.snapshot_period <= 0 then
            return nil
        end

        return self.snapshot_period -
            (fiber.time() + self.snapshot_period_bias) % self.snapshot_period
    end

    local function daemon_fiber(self)
        fiber.name(PREFIX)
        log.info("started")
        while true do
            local interval = next_snap_interval(self)
            if interval == nil then
                break
            end
            if self.control:get(interval) == nil then
                local s, e = pcall(process, self)

                if not s then
                    log.error(e)
                end
            else
                log.info("reloaded")
            end
        end
        log.info("stopped")
        self.control:close()
        self.control = nil
        self.fiber = nil
    end

    local function reload(self)
        if self.snapshot_period > 0 and self.fiber == nil then
            self.control = fiber.channel(5)
            self.fiber = fiber.create(daemon_fiber, self)
        elseif self.fiber ~= nil then
            -- wake up daemon
            self.control:put(true)
        end
    end

    setmetatable(daemon, {
        __index = {
            set_snapshot_period = function()
                daemon.snapshot_period = box.cfg.snapshot_period
                if daemon.snapshot_period > 0 then
                    local rnd = pickle.unpack('i', digest.urandom(4))
                    daemon.snapshot_period_bias = rnd % daemon.snapshot_period
                else
                    daemon.snapshot_period_bias = 0
                end
                reload(daemon)
                return
            end,

            set_snapshot_count = function()
                if math.floor(box.cfg.snapshot_count) ~= box.cfg.snapshot_count then
                    box.error(box.error.CFG, "snapshot_count",
                             "must be an integer")
                end
                daemon.snapshot_count = box.cfg.snapshot_count
                reload(daemon)
            end,

            next_snap_interval = next_snap_interval
        }
    })

    if box.internal == nil then
        box.internal = { [PREFIX] = daemon }
    else
        box.internal[PREFIX] = daemon
    end
end
