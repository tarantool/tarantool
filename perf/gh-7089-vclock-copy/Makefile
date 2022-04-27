SHELL := /bin/bash

test: test_none stop

test_wal: test_write stop

dirs:
	for i in {1..11}; do mkdir $${i} || true; done

start_swarm:
	for i in {2..11}; do ./swarm.lua $${i}; done

test_none: clean dirs start_swarm
	./speedtest.lua none 1

test_write: clean dirs start_swarm
	./speedtest.lua write 1

stop stop1:
	for i in {2..11}; do pkill -F $${i}/$${i}.pid || true; done

clean: stop1
	for i in {1..11}; do rm -rf $${i}; done
