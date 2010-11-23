package MR::IProto::Error;

=head1 NAME

MR::IProto::Error - iproto error

=head1 DESCRIPTION

Instance of this class is returned instead of L<MR::IProto::Response>
if error was occured.

=cut

use Mouse;

=head1 PUBLIC ATTRIBUTES

=over

=item error

Error string.

=cut

has error => (
    is  => 'ro',
    isa => 'Str',
    required => 1,
);

=item errno

Integer value of C<$!>.

=cut

has errno => (
    is  => 'ro',
    isa => 'Int',
);

=item request

Instance of L<MR::IProto::Request>.

=cut

has request => (
    is  => 'ro',
    isa => 'MR::IProto::Request',
    required => 1,
);

=back

=head1 PUBLIC METHODS

=over

=item is_error

Always returns true.

=cut

sub is_error {
    return 1;
}

=item error_message

Error message text.

=cut

sub error_message {
    my ($self) = @_;
    return $self->error;
}

=back

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
