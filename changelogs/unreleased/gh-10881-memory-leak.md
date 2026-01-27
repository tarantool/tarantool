## bugfix/box

Bug: When a replica is stopped but transactions are still in progress, a memory leak occurs. This patch terminates all active transactions on the instance, writes the changes to WAL, and only then stops the instance.(gh-10881)
