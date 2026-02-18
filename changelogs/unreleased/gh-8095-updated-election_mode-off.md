## bugfix/raft

* `election_mode='off'` has been reworked internally. A promoted off-mode
   node will now be reported as `'leader'` in `box.info.election.state`.
   Additionally, such nodes are now able to vote for other nodes that may
   have different election modes (gh-8095).
