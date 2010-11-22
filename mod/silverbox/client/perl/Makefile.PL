use ExtUtils::MakeMaker;

WriteMakefile(
    NAME          => "MR::SilverBox",
    VERSION_FROM  => "lib/MR/SilverBox.pm",
    MAKEFILE      => 'Makefile',
    PREREQ_PM     => {
        'Scalar::Util'      => 0,
        'AnyEvent'          => 0,
        'List::Util'        => 0,
        'List::MoreUtils'   => 0,
        'Moose'             => 0,
        'String::CRC32'     => 0,
        'Time::HiRes'       => 0,
        'Exporter'          => 0,
    },
);
