local t = require('luatest')
local g = t.group()

local crypto = require('crypto')

g.test_rsa_sig_verify = function()

    local pub_key = [=[
    -----BEGIN PUBLIC KEY-----
    MIIBIjANBgkqhkiG9w0BAQEFAAOCAQ8AMIIBCgKCAQEAxTZHa+OdyXZiqLQkrWo4
    6ulcBAx0pmHdxFVXEcwbyQ/uh38bXiWviDwczQyw7Mh9n7r45NYeuiyjJsl9fUzg
    ZJ71alU8ah1mJWE4nMarHdAtr1EmRGXqtU+hzWYDCx9x/LOvhjojCFsQIMGgGe6a
    Ze6ix9Yl4tcZor2WeY1xEuNriaJ9vPPvxjVbP+cOKruK0RYe7sDzTiVrgaIB2Ww3
    8wee2VAqpno+3SNONYzos89kZxBJxdHeWbIyIYPsgbernrRTA+JzimXzfjdI55qG
    50GbyEMaPdlp9uLggUCw0phGQsyxhRHB5WzcSn1IIlc/XBD3/uiulQvkXWDV6gZW
    qQIDAQAB
    -----END PUBLIC KEY-----
    ]=]

    local message = "This is the message to be checked by Tarantool!\n"

    local signature =
            '1648457f63127320826e5e84cdf0a9efa7b1956bc732c80471d8c281856ab4' ..
            '8ad399e071034792f318ea850e0900a4c5941283448210341d283223112f47' ..
            '65b52189acb53a36f4a9bc648c8267056311fdfc7444757780e748df4ddb7e' ..
            'edd9f670e261051660c3a89d34ea280c3e0095adb9752828d2fef19e6089dc' ..
            '465e13d666487cd3795872f51e420febcb7162b2bed62ec60aeb8cb6caa24a' ..
            '729edc7bd3ef4f7eb7c160a9898bdccbbd2b459ab865c26a623ee917c03c40' ..
            '78a8f37d3c43827c017246f2bdc96cc4d196521fccace47059973af258e9f1' ..
            'a14795611d64270f8ef4ea905fb8237bfcf615f932a3abaf4389b119334283' ..
            '81a1e2346e741858'
    signature = string.fromhex(signature)

    t.assert(crypto.rsa_pss_verify(message, pub_key, signature))
end
