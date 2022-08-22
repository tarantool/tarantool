local fiber = require('fiber')

-- Searches for a fiber with the specified name and returns the fiber object
local function find_by_name(name)
    for id, f in pairs(fiber.info()) do
        if f.name == name then
            return fiber.find(id)
        end
    end
    return nil
end

return {
    find_by_name = find_by_name
}
