#! /usr/bin/lua
--
-- lua-Harness : <https://fperrad.frama.io/lua-Harness/>
--
-- Copyright (C) 2009-2021, Perrad Francois
--
-- This code is licensed under the terms of the MIT/X11 license,
-- like Lua itself.
--

--[[

=head1 Lua object

=head2 Synopsis

    % prove 232-object.t

=head2 Description

See section "Object-Oriented Programming" in "Programming in Lua".

=cut

--]]

require'test_assertion'

plan(18)

do --[[ object ]]
    local Account = {balance = 0}

    function Account.withdraw (self, v)
        self.balance = self.balance - v
    end

    local a1 = Account; Account = nil
    a1.withdraw(a1, 100.00)
    equals(a1.balance, -100, "object")

    local a2 = {balance = 0, withdraw = a1.withdraw}
    a2.withdraw(a2, 260.00)
    equals(a2.balance, -260)
end

do --[[ object ]]
    local Account = {balance = 0}

    function Account:withdraw (v)
        self.balance = self.balance - v
    end

    local a = Account
    a:withdraw(100.00)
    equals(a.balance, -100, "object")

    Account = { balance = 0,
                withdraw = function (self, v)
                               self.balance = self.balance -v
                           end
              }
    function Account:deposit (v)
        self.balance = self.balance + v
    end

    Account.deposit(Account, 200.00)
    equals(Account.balance, 200, "object")
    Account:withdraw(100.00)
    equals(Account.balance, 100)
end

do --[[ class ]]
    local Account = {balance = 0}

    function Account:new (o)
        o = o or {}
        setmetatable(o, self)
        self.__index = self
        return o
    end

    function Account:deposit (v)
        self.balance = self.balance + v
    end

    function Account:withdraw (v)
        self.balance = self.balance - v
    end

    local a = Account:new{balance = 0}
    a:deposit(100.00)
    equals(a.balance, 100, "classe")

    local b = Account:new()
    equals(b.balance, 0)
    b:deposit(200.00)
    equals(b.balance, 200)
end

do --[[ inheritance ]]
    local Account = {balance = 0}

    function Account:new (o)
    --    print "Account:new"
        o = o or {}
        setmetatable(o, self)
        self.__index = self
        return o
    end

    function Account:deposit (v)
    --    print "Account:deposit"
        self.balance = self.balance + v
    end

    function Account:withdraw (v)
    --    print "Account:withdraw"
        if v > self.balance then error"insuficient funds" end
        self.balance = self.balance - v
    end

    local a = Account:new()
    equals(a.balance, 0, "inheritance")
    -- r, msg = pcall(Account.withdraw, a, 100)
    -- print(msg)

    local SpecialAccount = Account:new()

    function SpecialAccount:withdraw (v)
    --    print "SpecialAccount:withdraw"
        if self.balance - v <= -self:getLimit() then
            error"insuficient funds"
        end
        self.balance = self.balance - v
    end

    function SpecialAccount:getLimit ()
    --    print "SpecialAccount:getLimit"
        return self.limit or 0
    end

    local s = SpecialAccount:new{limit=1000.00}

    s:deposit(100.00)
    equals(s.balance, 100)

    s:withdraw(200.00)
    equals(s.balance, -100)
end

do --[[ multiple inheritance ]]
    -- look up for 'k' in list of tables 'plist'
    local function search (k, plist)
        for i=1, #plist do
            local v = plist[i][k]  -- try 'i'-th superclass
            if v then return v end
        end
    end

    local function createClass (...)
        local c = {}  -- new class
        local arg = {...}

        -- class will search for each method in the list of its
        -- parents ('arg' is the list of parents)
        setmetatable(c, {__index = function (t, k)
            return search(k, arg)
        end})

        -- prepare 'c' to be the metatable of its instance
        c.__index = c

        -- define a new constructor for this new class
        function c:new (o)
            o = o or {}
            setmetatable(o, c)
            return o
        end

        -- return new class
        return c
    end

    local Account = {balance = 0}
    function Account:deposit (v)
        self.balance = self.balance + v
    end
    function Account:withdraw (v)
        self.balance = self.balance - v
    end

    local Named = {}
    function Named:getname ()
        return self.name
    end
    function Named:setname (n)
        self.name = n
    end

    local NamedAccount = createClass(Account, Named)

    local account = NamedAccount:new{name = "Paul"}
    equals(account:getname(), 'Paul', "multiple inheritance")
    account:deposit(100.00)
    equals(account.balance, 100)
end

do --[[ multiple inheritance (patched) ]]
    -- look up for 'k' in list of tables 'plist'
    local function search (k, plist)
        for i=1, #plist do
            local v = plist[i][k]  -- try 'i'-th superclass
            if v then return v end
        end
    end

    local function createClass (...)
        local c = {}  -- new class
        local arg = {...}

        -- class will search for each method in the list of its
        -- parents ('arg' is the list of parents)
        setmetatable(c, {__index = function (t, k)
            -- return search(k, arg)
            return (search(k, arg))
        end})

        -- prepare 'c' to be the metatable of its instance
        c.__index = c

        -- define a new constructor for this new class
        function c:new (o)
            o = o or {}
            setmetatable(o, c)
            return o
        end

        -- return new class
        return c
    end

    local Account = {balance = 0}
    function Account:deposit (v)
        self.balance = self.balance + v
    end
    function Account:withdraw (v)
        self.balance = self.balance - v
    end

    local Named = {}
    function Named:getname ()
        return self.name
    end
    function Named:setname (n)
        self.name = n
    end

    local NamedAccount = createClass(Account, Named)

    local account = NamedAccount:new{name = "Paul"}
    equals(account:getname(), 'Paul', "multiple inheritance (patched)")
    account:deposit(100.00)
    equals(account.balance, 100)
end

do --[[ privacy ]]
    local function newAccount (initialBalance)
        local self = {balance = initialBalance}

        local withdraw = function (v)
                             self.balance = self.balance - v
                         end

        local deposit = function (v)
                            self.balance = self.balance + v
                        end

        local getBalance = function () return self.balance end

        return {
            withdraw = withdraw,
            deposit = deposit,
            getBalance = getBalance
        }
    end

    local acc1 = newAccount(100.00)
    acc1.withdraw(40.00)
    equals(acc1.getBalance(), 60, "privacy")
end

do --[[ single-method approach ]]
    local function newObject (value)
        return function (action, v)
            if action == 'get' then return value
            elseif action == 'set' then value = v
            else error("invalid action")
            end
        end
    end

    local d = newObject(0)
    equals(d('get'), 0, "single-method approach")
    d('set', 10)
    equals(d('get'), 10)
end

-- Local Variables:
--   mode: lua
--   lua-indent-level: 4
--   fill-column: 100
-- End:
-- vim: ft=lua expandtab shiftwidth=4:
