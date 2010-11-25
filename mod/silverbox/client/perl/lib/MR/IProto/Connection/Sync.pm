package MR::IProto::Connection::Sync;

=head1 NAME

MR::IProto::Connection::Sync - sync communication

=head1 DESCRIPTION

Used to perform synchronous communication.

=cut

use Mouse;
extends 'MR::IProto::Connection';

use Errno;
use IO::Socket::INET;
use Socket qw( TCP_NODELAY SO_KEEPALIVE SO_SNDTIMEO SO_RCVTIMEO );

has _socket => (
    is  => 'ro',
    isa => 'IO::Socket::INET',
    lazy_build => 1,
);

=head1 PUBLIC METHODS

=over

=item send

See L<MR::IProto::Connection/send> for more information.

=cut

sub send {
    my ($self, $msg, $payload, $callback) = @_;
    my ($resp_msg, $resp_payload);
    my $ok = eval {
        my $sync = $self->_choose_sync();
        my $header = $self->_pack_header($msg, length $payload, $sync);
        $self->_send_started($sync, $msg, $payload);
        $self->_debug_dump(5, 'send header: ', $header);
        $self->_debug_dump(5, 'send payload: ', $payload);

        my $write = $header . $payload;
        while( length $write ) {
            my $written = syswrite($self->_socket, $write);
            die $! unless defined $written;
            substr $write, 0, $written, '';
        }

        my $resp_header;
        my $to_read = 12;
        while( $to_read ) {
            my $read = sysread($self->_socket, my $buf, $to_read);
            die $! unless defined $read;
            $resp_header .= $buf;
            $to_read -= $read;
        }
        $self->_debug_dump(6, 'recv header: ', $resp_header);
        ($resp_msg, my $resp_length, my $resp_sync) = $self->_unpack_header($resp_header);
        die "Request and reply sync is different: $resp_msg != $msg" unless $resp_msg == $msg;

        $to_read = $resp_length;
        while( $to_read ) {
            my $read = sysread($self->_socket, my $buf, $to_read);
            die $! unless defined $read;
            $resp_payload .= $buf;
            $to_read -= $read;
        }
        $self->_debug_dump(6, 'recv payload: ', $resp_payload);
        1;
    };
    if($ok) {
        $callback->($resp_msg, $resp_payload);
    }
    else {
        $! = Errno::ETIMEDOUT if $! == Errno::EINPROGRESS; # Hack over IO::Socket behaviour
        if($self->_has_socket()) {
            close($self->_socket);
            $self->_clear_socket();
        }
        $self->server->active(0);
        $callback->(undef, undef, $@);
    }
    return;
}

=item set_timeout( $timeout )

Set timeout value for existing connection.

=cut

sub set_timeout {
    my ($self, $timeout) = @_;
    $self->_set_timeout($self->_socket, $timeout) if $self->_has_socket();
    return;
}

=back

=cut

sub _build__socket {
    my ($self) = @_;
    my $socket = IO::Socket::INET->new(
        PeerHost => $self->host,
        PeerPort => $self->port,
        Proto    => 'tcp',
        Timeout  => $self->connect_timeout,
    ) or die $@;
    $socket->sockopt(SO_KEEPALIVE, 1) if $self->tcp_keepalive;
    $socket->setsockopt((getprotobyname('tcp'))[2], TCP_NODELAY, 1) if $self->tcp_nodelay;
    $self->_set_timeout($socket, $self->timeout) if $self->timeout;
    return $socket;
}

sub _set_timeout {
    my ($self, $socket, $timeout) = @_;
    my $sec  = int $timeout; # seconds
    my $usec = int( ($timeout - $sec) * 1_000_000 ); # micro-seconds
    my $timeval = pack "L!L!", $sec, $usec; # struct timeval;
    $socket->sockopt(SO_SNDTIMEO, $timeval);
    $socket->sockopt(SO_RCVTIMEO, $timeval);
    return;
}

=head1 SEE ALSO

L<MR::IProto::Connection>, L<MR::IProto::Cluster::Server>.

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
