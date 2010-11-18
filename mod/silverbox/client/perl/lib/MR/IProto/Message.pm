package MR::IProto::Message;

=head1 NAME

=head1 DESCRIPTION

=cut

use Moose;

has data => (
    is  => 'ro',
    isa => 'Value',
    lazy_build => 1,
);

sub key {
    my ($self) = @_;
    return undef unless $self->can('key_attr');
    my $method = $self->key_attr;
    return $self->$method();
}

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

sub _build_data {
    my ($self) = @_;
    return '';
}

sub _parse_data {
    my ($class, $data) = @_;
    return { data => $data };
}

no Moose;
__PACKAGE__->meta->make_immutable();

1;
