function tst_sleep( )
    
    local tuple = box.select( 0, 0, box.pack('i', 1) )
    if tuple == nil then
        return { 'unknown', '0.0' }
    end

    local delay = 0.01 * math.random()
    if math.random(1000) > 100 then
        box.fiber.sleep(delay)
    else
        box.fiber.sleep(.3)
        delay = 1
    end
    return { tuple[1], string.format('%f', delay) }

end

function tst_rand_init()
    math.randomseed( os.time() )
end
