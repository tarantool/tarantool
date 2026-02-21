## bugfix/box

Fixed a memory leak when a replica was stopped but transactions were still in 
progress. This patch terminates all active transactions on the instance, writes the 
changes to WAL, and only then stops the instance (gh-10881).
