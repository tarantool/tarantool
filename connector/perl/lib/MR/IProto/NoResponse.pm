package MR::IProto::NoResponse;

=head1 NAME

MR::IProto::NoResponse - no response

=head1 DESCRIPTION

Base class used to mark messages with no response.

=cut

use Mouse;
extends 'MR::IProto::Response';

has '+data' => (
    isa => 'Undef',
);

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
