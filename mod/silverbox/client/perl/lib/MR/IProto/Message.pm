package MR::IProto::Message;

=head1 NAME

MR::IProto::Message - iproto message

=head1 DESCRIPTION

Base class for all messages sent throught iproto protocol.

=cut

use Moose;

=head1 ATTRIBUTES

=over

=item data

Binary representation of message data.

=cut

has data => (
    is  => 'ro',
    isa => 'Value',
    lazy_build => 1,
);

=back

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

=item allow_retry

If request retry is allowed.

=cut

sub allow_retry {
    my ($self) = @_;
    return 0;
}

=back

=head1 PROTECTED METHODS

=over

=item BUILDARGS( %args | \%args | $data )

If C<$data> is passed as argument then it unpacked and object is
created based on information contained in it.

See L<Moose::Manual::Construction/BUILDARGS> for more information.

=cut

around BUILDARGS => sub {
    my $orig = shift;
    my $class = shift;
    if( @_ == 1 && !ref $_[0] ) {
        return $class->$orig( $class->_parse_data(@_) );
    }
    else {
        return $class->$orig(@_);
    }
};

=item key_attr

Class method which must return name of key attribute.

=item _build_data

You B<must> implement this method in you subclass to pack your object.
Returns packed data.

=cut

sub _build_data {
    my ($self) = @_;
    return '';
}

=item _parse_data( $data )

You B<must> implement this method in you subclass to unpack your object.
Returns hashref of attributes which will be passed to constructor.

=cut

sub _parse_data {
    my ($class, $data) = @_;
    return { data => $data };
}

=back

=head1 SEE ALSO

L<MR::IProto>.

=cut

no Moose;
__PACKAGE__->meta->make_immutable();

1;
