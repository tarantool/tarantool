package MR::IProto::Request;

=head1 NAME

MR::IProto::Request - request message

=head1 DESCRIPTION

Base class for all request messages.

=cut

use Mouse;
extends 'MR::IProto::Message';

=head1 PUBLIC METHODS

=over

=item key

Returns key value.
You must implement L</key_attr> method in your subclass to use this feature.

=cut

sub key {
    my ($self) = @_;
    return undef unless $self->can('key_attr');
    my $method = $self->key_attr;
    return $self->$method();
}

=item retry

If request retry is allowed.

=cut

sub retry {
    my ($self) = @_;
    return 0;
}

=back

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
