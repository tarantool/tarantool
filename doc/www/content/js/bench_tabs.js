function create_li(arr) {
    var tmparr = $.map(arr, function(elem, index){
        var active = ''
        if (index == 0) {
            active = ' p-active';
        }
        return ('<li class="b-switcher-item"><a href="#" ' +
         'class="b-switcher-item-urlACTIVE">OP Latency</a>'+
         '</li>').replace('OP', elem).replace('ACTIVE', active)
    });
    return tmparr.join('');
}

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
                var image_1 = $('#b-benchmark-grapf-image_1');
                var image_2 = $('#b-benchmark-grapf-image_2');
                var ul = $('.b-benchmark-type .b-switcher');
                var table = {
                    'A': ['READ', 'UPDATE'],
                    'B': ['READ', 'UPDATE'],
                    'C': ['READ'],
                    'D': ['READ', 'INSERT'],
                    'E': ['SCAN', 'INSERT'],
                    'F': ['READ', 'READ-MODIFY-WRITE', 'UPDATE']
                };
                title.html('Workload ' + link.html());
                image_1.renderChart('/ycsb/' + link.html() + '_throughput.json');
                image_2.renderChart('/ycsb/' + link.html() + '_' + table[link.html()][0] + '_latency.json');
                ul.html(create_li(table[link.html()]));
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
                var image = $('#b-benchmark-grapf-image_2');
                image.renderChart("/ycsb/"+ title.replace("Workload ", "") + "_" +
                                    link.html().replace(" Latency", "") + "_latency.json")
            }
        }
    }, '.b-benchmark-type .b-switcher-item')

    dOn.ready(function(event) {
        $('#b-benchmark-grapf-image_1').renderChart('/ycsb/A_throughput.json');
        $('#b-benchmark-grapf-image_2').renderChart('/ycsb/A_READ_latency.json');
    });
})();
