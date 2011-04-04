package MR::IProto::Role::Debuggable;

=head1 NAME

=head1 DESCRIPTION

=cut

use Mouse::Role;

=item debug

Deug level.

=cut

has debug => (
    is  => 'rw',
    isa => 'Int',
    default => 0,
);

=item debug_cb

Callback which is called when debug message is written.

=cut

has debug_cb => (
    is  => 'rw',
    isa => 'CodeRef',
    lazy_build => 1,
);

=item dump_no_ints

Skip print of integers in dump.

=cut

has dump_no_ints => (
    is  => 'ro',
    isa => 'Bool',
);

sub _build_debug_cb {
    my ($self) = @_;
    my $prefix = ref $self;
    return sub {
        my ($msg) = @_;
        chomp $msg;
        warn sprintf "$prefix: $msg\n";
        return;
    };
}

sub _debug {
    $_[0]->debug_cb->($_[1]);
}

sub _debug_dump {
    my ($self, $msg, $datum) = @_;
    unless($self->dump_no_ints) {
        $msg .= join(' ', unpack('L*', $datum));
        $msg .= ' > ';
    }
    $msg .= join(' ', map { sprintf "%02x", $_ } unpack("C*", $datum));
    $self->_debug($msg);
    return;
}

no Mouse::Role;

1;
