package MR::IProto::Message;

=head1 NAME

MR::IProto::Message - iproto message

=head1 DESCRIPTION

Base class for all messages sent throught iproto protocol.

=cut

use Mouse;

=head1 ATTRIBUTES

=over

=item data

Binary representation of message data.

=cut

has data => (
    is  => 'ro',
    isa => 'Value',
);

=back

=head1 SEE ALSO

L<MR::IProto>.

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
