$(document).ready(function () {
  $("div>h1").remove();
  $("h2, h3, h4, h5, h6").each(
    function(i, el) {
      var icon = '<i class="fa fa-link"></i>';
      var hlink = $(el).find(".headerlink");
      var hlink_id = hlink.attr("href");
      if (typeof(hlink_id) != 'undefined') {
        $(hlink).remove();
        $(el).prepend($("<a />").addClass("headerlink").attr("href", hlink_id).html(icon));
      }
    }
  );
  $("[id^='lua-object'], [id^='lua-function'], [id^='lua-data']").each(
    function(i, el) {
      var icon = '<i class="fa fa-link"></i>';
      var hlink = $(el).find(".headerlink");
      var hlink_id = hlink.attr("href");
      if (typeof(hlink_id) != 'undefined') {
        $(hlink).remove();
        $(el).prepend($("<a />").addClass("headerlink").attr("href", hlink_id).html(icon));
      }
    }
  );
  $(".admonition.note p.first.admonition-title").each(
    function(i, el) {
      var icon = '<i class="fa fa-comments-o"></i>';
      $(el).html(icon + $(el).html());
    }
  );
  $(".admonition.warning p.first.admonition-title").each(
    function(i, el) {
      var icon = '<i class="fa fa-exclamation-triangle"></i>';
      $(el).html(icon + $(el).html());
    }
  );
  $(".b-cols_content_left").pin({containerSelector: ".b-cols_content"});
});

// vim: syntax=javascript ts=2 sts=2 sw=2 expandtab
