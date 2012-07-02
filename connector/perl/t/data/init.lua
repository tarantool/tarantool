function tst_server_name()
    local tuple = box.select( 0, 0, box.pack('i', 1) )
    if tuple == nil then
        return { 'unknown' }
    else
        return { tuple[1] }
    end
end

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


function tst_sleep_force( first_delay, second_delay )

    local tuple = box.select( 0, 0, box.pack('i', 1) )
    local name  = tuple[1]
    local delay = 10.0

    if name == 'first' then
        delay = first_delay
    else
        if name == 'second' then
            delay = second_delay
        else
            return { 'unknown', '0.0', first_delay, second_delay }
        end
    end

    box.fiber.sleep(delay)

    return { name, string.format('%f', delay), first_delay, second_delay }
end
