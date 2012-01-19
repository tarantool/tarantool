package MR::IProto::Response;

=head1 NAME

MR::IProto::Response - response message

=head1 DESCRIPTION

Base class for all response messages.

=cut

use Mouse;
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
    return 0;
}

=back

=head1 PROTECTED METHODS

=over

=item BUILDARGS( %args | \%args | $data )

If C<$data> is passed as argument then it unpacked and object is
created based on information contained in it.

See L<Mouse::Manual::Construction/BUILDARGS> for more information.

=cut

around BUILDARGS => sub {
    my ($orig, $class, %args) = @_;
    if( exists $args{data} ) {
        my $parsed_args = $class->_parse_data($args{data});
        my $tail = delete $parsed_args->{data};
        warn "Not all data was parsed" if defined $tail && length $tail;
        return $class->$orig(%$parsed_args, %args);
    }
    else {
        return $class->$orig(%args);
    }
};

=item _parse_data( $data )

You B<must> implement this method in you subclass to unpack your object.
Returns hashref of attributes which will be passed to constructor.

=cut

sub _parse_data {
    return { data => $_[1] };
}

=back

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
