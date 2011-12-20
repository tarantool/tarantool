use ExtUtils::MakeMaker;

WriteMakefile(
    NAME          => "MR::Tarantool",
    VERSION_FROM  => "lib/MR/Tarantool/Box.pm",
    MAKEFILE      => 'Makefile',
    PREREQ_PM     => {
        'Scalar::Util'      => 0,
        'AnyEvent'          => 0,
        'List::Util'        => 0,
        'List::MoreUtils'   => 0,
        'Mouse'             => 0,
        'MRO::Compat'       => 0,
        'String::CRC32'     => 0,
        'Time::HiRes'       => 0,
        'Exporter'          => 0,
        'Class::Singleton'  => 0,
    },
);
