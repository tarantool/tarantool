(function(){
    var dOn = $(document);

    dOn.on({
        click: function(event){
            event.preventDefault();
            link = $(this).children('a')
            if (!(link.hasClass('p-active'))) {
                $('.b-benchmark-catalog .b-switcher-item-url.p-active').removeClass('p-active');
                link.addClass('p-active');
                var title = $('.b-benchmark-type-title');
                var image = $('#b-benchmark-grapf-image');
                title.html('Workload ' + link.html());
                image.renderChart('/ycsb/' + link.html() + '_throughput.json');
                $('.b-benchmark-type .b-switcher-item-url.p-active').removeClass('p-active');
                $('.b-benchmark-type .b-switcher-item-url').first().addClass('p-active');
            }
        }
    }, '.b-benchmark-catalog .b-switcher-item')

    dOn.on({
        click: function(event){
            event.preventDefault();
            link = $(this).children('a')
            if (!(link.hasClass('p-active'))) {
                $('.b-benchmark-type .b-switcher-item-url.p-active').removeClass('p-active');
                link.addClass('p-active');
                var title = $('.b-benchmark-type-title').html();
                var image = $('#b-benchmark-grapf-image');

                var table = {
                    'A': 'READ',
                    'B': 'READ',
                    'C': 'READ',
                    'D': 'READ',
                    'E': 'SCAN',
                    'F': 'READ'
                };
                var letter = '';
                letter = title.replace('Workload ', '');
                if (link.html() == 'Latency') {
                    image.renderChart('/ycsb/' + letter +
                        '_' + table[letter] + '_latency.json');
                } else if (link.html() == 'Throughput') {
                    image.renderChart('/ycsb/' + letter +
                        '_throughput.json');
                }
            }
        }
    }, '.b-benchmark-type .b-switcher-item')

    dOn.ready(function(event) {
        var image = $('#b-benchmark-grapf-image');
        image.renderChart('/ycsb/A_throughput.json');
    });
})();
