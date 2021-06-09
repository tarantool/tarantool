test_run = require('test_run').new()

-- gh-6034: test that every box.ctl.promote() bumps
-- the instance's term. Even when elections are disabled.
box.cfg{election_mode='off'}

term = box.info.election.term
box.ctl.promote()
assert(box.info.election.term == term + 1)

-- Consequent promotes are no-ops on the leader.
box.ctl.promote()
assert(box.info.election.term == term + 1)

-- Cleanup.
box.ctl.demote()
