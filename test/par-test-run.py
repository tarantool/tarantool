#!/usr/bin/env python2

import os
import re
import sys
import copy
import glob
import time
import uuid
import random
import itertools
import traceback
import ConfigParser

import multiprocessing
from multiprocessing import Process as MProcess
from multiprocessing.managers import BaseManager as MBaseManager

from Queue import Empty as QueueEmpty
from Queue import Full as QueueFull

from lib.test import FilteredStream
from lib.preprocessor import TestState
from lib.tarantool_server import TarantoolServer

import logging
logger = multiprocessing.log_to_stderr()
logger.setLevel(logging.INFO)

import pickle

from StringIO import StringIO

STATUS_TABLE = {
    0: "pass",
    1: "skip",
    2: "NEW",
    3: "fail"
}

class Parallel_Manager(MBaseManager): pass
Parallel_Manager.register('Queue', multiprocessing.Queue)

class Parallel_Process(MProcess):
    def __init__(self, **kwargs):
        if kwargs and 'target' in kwargs:
            del kwargs['target']
        super(Parallel_Process, self).__init__(**kwargs)

    def run(self):
        queuein  = self._args[0]
        queueout = self._args[1]
        while True:
            logger.debug("||_process.run > getting job")
            obj = queuein.get()
            logger.debug("||_process.run > ok, it's great")
            assert obj
            assert len(obj) == 2
            assert callable(obj[0])
            assert isinstance(obj[1], (tuple, list))
            retv = obj[0](*obj[1])
            logger.debug("||_process.run > job is done, let's put into outqueue")
            queueout.put(retv)

class Parallel_PoolException(Exception):
    def __init__(self, message):
        self.message = message

class Parallel_Pool(object):
    DEFAULT = -1
    INITED = 0
    POPULATED = 1
    STARTED = 2
    ENDED = 3

    def __init__(self, **kwargs):
        self.status   = Parallel_Pool.DEFAULT
        self.pool     = []
        self.number   = kwargs.get('processes', 1)
        self.manager  = Parallel_Manager()
        self.manager.start()
        self.queuein  = self.manager.Queue()
        self.queueout = self.manager.Queue()
        self.jobs_in  = 0
        self.jobs_out = 0
        self.jobs_end = False
        self.status   = Parallel_Pool.INITED
        self._populate()

    def _populate(self):
        assert(self.status == Parallel_Pool.INITED)
        for i in xrange(self.number):
            kwargs = {
            }
            self.pool.append(Parallel_Process(
                group    = None,
                name     = 'Worker-%d' % i,
                args     = [
                    self.queuein,
                    self.queueout
                ]
            ))
        self.status = Parallel_Pool.POPULATED

    def _repopulate(self):
        assert(self.status >= Parallel_Pool.STARTED)
        logger.debug('||_pool.repopulate > Begin repopulation')
        for n, proc in enumerate(self.pool):
            if not proc.is_alive() and self.status != Parallel_Pool.ENDED:
                logger.debug("Manager: Process %s is dead (code %s). Recreating",
                        repr(proc.name), proc.exitcode)
                self.pool[n] = Parallel_Process(
                        group     = None,
                        name      = proc.name,
                        args      = [
                            self.queuein,
                            self.queueout
                        ]
                )
                self.jobs_out += 1
        logger.debug('||_pool.repopulate > Ending repopulation')
        return 0

    def fill(self, iterable=None):
        logger.debug('||_pool.fill > Entering')
        assert(self.status > Parallel_Pool.INITED and self.status < Parallel_Pool.ENDED)
        if iterable == None:
            raise Parallel_PoolException("Iterable must be defined \
                    for '||_pool.fill'")
        jobs = iterable
        target = 0
        while True:
            self._repopulate()
            try:
                while (not self.queuein.full()   and
                       target < 10               and
                       not self.queueout.full()):
                    logger.debug("I'll put a job now")
                    job = iterable.next()
                    self.queuein.put(job)
                    self.jobs_in += 1
                    target += 1
                logger.debug("||_pool.fill > While stopped")
            except StopIteration:
                logger.debug("||_pool.fill > StopIteration")
                self.jobs_end = True
                raise StopIteration
            yield
            target = 0

    def run(self):
        for proc in self.pool:
            proc.start()
        self.status = Parallel_Pool.STARTED
        return Parallel_Iterator(self)

class Parallel_Iterator(object):
    def __init__(self, pool):
        self.pool = pool

    def __iter__(self):
        return self

    def __next__(self):
        return self.next()

    def next(self, timeout=None):
        if self.pool.jobs_end == True and \
                (self.pool.jobs_in == self.pool.jobs_out):
            raise StopIteration()
        else:
            ans = self.pool.queueout.get(block=True, timeout=timeout)
            self.pool.jobs_out += 1
            return ans

class Parallel_FilteredStream(object):
    def __init__(self):
        self.stream = StringIO()
        self.filters = []

    def write(self, fragment):
        skipped = False
        for line in fragment.splitlines(True):
            original_len = len(line.strip())
            for pattern, replacement in self.filters:
                line = pattern.sub(replacement, line)
                skipped = original_len and not line.strip()
                if skipped:
                    break
            if not skipped:
                self.stream.write(line)

    def push_filter(self, pattern, replacement):
        self.filters.append([re.compile(pattern), replacement])

    def pop_filter(self):
        self.filters.pop()

    def clear_all_filters(self):
        self.filters = []

    def close(self):
        self.clear_all_filters()

    def getvalue(self):
        return self.stream.getvalue()

