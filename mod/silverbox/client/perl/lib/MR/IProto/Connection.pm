package MR::IProto::Connection;

=head1 NAME

MR::IProto::Connection - base communication class

=head1 DESCRIPTION

Base class for sync and async connections.

=cut

use Moose;

=head1 server

=over

=item server

Instanse of L<MR::IProto::Cluster::Server>.

=cut

has server => (
    is  => 'ro',
    isa => 'MR::IProto::Cluster::Server',
    required => 1,
    weak_ref => 1,
    handles  => [qw(
        host
        port
        connect_timeout
        timeout
        tcp_nodelay
        tcp_keepalive
        max_parallel
        _send_started
        _recv_finished
        _debug
        _debug_dump
    )],
);

=back

=cut

sub _pack_header {
    my ($self, $msg, $length, $sync) = @_;
    return pack 'L3', $msg, $length, $sync;
}

sub _unpack_header {
    my ($self, $header) = @_;
    return unpack 'L3', $header;
}

sub _choose_sync {
    my ($self) = @_;
    return int(rand 0xffffffff);
}

no Moose;
__PACKAGE__->meta->make_immutable();

1;
