package MR::IProto::Request;

=head1 NAME

MR::IProto::Request - request message

=head1 DESCRIPTION

Base class for all request messages.

=cut

use Mouse;
extends 'MR::IProto::Message';

has '+data' => (
    lazy_build => 1,
);

=head1 PUBLIC METHODS

=over

=item key

Returns key value.
You must reimplement this method in your subclass to use this feature.

=cut

sub key {
    return undef;
}

=item retry

If request retry is allowed.

=cut

sub retry {
    return 0;
}

=back

=head1 PROTECTED METHODS

=over

=item key_attr

Class method which must return name of key attribute.

=item _build_data

You B<must> implement this method in you subclass to pack your object.
Returns packed data.

=cut

sub _build_data {
    return '';
}

=back

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
