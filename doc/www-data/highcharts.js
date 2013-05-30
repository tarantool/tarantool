(function($){
	$.fn.renderChart = function(url, options) {
		var batch = $.fn.renderChart.nextBatch;
	    $.fn.renderChart.nextBatch += 1;
		var objects = this;
		objects.data('renderChart_batch', batch);
		options = options || {};
		$.ajax({
			url: url,
			dataType: 'json',
			success: function(config) {
				config = $.extend(true, config, options);
				config.chart = config.chart || {};
				if (config.tooltip && config.tooltip.formatter){
					config.tooltip.formatter = Function(config.tooltip.formatter);
				}
				objects.each(function() {
					if ($(this).data('renderChart_batch') == batch) {
						config.chart.renderTo = this;
						new Highcharts.Chart(config);
					}
				});
			}
		});
	return this;
	};
	$.fn.renderChart.nextBatch = 0;
})(jQuery);
