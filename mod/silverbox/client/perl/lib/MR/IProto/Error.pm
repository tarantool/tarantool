package MR::IProto::Error;

=head1 NAME

MR::IProto::Error - iproto error

=head1 DESCRIPTION

Instance of this class is returned instead of L<MR::IProto::Response>
if error was occured.

=cut

use Moose;

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

=cut

no Moose;
__PACKAGE__->meta->make_immutable();

1;
