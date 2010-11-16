package MR::IProto::Message;

=head1 NAME

=head1 DESCRIPTION

=cut

use Moose;

has key => (
    is  => 'ro',
    isa => 'Str',
);

has msg => (
    is  => 'ro',
    isa => 'Int',
    required => 1,
);

has data => (
    is  => 'ro',
    isa => 'ArrayRef',
    predicate => 'has_data',
);

has pack => (
    is  => 'ro',
    isa => 'Str',
    default => 'L*',
    lazy    => 1,
);

has sync => (
    is  => 'ro',
    isa => 'Int',
    default => sub { int(rand 0xffffffff) },
    lazy    => 1,
);

has header => (
    is  => 'ro',
    isa => 'Value',
    lazy_build => 1,
);

has payload => (
    is  => 'ro',
    isa => 'Value',
    lazy_build => 1,
);

has message => (
    is  => 'ro',
    isa => 'Value',
    lazy_build => 1,
);

has no_reply => (
    is  => 'ro',
    isa => 'Bool',
);

has unpack => (
    is  => 'ro',
    isa => 'CodeRef',
    lazy_build => 1,
);

=head1 PUBLIC METHODS

=over

=cut



=back

=head1 PROTECTED METHODS

=over

=cut

sub BUILD {
    my ($self) = @_;
    die "Message data or payload must be specified" unless $self->has_data() || $self->has_payload();
    return;
}

sub _build_header {
    my ($self) = @_;
    return pack('LLL', $self->msg, length($self->payload), $self->sync);
}

sub _build_payload {
    my ($self) = @_;
    return pack($self->pack, @{$self->data});
}

sub _build_message {
    my ($self) = @_;
    return $self->header . $self->payload;
}

sub _build_unpack {
    my ($self) = @_;
    return $self->no_reply ? sub { 0 } : undef;
}

=back

=cut

no Moose;
__PACKAGE__->meta->make_immutable();

1;
