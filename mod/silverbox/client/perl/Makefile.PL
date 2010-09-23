use ExtUtils::MakeMaker;

WriteMakefile(
    NAME          => "MR::SilverBox",
    VERSION_FROM  => "MR/SilverBox.pm",
    MAKEFILE      => 'Makefile',
    PREREQ_PM     => {
        'Scalar::Util'      => 0,
        'List::Util'        => 0,
        'List::MoreUtils'   => 0,
        'Time::HiRes'       => 0,
        'String::CRC32'     => 0,
        'Exporter'          => 0,
        'Fcntl'             => 0,
    },
);
