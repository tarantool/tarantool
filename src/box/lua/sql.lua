-- sql.lua (internal file)

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

        -- do_connect is written in C
        -- it add 'raw' field in the table
        local s, c = pcall(box.net.sql.do_connect, self)
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
                self.queue[ box.fiber.fid ] = box.ipc.channel()
                self.queue[ box.fiber.fid ]:get()
                self.queue[ box.fiber.fid ] = nil
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
                if self.raise then
                    error("SQL request returned multiply rows")
                else
                    return {}, -1, "SQL request returned multiply rows"
                end
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
        end
    }

}
