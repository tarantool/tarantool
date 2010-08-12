use FindBin qw($Bin);
use lib "$Bin/../client/perl";

use MR::SilverBox;

$main::box = MR::SilverBox->new({ servers => $ARGV[1] || q/localhost:15013/,
                                  namespaces => [ {
                                      indexes => [ {
                                          index_name   => 'primary_id',
                                          keys         => [TUPLE_ID],
                                      }, {
                                          index_name   => 'primary_email',
                                          keys         => [TUPLE_Email],
                                      }, ],
                                      namespace     => 0,
                                      format        => 'l& SSLL',
                                      default_index => 'primary_id',
                                  } ], });
