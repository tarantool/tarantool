use ExtUtils::MakeMaker;

use 5.005003;

WriteMakefile(
#    'LIB' => '/usr/local/mpop/lib',
#    dist	=> {
#	            PREOP	=> 'pod2text Lemmatizer.pm | tee README >$(DISTVNAME)/README; chmod -R u=rwX,go=rX . ;',
#	            COMPRESS	=> 'gzip -9v',
#	            SUFFIX	=> '.gz',
#	           },
    NAME => "MR::SilverBox",
    VERSION_FROM => "MR/SilverBox.pm",
    MAKEFILE  => 'Makefile',
#    CCFLAGS   => `pkg-config --cflags lemmatizer`,
#    LIBS      => `pkg-config  --libs lemmatizer`,
);
