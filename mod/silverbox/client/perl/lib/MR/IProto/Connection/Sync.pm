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

has _sent => (
    is  => 'ro',
    default => sub { [] },
);

=head1 PUBLIC METHODS

=over

=item send

See L<MR::IProto::Connection/send> for more information.

=cut

sub send {
    my ($self, $msg, $payload, $callback, $no_reply, $sync) = @_;
    my $server = $self->server;
    my $sent = $self->_sent;
    my $ok = eval {
        $sync = $self->_choose_sync() unless defined $sync;
        $server->_send_started($sync, $msg, $payload);

        my $socket = $self->_socket;
        unless (@$sent) {
            vec((my $rin = ''), fileno($socket), 1) = 1;
            if (select((my $rout = $rin), undef, undef, 0) > 0) {
                if (sysread($socket, my $buf, 1)) {
                    die "More then 0 bytes was received when nothing was waited for";
                } else {
                    # Connection was closed by other side, socket is in CLOSE_WAIT state, reconnecting
                    $self->_clear_socket();
                    $socket = $self->_socket;
                }
            }
        }

        my $header = $self->_pack_header($msg, length $payload, $sync);
        if( $server->debug >= 5 ) {
            $server->_debug_dump('send header: ', $header);
            $server->_debug_dump('send payload: ', $payload);
        }

        my $write = $header . $payload;
        while( length $write ) {
            my $written = syswrite($socket, $write);
            if (!defined $written) {
                if ($! != Errno::EINTR) {
                    $! = Errno::ETIMEDOUT if $! == Errno::EAGAIN; # Hack over SO_SNDTIMEO behaviour
                    die "send: $!";
                }
            } else {
                substr $write, 0, $written, '';
            }
        }
        1;
    };
    if($ok) {
        if ($no_reply) {
            $callback->(undef, undef);
            $server->_recv_finished($sync, undef, undef);
        } else {
            push @$sent, [$sync, $callback];
        }
    }
    else {
        $self->_handle_error($sync, $callback, $@);
    }
    return;
}

sub recv_all {
    my ($self) = @_;
    my $server = $self->server;
    my $sent = $self->_sent;
    my $dump_resp = $server->debug >= 6;
    while (my $args = shift @$sent) {
        my ($sync, $callback) = @$args;
        my ($resp_msg, $resp_payload);
        my $ok = eval {
            my $socket = $self->_socket;
            my $resp_header;
            my $to_read = 12;
            while( $to_read ) {
                my $read = sysread($socket, my $buf, $to_read);
                if (!defined $read) {
                    if ($! != Errno::EINTR) {
                        $! = Errno::ETIMEDOUT if $! == Errno::EAGAIN; # Hack over SO_RCVTIMEO behaviour
                        die "recv: $!";
                    }
                } elsif ($read == 0) {
                    die "recv: Unexpected end-of-file";
                } else {
                    $resp_header .= $buf;
                    $to_read -= $read;
                }
            }
            $server->_debug_dump('recv header: ', $resp_header) if $dump_resp;
            ($resp_msg, my $resp_length, my $resp_sync) = $self->_unpack_header($resp_header);
            die "Request and reply sync is different: $resp_sync != $sync" unless $resp_sync == $sync;

            $to_read = $resp_length;
            while( $to_read ) {
                my $read = sysread($socket, my $buf, $to_read);
                if (!defined $read) {
                    if ($! != Errno::EINTR) {
                        $! = Errno::ETIMEDOUT if $! == Errno::EAGAIN; # Hack over SO_RCVTIMEO behaviour
                        die "recv: $!";
                    }
                } elsif ($read == 0) {
                    die "recv: Unexpected end-of-file";
                } else {
                    $resp_payload .= $buf;
                    $to_read -= $read;
                }
            }
            $server->_debug_dump('recv payload: ', $resp_payload) if $dump_resp;
            1;
        };
        if($ok) {
            $server->_recv_finished($sync, $resp_msg, $resp_payload);
            $callback->($resp_msg, $resp_payload);
        }
        else {
            $self->_handle_error($sync, $callback, $@);
        }
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
    my $server = $self->server;
    $server->_debug("connecting") if $server->debug >= 4;
    my $socket = IO::Socket::INET->new(
        PeerHost => $self->host,
        PeerPort => $self->port,
        Proto    => 'tcp',
        Timeout  => $self->connect_timeout,
    ) or do {
        $@ =~ s/^IO::Socket::INET: (?:connect: )?//;
        if ($@ eq 'timeout') {
            # Hack over IO::Socket behaviour
            $! = Errno::ETIMEDOUT;
            $@ = "$!";
        }
        die "connect: $@";
    };
    $socket->sockopt(SO_KEEPALIVE, 1) if $self->tcp_keepalive;
    $socket->setsockopt((getprotobyname('tcp'))[2], TCP_NODELAY, 1) if $self->tcp_nodelay;
    $self->_set_timeout($socket, $self->timeout) if $self->timeout;
    $server->_debug("connected") if $server->debug >= 4;
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

sub _handle_error {
    my ($self, $sync, $callback, $error) = @_;
    if (!$error) {
        $error = 'Unknown error';
    } elsif ($error =~ /^(.+?) at \S+ line \d+/s) {
        $error = $1;
    }
    my $server = $self->server;
    $server->_debug("error: $error");
    if($self->_has_socket()) {
        close($self->_socket);
        $self->_clear_socket();
    }
    $server->active(0);
    my $sent = $self->_sent;
    my @sent = splice @$sent, 0, $#$sent;
    $server->_recv_finished($sync, undef, undef, $error);
    $callback->(undef, undef, $error);
    foreach my $args (@sent) {
        my ($sync, $callback) = @$args;
        $server->_recv_finished($sync, undef, undef, $error);
        $callback->(undef, undef, $error);
    }
    return
}

=head1 SEE ALSO

L<MR::IProto::Connection>, L<MR::IProto::Cluster::Server>.

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
