-- sql.lua (internal file)

local fiber = require('fiber')

if box.net == nil then
    box.net = {}
end

box.net.sql = {
    -- constructor 
    -- box.net.sql.connect(
    --          'pg',                       -- @driver ('pg' or 'mysql')
    --          'my.host',                  -- @host
    --          5432,                       -- @port
    --          'user',                     -- @username
    --          'SECRET',                   -- @password
    --          'DB',                       -- @database name
    --          { raise = false },           -- @config options
    --          { sql1, var, ... var },     -- @startup SQL statements
    --          ...
    -- )
    --
    -- @return connector to database or throw error
    -- if option raise set in 'false' and an error will be happened
    -- the function will return 'nil' as the first variable and
    -- text of error as the second

    connect = function(driver, host, port, user, password, db, cfg, ...)

        if type(driver) == 'table' then
            driver = driver.driver
        end

        if type(box.net.sql.connectors[driver]) ~= 'function' then
            error(string.format("Unknown driver '%s'", driver))
        end

        local self = {
            -- connection variables
            driver      = driver,
            host        = host,
            port        = port,
            user        = user,
            password    = password,
            db          = db,

            -- private variables
            queue       = {},
            processing  = false,

            -- throw exception if error
            raise       = true
        }

        -- config parameters
        if type(cfg) == 'table' then
            if type(cfg.raise) == 'boolean' then
                self.raise = cfg.raise
            end
        end

        local init      = { ... }

        setmetatable(self, box.net.sql)

        -- it add 'raw' field in the table
        local s, c = pcall(box.net.sql.connectors[driver], self)
        if not s then
            if self.raise then
                error(c)
            end
            return nil, c
        end

        -- perform init statements
        for i, s in pairs(init) do
            c:execute(unpack(s))
        end
        return c
    end,

    connectors = { },

    __index = {
        -- base method
        -- example:
        -- local tuples, arows, txtst = db:execute(sql, args)
        --   tuples - a table of tuples (tables)
        --   arows  - count of affected rows
        --   txtst  - text status (Postgresql specific)

        -- the method throws exception by default.
        -- user can change the behaviour by set 'connection.raise'
        -- attribute to 'false'
        -- in the case it will return negative arows if error and
        -- txtst will contain text of error

        execute = function(self, sql, ...)
            -- waits until connection will be free
            while self.processing do
                self.queue[ fiber.id() ] = fiber.channel()
                self.queue[ fiber.id() ]:get()
                self.queue[ fiber.id() ] = nil
            end
            self.processing = true

            local res = { pcall(self.raw.execute, self, sql, ...) }
            self.processing = false
            if not res[1] then
                if self.raise then
                    error(res[2])
                end
                return {}, -1, res[2]
            end

            -- wakeup one waiter
            for fid, ch in pairs(self.queue) do
                ch:put(true, 0)
                self.queue[ fid ] = nil
                break
            end
            table.remove(res, 1)
            return unpack(res)
        end,


        -- pings database
        -- returns true if success. doesn't throw any errors
        ping = function(self)
            local pf = function()
                local res = self:execute('SELECT 1 AS code')
                if type(res) ~= 'table' then
                    return false
                end
                if type(res[1]) ~= 'table' then
                    return false
                end

                return res[1].code == 1
            end

            local res, code = pcall(pf)
            if res == true then
                return code
            else
                return false
            end
        end,


        -- select rows
        -- returns table of rows
        select = function(self, sql, ...)
            local res = self:execute(sql, ...)
            return res
        end,

        -- select one row
        single = function(self, sql, ...)
            local res = self:execute(sql, ...)
            if #res > 1 then
                error("SQL request returned multiply rows")
            end
            return res[1]
        end,

        -- perform request. returns count of affected rows
        perform = function(self, sql, ...)
            local res, affected, status = self:execute(sql, ...)
            return affected
        end,


        -- quote variable
        quote = function(self, variable)
            return self.raw:quote(variable)
        end,
        
        -- quote identifier
        quote_ident = function(self, variable)
            return self.raw:quote_ident(variable)
        end,


        -- begin transaction
        begin_work = function(self)
            return self:perform('BEGIN')
        end,

        -- commit transaction
        commit  = function(self)
            return self:perform('COMMIT')
        end,

        -- rollback transaction
        rollback = function(self)
            return self:perform('ROLLBACK')
        end,

        -- transaction
        txn = function(self, proc)
            local raise = self.raise
            self.raise = true
            self:begin_work()
            local res = { pcall(proc, self) }

            -- commit transaction
            if res[1] then
                res = { pcall(function() self:commit() end) }
                self.raise = raise
                if res[1] then
                    return true
                end
                return res[1], res[2]
            end


            local res_txn = { pcall(function() self:rollback() end) }

            if not res_txn[1] then
                res[2] = res[2] .. "\n" .. res_txn[2]
            end
            self.raise = raise
            return res[1], res[2]
        end
    }
}
