package MR::IProto::Response;

=head1 NAME

MR::IProto::Response - response message

=head1 DESCRIPTION

Base class for all response messages.

=cut

use Moose;
extends 'MR::IProto::Message';

=head1 PUBLIC ATTRIBUTES

=over

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

=item retry

Is request retry must be done.

=cut

sub retry {
    my ($self) = @_;
    return 0;
}

=back

=cut

no Moose;
__PACKAGE__->meta->make_immutable();

1;
