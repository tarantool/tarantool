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
    my $args = @_ == 1 ? $_[0] : { @_ };
    if( exists $args->{data} ) {
        my $parsed_args = $class->_parse_data($args->{data});
        my $tail = delete $parsed_args->{data};
        warn "Not all data was parsed" if defined $tail && length $tail;
        @$args{ keys %$parsed_args } = values %$parsed_args;
        return $class->$orig($args);
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
