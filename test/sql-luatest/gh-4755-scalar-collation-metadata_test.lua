local server = require('luatest.server')
local t = require('luatest')

local g = t.group()

g.before_all(function()
    g.server = server:new({alias = 'master'})
    g.server:start()
end)

g.after_all(function()
    g.server:stop()
end)

--
-- gh-4755: collation in metadata must be displayed for both
-- string and scalar field types.
--
g.test_4755_scalar_collation_metadata = function()
    g.server:exec(function()
        box.execute([[SET SESSION "sql_full_metadata" = true;]])

        -- Create tables.
        box.execute([[
            CREATE TABLE test_memtx (
        	    a SCALAR COLLATE "unicode_ci" PRIMARY KEY,
        	    b STRING COLLATE "unicode_ci"
    	    ) WITH ENGINE = 'memtx';
        ]])
      	
        box.execute([[
            CREATE TABLE test_vinyl (
        	    a SCALAR COLLATE "unicode_ci" PRIMARY KEY,
        	    b STRING COLLATE "unicode_ci"
    	    ) WITH ENGINE = 'vinyl';
        ]])

        --MEMTX.
        local result = box.execute("SELECT * FROM SEQSCAN test_memtx;")

	    -- Check whole structure.
        t.assert_equals(result, {
            metadata = {
                {
                    span = "a",
                    type = "scalar", 
                    is_nullable =  false,
                    name =  "a",
                    collation =  "unicode_ci"
                },
                {
                    span =  "b", 
                    type =  "string",
                    is_nullable =  true,
                    name =  "b", 
                    collation = "unicode_ci"
                }
            },
            rows = {}
            })
        
        --VINYL.
        local result = box.execute("SELECT * FROM SEQSCAN test_vinyl;")

        -- Check whole structure.
        t.assert_equals(result, {
            metadata = {
                {
                    span = "a",
                    type = "scalar", 
                    is_nullable =  false,
                    name =  "a",
                    collation =  "unicode_ci"
                },
                {
                    span =  "b", 
                    type =  "string",
                    is_nullable =  true,
                    name =  "b", 
                    collation = "unicode_ci"
                }
            },
            rows = {}
            })
        
        box.execute([[SET SESSION "sql_full_metadata" = false;]])
        box.execute([[DROP TABLE test_memtx;]])
        box.execute([[DROP TABLE test_vinyl;]])
        t.assert_equals(box.space.test_memtx, nil)
        t.assert_equals(box.space.test_vinyl, nil)
	
    end)
end