class TestStatus(object):
    def __init__(self, status):
        if isinstance(status, basestring):
            status = status.lower()
            if (status == "pass"):
                status = 0
            elif (status == "skip"):
                status = 1
            elif (status == "new"):
                status = 2
            else:
                status = 3
        self.status = status
        self.message = ''

    def set_message(self, msg):
        self.message = msg
        return self

    def generate_printable(self):
        pass

class Supervisor(object):
    def __init__(self):
        self.name      = 'parallel'
        self.server    = TarantoolServer({
            'core'  : 'tarantool',
            'script': 'parallel/box.lua',
            'vardir': 'var_parallel'
        })
        self.pool = None
        self.iterator = None

    def search_tests(self):
        self.tests  = []
        self.tests += [Parallel_PythonTest(k) \
                for k in sorted(glob.glob(os.path.join(self.name, "*.test.py" )))]
#        self.tests += [Parallel_LuaTest(k)    \
#                for k in sorted(glob.glob(os.path.join(self.name, "*.test.lua")))]

    def take_rand(self):
        while True:
            sql = self.server.sql.ret_copy()
            admin = self.server.admin.ret_copy()
            yield [random.choice(self.tests), [sql, admin]]

    def run(self, jobs):
        self.search_tests()
        self.pool = Parallel_Pool(processes = jobs)
        self.iterator = self.pool.run()
        self.filler = self.pool.fill(self.take_rand())
        try:
            self.server.cleanup()
            logger.info("Tarantool.Instance > Server cleanuped")
            logger.info("Tarantool.Instance > Server's path: %s", self.server.binary)
            self.server.deploy()
            logger.info("Tarantool.Instance > Server deployed")
            try:
                while True:
                    self.filler.next()
                    logger.debug("BigBrother.run > Jobs filled %d %d" %
                            (self.pool.queuein.qsize(), self.pool.queueout.qsize()))
                    while True:
                        try:
                            logger.debug("BigBrother.run > waiting for task")
                            task = self.iterator.next(1)
                            logger.debug("BigBrother.run > took task")
                            if task is None:
                                logger.info('>>>> Test return NONE')
                                continue
                            stat = task.get_status()
                            if stat.status != 3:
                                logger.info('>>>> Test %s finished' % repr(task.name))
                            else:
                                logger.info('>>>> Test %s failed with %s' %
                                        (repr(task.name), stat.message))
                        except (QueueEmpty, StopIteration):
                            break
            except StopIteration:
                pass
        finally:
            self.server.stop()
            logger.info("Tarantool.Instance > Server stopped")

class Parallel_Test(object):
    def __init__(self, name):
        rg = re.compile('.test.*')
        self.name   = name
        self.reject = None
        self.id     = None
        logger.debug("||_Test.__init__ > Entering test '%s'" % self.name)
        self.result = rg.sub('.result', name)
        self.is_executed     = False
        self.is_executed_ok  = False
        self.is_equal_result = False
        self.is_new          = False

    def passed(self):
        return self.is_executed and self.is_executed_ok and self.is_equal_result

    def execute(self, server, sql, admin):
        pass

    def run(self, sql, admin):
        self.id     = uuid.uuid1().get_hex().replace('-', '')[0:6]
        self.reject = re.sub('.test.*', '.reject_%s' % self.id, self.name)
        self.diagnostics = "unknown"
        save_stdout = sys.stdout
        self.test_stdout = Parallel_FilteredStream()
        try:
            sys.stdout = self.test_stdout
            logger.debug("Entering")
            self.execute(sql, admin)
            self.is_executed_ok = True
        except Exception as e:
            logger.error("||_Test.run > Exception '%s' was thrown for '%s'" % (type(e), str(e)))
            logger.error(traceback.format_exc())
            with open(self.reject, 'a') as reject:
                traceback.print_exc(e, reject)
            self.diagnostics = str(e)
        finally:
            sys.stdout = save_stdout
        self.is_executed = True

        if not os.path.isfile(self.result):
            self.is_new = True
            with open(self.result, 'w') as result:
                result.write(self.test_stdout.getvalue())

        if self.is_executed_ok and not self.is_new:
            self.is_equal_result = \
                    (open(self.result, 'r').read() == self.test_stdout.getvalue())

        if not self.is_equal_result:
            with open(self.reject, 'a') as reject:
                reject.write(self.test_stdout.getvalue())

        return self

    def get_status(self):
        if self.is_executed_ok and self.is_equal_result:
            return TestStatus("pass")
        elif (self.is_executed_ok and not self.is_equal_result and self.is_new):
            return TestStatus("new")
        else:
            where = ""
            if not self.is_executed_ok:
                where = "test execution aborted, reason '{0}'".format(self.diagnostics)
            elif not self.is_equal_result:
                where = "wrong test output"
            return TestStatus("fail").set_message(where)

    def __call__(self, sql, admin):
        try:
            logger.debug("||_Test.__call__ > Entering test '%s'" % self.name)
            return self.run(sql, admin)
        except Exception as e:
            logger.error("||_Test.__call__ > Exception '%s' was thrown for '%s'" % (type(e), str(e)))
            logger.error(traceback.format_exc())

class Parallel_FuncTest(Parallel_Test):
    def execute(self, sql, admin):
        execfile(self.name, dict(locals(), sql=sql, admin=admin))

class Parallel_PythonTest(Parallel_FuncTest): pass

if __name__ == '__main__':
    TarantoolServer.find_exe('..')
    sup = Supervisor()
    sup.run(1)
