(function(){
var dOn = $(document);

dOn.on({
    click: function(){
        if (!($(this).hasClass('b-button_on'))){
            $('.b-button_on').removeClass('b-button_on');
            $(this).addClass('b-button_on');

            switch ($(this).html()) {
                case 'A_Read' : {
                    $('#picture2').renderChart('theme/ycsb/A_READ_latency.json');
                    break;
                }
                case 'A_Update' : {
                    $('#picture2').renderChart('theme/ycsb/A_UPDATE_latency.json');
                    break;
                }
                case 'B_Read' : {
                    $('#picture2').renderChart('theme/ycsb/B_READ_latency.json');
                    break;
                }
                case 'B_Update' : {
                    $('#picture2').renderChart('theme/ycsb/B_UPDATE_latency.json');
                    break;
                }
                case 'C_Read' : {
                    $('#picture2').renderChart('theme/ycsb/C_READ_latency.json');
                    break;
                }
                case 'D_Insert' : {
                    $('#picture2').renderChart('theme/ycsb/D_INSERT_latency.json');
                    break;
                }
                case 'D_Read' : {
                    $('#picture2').renderChart('theme/ycsb/D_READ_latency.json');
                    break;
                }
                case 'E_Insert' : {
                    $('#picture2').renderChart('theme/ycsb/E_INSERT_latency.json');
                    break;
                }
                case 'E_Scan' : {
                    $('#picture2').renderChart('theme/ycsb/E_SCAN_latency.json');
                    break;
                }
                case 'F_Read' : {
                    $('#picture2').renderChart('theme/ycsb/F_READ_latency.json');
                    break;
                }
                case 'F_Read-Modify-Write' : {
                    $('#picture2').renderChart('theme/ycsb/F_READ-MODIFY-WRITE_latency.json');
                    break;
                }
                case 'F_Update' : {
                    $('#picture2').renderChart('theme/ycsb/F_UPDATE_latency.json');
                    break;
                }
                case 'LOAD_Insert' : {
                    $('#picture2').renderChart('theme/ycsb/LOAD_INSERT_latency.json');
                    break;
                }
            }

        }
    }
}, '.b-button');

dOn.on({
    click: function(){
        if (!($(this).hasClass('b-tabs__li_on'))){
            $('.b-tabs__li_on').removeClass('b-tabs__li_on');
            $(this).addClass('b-tabs__li_on');

            $('.b-button').remove();
            var head = $('.b-tabs__header'), ul = $('.b-tabs__buttons'), li, desc = $('.b-tabs__description');

            switch ($(this).html()) {
                case 'A' : {
                    head.html('Workload A')
                    $('#picture1').renderChart('theme/ycsb/A_throughput.json');
                    $('#picture2').renderChart('theme/ycsb/A_READ_latency.json');

                    li = $('<li class="b-button b-button_on">A_Read</li>');
                    ul.append(li);
                    li = $('<li class="b-button">A_Update</li>');
                    ul.append(li);

                    desc.html('50/50 update/read ratio');

                    break;
                }
                case 'B' : {
                    head.html('Workload B')
                    $('#picture1').renderChart('theme/ycsb/B_throughput.json');
                    $('#picture2').renderChart('theme/ycsb/B_READ_latency.json');

                    li = $('<li class="b-button b-button_on">B_Read</li>');
                    ul.append(li);
                    li = $('<li class="b-button">B_Update</li>');
                    ul.append(li);

                    desc.html('5/95 update/read ratio');

                    break;
                }
                case 'C' : {
                    head.html('Workload C')
                    $('#picture1').renderChart('theme/ycsb/C_throughput.json');
                    $('#picture2').renderChart('theme/ycsb/C_READ_latency.json');

                    li = $('<li class="b-button b-button_on">C_Read</li>');
                    ul.append(li);

                    desc.html('100% read-only');

                    break;
                }
                case 'D' : {
                    head.html('Workload D')
                    $('#picture1').renderChart('theme/ycsb/D_throughput.json');
                    $('#picture2').renderChart('theme/ycsb/D_READ_latency.json');

                    li = $('<li class="b-button b-button_on">D_Read</li>');
                    ul.append(li);
                    li = $('<li class="b-button">D_Insert</li>');
                    ul.append(li);

                    desc.html('5/95 insert/read ratio, the read load is skewed towards the end of the key range');

                    break;
                }
                case 'E' : {
                    head.html('Workload E')
                    $('#picture1').renderChart('theme/ycsb/E_throughput.json');
                    $('#picture2').renderChart('theme/ycsb/E_INSERT_latency.json');

                    li = $('<li class="b-button b-button_on">E_Insert</li>');
                    ul.append(li);
                    li = $('<li class="b-button">E_Scan</li>');
                    ul.append(li);

                    desc.html('5/95 ratio of insert/reads over a range of 10 records');

                    break;
                }
                case 'F' : {
                    head.html('Workload F')
                    $('#picture1').renderChart('theme/ycsb/F_throughput.json');
                    $('#picture2').renderChart('theme/ycsb/F_READ_latency.json');

                    li = $('<li class="b-button b-button_on">F_Read</li>');
                    ul.append(li);
                    li = $('<li class="b-button">F_Read-Modify-Write</li>');
                    ul.append(li);
                    li = $('<li class="b-button">F_Update</li>');
                    ul.append(li);

                    desc.html('95% read/modify/write, 5% read');

                    break;
                }
                case 'LOAD' : {
                    head.html('Insert only')
                    $('#picture1').renderChart('theme/ycsb/LOAD_throughput.json');
                    $('#picture2').renderChart('theme/ycsb/LOAD_INSERT_latency.json');

                    li = $('<li class="b-button b-button_on">L_Insert</li>');
                    ul.append(li);

                    desc.html('');

                    break;
                }
            }
        }
    }
}, '.b-tabs__li');
})();

(function(){
var dOn = $(document);
dOn.ready(function() {
    $('#picture1').renderChart('theme/ycsb/A_throughput.json');
    $('#picture2').renderChart('theme/ycsb/A_READ_latency.json');
});
})();
