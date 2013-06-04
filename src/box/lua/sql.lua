box.net.sql = {
    connect = function(driver, host, port, user, login, db, ...)

        if type(driver) == 'table' then
            driver = driver.driver
        end

        local self = {
            driver      = driver,
            host        = host,
            port        = port,
            login       = login,
            password    = password,
            db          = db
        }
            
        local init      = { ... }

        setmetatable(self, box.net.sql)

        -- do_connect is written in C
        -- it add 'raw' field in the table
        local c = box.net.sql.do_connect( self )

        -- perform init statements
        for i, s in pairs(init) do
            c:execute(unpack(s))
        end
        return c
    end,


    __index = {
        -- main method
        -- do query and returns: status, resultset
        execute = function(self, sql, ...)
            if self.raw == nil then
                error("Connection was not established")
            end
            error("aaaaaaaaaaaaaaaaaaaaaaaaaaaaaa")
            return self.raw.execute(self, sql, ...)
        end,

        -- quote identifiers
        ident_quote = function(self, ident)
            return self.raw.ident_quote(ident)
        end,

        -- quote variables
        quote = function(self, value)
            return self.raw.quote(value)
        end,
    }

}
