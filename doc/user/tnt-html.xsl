<?xml version='1.0'?> 
<xsl:stylesheet  
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0"
    xmlns:xslthl="http://xslthl.sf.net">

<xsl:import href="http://docbook.sourceforge.net/release/xsl-ns/current/html/docbook.xsl"/> 

<xsl:import href="html-highlight.xsl"/>

<xsl:param name="generate.toc" select="'book toc'"/>
<xsl:param name="html.stylesheet" select="'tnt.css'"/> 
<xsl:param name="highlight.source" select="1"/>
<xsl:param name="highlight.xslthl.config">file:////usr/share/xml/docbook/stylesheet/docbook-xsl-ns/highlighting/xslthl-config.xml</xsl:param>

<!-- Add Google Analytics to the generated html-->

<xsl:template name="user.head.content">
  <script type="text/javascript">

    var _gaq = _gaq || [];
    _gaq.push(['_setAccount', 'UA-22120502-1']);
    _gaq.push(['_trackPageview']);

    (function() {
      var ga = document.createElement('script'); ga.type = 'text/javascript'; ga.async = true;
      ga.src = ('https:' == document.location.protocol ? 'https://ssl' : 'http://www') + '.google-analytics.com/ga.js';
      var s = document.getElementsByTagName('script')[0]; s.parentNode.insertBefore(ga, s);
    })();

  </script>
</xsl:template>
</xsl:stylesheet>
