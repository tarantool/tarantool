benchmark:
    main: |
        # Preface

        There are lies, then there is statistics, but the first place in
        misrepresenting the truth is undoubtedly owned by benchmarks.

        Comparing Tarantool with other systems, apples to apples, is
        not strictly correct: the server networking subsystem is fully
        asynchronous and it's possible to proxy all clients via a single
        socket. In this case, responses to queries are sent as soon they
        are ready. Most production application use asynchronous and
        batched I/O with Tarantool.

        As long as the overhead of system calls and context switches is
        the single largest contributor to the cost of serving a single
        request, use of batched and multiplexed I/O produces an order of
        magnitude better results, when compared with traditional
        multi-threaded workloads. A tool we developed for our own use,
        [nosqlbench](http://github.com/mailru/nosqlbench), is utilizing
        this approach at full.

        However, to compare with the rest of the world, a standardized
        benchmarking kit is more appropriate. This is why Yahoo! Cloud
        Serving Benchmark (c) was used to produce the charts
        below. A fork of YCSB with Tarantool support is available
        [here](https://github.com/bigbes92/YCSB). Since YCSB was developed
        to compare cloud key/value servers, it provides a very narrow view
        at performance of a tested server.  For example, performance of
        secondary keys or overhead of locking (which Tarantool doesn't
        have) is not tested at all.

        # What is YCSB

        Yahoo! Cloud Serving Benchmark (c) consists of two components:

        - the client, which generates the load according to a workload type
        and analyzes latency and throughput,
        - workload files, which define a single benchmark by describing
        the size of the data set, the total amount of requests, the ratio of
        read and write queries.

        There are 6 major workload types in YCSB:

        - workload **A**, 50/50 update/read ratio, size of the data set
        is 200 000 key/value pairs,
        - workload **B**, 5/95 update/read ratio, the same size of the data set,
        - workload **C** is 100% read-only,
        - workload **D** 5/95 insert/read ratio, the read load is skewed
        towards the end of the key range,
        - workload **E**, 5/95 ratio of insert/reads over a range of 10
        records,
        - workload **F**, 95% read/modify/write, 5% read.

        For additional information on YCSB and workload types, please visit
        [YCSB official page on Github](http://github.com/brianfrankcooper/YCSB).

        All charts below were measured using 1M queries per test, averaged
        over 8 consecutive test runs.

        Configuration files for the tested systems can be found
        [here](https://github.com/bigbes92/ycsb-expand-db/tree/master/confs).

        <script src="http://ajax.googleapis.com/ajax/libs/jquery/1.9.1/jquery.min.js"></script>
        <script src="http://code.highcharts.com/highcharts.js"></script>
        <script type="text/javascript" src="highcharts.js"></script>
        <div class="b-tabs">
            <div>
                <ul class="b-tabs__list">
                    <li class="b-tabs__li b-tabs__li_on">A</li>
                    <li class="b-tabs__li">B</li>
                    <li class="b-tabs__li">C</li>
                    <li class="b-tabs__li">D</li>
                    <li class="b-tabs__li">E</li>
                    <li class="b-tabs__li">F</li>
                    <li class="b-tabs__li">LOAD</li>
                </ul>
            </div>

            <div class="b-tabs__body">
                <h3 align=center><p class="b-tabs__header">Workload A</p></h3>
                <div id="picture1"></div>
                <ul class="b-tabs__buttons">
                    <li class="b-button b-button_on">A_Read</li>
                    <li class="b-button">A_Update</li>
                </ul>
                <div class="clear"></div>
                <div id="picture2"></div>
                <p class="b-tabs__description" align="center">50/50 update/read ratio</p>
            </div>
        </div>
        <script type="text/javascript" src="tabs.js"></script>
