box.cfg{listen=3303, replication={3301, 3302, 3303}}
box.ctl.wait_rw()
box.snapshot()
