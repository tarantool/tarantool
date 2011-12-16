function myselect(space, index, key)
    print("select in space: ", space, " index: ", index, " by key ", key)
    return box.select(space, index, key)
end

function myfoo()
    return { 0, "Star Wars", "1977 year", "A New Hope",
	     "A long time ago, in a galaxy far, far away...\n" ..
             "It is a period of civil war. Rebel\n" ..
             "spaceships, striking from a hidden\n" ..
             "base, have won their first victory\n" ..
             "against the evil Galactic Empire.\n" ..
             "\n" ..
             "During the battle, Rebel spies managed\n" ..
             "to steal secret plans to the Empire's\n" ..
             "ultimate weapon, the Death Star, an\n" ..
             "armored space station with enough\n"..
             "power to destroy an entire planet.\n" ..
             "\n" ..
             "Pursued by the Empire's sinister agents,\n" ..
             "Princess Leia races home aboard her\n" ..
             "starship, custodian of the stolen plans\n" ..
             "that can save her people and restore\n" ..
             "freedom to the galaxy...." }
end