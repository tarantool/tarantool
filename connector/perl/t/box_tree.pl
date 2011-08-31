use strict;
use warnings;
use Data::Dumper;
BEGIN {
    sub mPOP::Config::GetValue ($) {
        die;
    }
}
use My::SilverBox ();

use Test::More qw/no_plan/;
use Test::Exception;

use constant ILL_PARAM => qr/Error 00000202: Illegal parametrs/;

my $box;
my $server = q/alei7:13013/;

sub cleanup ($) {
    my ($id) = @_;
    ok defined $box->Delete($id), 'delete of possible existing record';
    ok $box->Delete($id) == 0, 'delete of non existing record';
}


$box = My::SilverBox->new({servers => $server, tuple_format_0 => q/l& SSLL/, tuple_format_1 => q/l& SSLL/});
ok $box->isa('My::SilverBox'), 'connect';

for (my $from = 100; $from < 200; ++$from) {
	ok $box->Insert($from, "$from\@test.mail.ru", 1, 2, 3, 4), 'insert';
}

my $from = 100;
while (1) {
	my @ans;
	my $num = int(rand 20) + 1;

	if ($from != 200) {
		ok @ans = $box->Select($from, {next_rows => $num}), 'select';
	} else {
		ok not (defined $box->Select($from, {next_rows => $num})), 'select';
	}

	last if scalar @ans == 0;

	my @tuples = ();
	for (my $i = 0; $i < scalar @ans; ++$i) {
		push @tuples, [$from, "$from\@test.mail.ru", 1, 2, 3, 4];
		$from++;
	}
	is_deeply \@ans, \@tuples, 'select deeply'
}
