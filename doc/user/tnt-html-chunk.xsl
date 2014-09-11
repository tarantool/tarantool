<?xml version='1.0'?>

<xsl:stylesheet
    xmlns:xsl="http://www.w3.org/1999/XSL/Transform" version="1.0"
    xmlns:xslthl="http://xslthl.sf.net">
  <xsl:import href="http://docbook.sourceforge.net/release/xsl-ns/current/html/chunk.xsl"/>
  <xsl:import href="http://docbook.sourceforge.net/release/xsl-ns/current/html/highlight.xsl"/>
  <xsl:param name="highlight.source" select="1"/>
  <xsl:param name="highlight.xslthl.config">file:////usr/share/xml/docbook/stylesheet/docbook-xsl-ns/highlighting/xslthl-config.xml</xsl:param>
  <xsl:param name="use.id.as.filename" select="1"></xsl:param>
  <xsl:param name="suppress.header.navigation" select="1"></xsl:param>
  <xsl:param name="generate.toc">
    chapter toc
    book toc
  </xsl:param>
  <xsl:param name="html.stylesheet" select="'tnt.css'"/>
  <xsl:param name="collect.xref.targets">all</xsl:param>
</xsl:stylesheet>
