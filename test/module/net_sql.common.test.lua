package.path  = "../../src/module/sql/?.lua"
package.cpath  = "?.so"

require("sql")
if type(box.net.sql) ~= "table" then error("net.sql load failed") end

