(function(){
    var dOn = $(document);

    dOn.on({
        click: function(){
            event.preventDefault();
            link = $(this).children('a')
            if (!(link.hasClass('p-active'))) {
                $('.b-benchmark-catalog .b-switcher-item-url.p-active').removeClass('p-active');
                link.addClass('p-active');
                var title = $('.b-benchmark-type-title');
                var image = $('#b-benchmark-grapf-image');

                switch (link.html()) {
                    case 'A' : {
                        title.html('Workload A');
                        image.renderChart('/ycsb/A_throughput.json');
                        break;
                    } case 'B' : {
                        title.html('Workload B');
                        image.renderChart('/ycsb/B_throughput.json');
                        break;
                    } case 'C' : {
                        title.html('Workload C');
                        image.renderChart('/ycsb/C_throughput.json');
                        break;
                    } case 'D' : {
                        title.html('Workload D');
                        image.renderChart('/ycsb/D_throughput.json');
                        break;
                    } case 'E' : {
                        title.html('Workload E');
                        image.renderChart('/ycsb/E_throughput.json');
                        break;
                    } case 'F' : {
                        title.html('Workload F');
                        image.renderChart('/ycsb/F_throughput.json');
                        break;
                    } case 'LOAD' : {
                        title.html('Insert Only');
                        image.renderChart('/ycsb/LOAD_throughput.json');
                        break;
                    }
                }
                $('.b-benchmark-type .b-switcher-item-url.p-active').removeClass('p-active');
                $('.b-benchmark-type .b-switcher-item-url').first().addClass('p-active');
            }
        }
    }, '.b-benchmark-catalog .b-switcher-item')

    dOn.on({
        click: function(){
            event.preventDefault();
            link = $(this).children('a')
            if (!(link.hasClass('p-active'))) {
                $('.b-benchmark-type .b-switcher-item-url.p-active').removeClass('p-active');
                link.addClass('p-active');
                var title = $('.b-benchmark-type-title').html();
                var image = $('#b-benchmark-grapf-image');

                if (title == 'Workload A' && link.html() == 'Latency') {
                    image.renderChart('/ycsb/A_READ_latency.json');
                } else if (title == 'Workload A' && link.html() == 'Throughput') {
                    image.renderChart('/ycsb/A_throughput.json')
                } else if (title == 'Workload B' && link.html() == 'Latency') {
                    image.renderChart('/ycsb/B_READ_latency.json');
                } else if (title == 'Workload B' && link.html() == 'Throughput') {
                    image.renderChart('/ycsb/B_throughput.json')
                } else if (title == 'Workload C' && link.html() == 'Latency') {
                    image.renderChart('/ycsb/C_READ_latency.json');
                } else if (title == 'Workload C' && link.html() == 'Throughput') {
                    image.renderChart('/ycsb/C_throughput.json')
                } else if (title == 'Workload D' && link.html() == 'Latency') {
                    image.renderChart('/ycsb/D_READ_latency.json');
                } else if (title == 'Workload D' && link.html() == 'Throughput') {
                    image.renderChart('/ycsb/D_throughput.json')
                } else if (title == 'Workload E' && link.html() == 'Latency') {
                    image.renderChart('/ycsb/E_SCAN_latency.json');
                } else if (title == 'Workload E' && link.html() == 'Throughput') {
                    image.renderChart('/ycsb/E_throughput.json')
                } else if (title == 'Workload F' && link.html() == 'Latency') {
                    image.renderChart('/ycsb/F_READ_latency.json');
                } else if (title == 'Workload F' && link.html() == 'Throughput') {
                    image.renderChart('/ycsb/F_throughput.json')
                } else if (title == 'Insert Only' && link.html() == 'Latency') {
                    image.renderChart('/ycsb/LOAD_INSERT_latency.json');
                } else if (title == 'Insert Only' && link.html() == 'Throughput') {
                    image.renderChart('/ycsb/LOAD_throughput.json')
                }

            }
        }
    }, '.b-benchmark-type .b-switcher-item')

    dOn.ready(function() {
        var image = $('#b-benchmark-grapf-image');
        image.renderChart('/ycsb/A_throughput.json');
    });
})();
