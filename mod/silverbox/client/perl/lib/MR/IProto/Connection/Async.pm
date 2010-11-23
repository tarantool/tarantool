package MR::IProto::Connection::Async;

=head1 NAME

MR::IProto::Connection::Async - async communication

=head1 DESCRIPTION

Used to perform asynchronous communication.

=cut

use Mouse;
extends 'MR::IProto::Connection';

use AnyEvent::Handle;
use Scalar::Util qw(weaken);

has _handle => (
    is  => 'ro',
    isa => 'AnyEvent::Handle',
    lazy_build => 1,
);

has _queue => (
    is  => 'ro',
    isa => 'ArrayRef',
    lazy_build => 1,
);

has _in_progress => (
    is  => 'rw',
    isa => 'Int',
    default => 0,
);

has _callbacks => (
    is  => 'ro',
    isa => 'HashRef',
    lazy_build => 1,
);

has _read_reply => (
    is  => 'ro',
    isa => 'CodeRef',
    lazy_build => 1,
);

=head1 PUBLIC METHODS

=over

=item send

Enqueue message send.
For list of arguments see L</_send>.

=cut

sub send {
    my $self = shift;
    push @{$self->_queue}, [ @_ ];
    $self->_try_to_send();
    return;
}

=item set_timeout( $timeout )

Set timeout value for existing connection.

=cut

sub set_timeout {
    my ($self, $timeout) = @_;
    $self->_handle->timeout($timeout) if $self->_has_handle();
    return;
}

=back

=head1 PROTECTED METHODS

=over

=item _send( $msg, $payload, $callback )

Send message to server.

=cut

sub _send {
    my ($self, $msg, $payload, $callback) = @_;
    my $sync = $self->_choose_sync();
    my $header = $self->_pack_header($msg, length $payload, $sync);
    $self->_callbacks->{$sync} = $callback;
    $self->_send_started($sync, $msg, $payload);
    $self->_debug_dump(5, 'send header: ', $header);
    $self->_debug_dump(5, 'send payload: ', $payload);
    $self->_handle->push_write( $header . $payload );
    $self->_handle->push_read( chunk => 12, $self->_read_reply );
    return;
}

sub _build__read_reply {
    my ($self) = @_;
    weaken($self);
    return sub {
        my ($handle, $data) = @_;
        $self->_debug_dump(6, 'recv header: ', $data);
        my ($msg, $payload_length, $sync) = $self->_unpack_header($data);
        $handle->unshift_read( chunk => $payload_length, sub {
            my ($handle, $data) = @_;
            $self->_debug_dump(6, 'recv payload: ', $data);
            $self->_recv_finished($sync, $msg, $data);
            $self->_try_to_send();
            delete($self->_callbacks->{$sync})->($msg, $data);
            return;
        });
        return;
    };
}

sub _try_to_send {
    my ($self) = @_;
    while( $self->_in_progress < $self->max_parallel && (my $task = shift @{ $self->_queue }) ) {
        $self->_send(@$task);
    }
    return;
}

sub _build__handle {
    my ($self) = @_;
    $self->_debug(4, "connecting");
    weaken($self);
    return AnyEvent::Handle->new(
        connect    => [ $self->host, $self->port ],
        no_delay   => $self->tcp_nodelay,
        keepalive  => $self->tcp_keepalive,
        timeout    => $self->timeout,
        on_prepare => sub {
            return $self->connect_timeout;
        },
        on_connect => sub {
            my ($handle) = @_;
            $self->_debug(1, "connected");
            return;
        },
        on_error   => sub {
            my ($handle, $fatal, $message) = @_;
            $self->_debug(0, ($fatal ? 'fatal ' : '') . 'error: ' . $message);
            my @callbacks;
            foreach my $sync ( keys %{$self->_callbacks} ) {
                $self->_recv_finished($sync, undef, undef, $message);
                push @callbacks, $self->_callbacks->{$sync};
            }
            $self->server->active(0);
            $self->_clear_handle();
            $self->_clear_callbacks();
            $self->_debug(1, 'closing socket');
            $handle->destroy();
            $self->_try_to_send();
            $_->(undef, undef, $message) foreach @callbacks;
            return;
        },
        on_timeout => sub {
            my ($handle) = @_;
            return unless keys %{$self->_callbacks};
            $handle->_error( Errno::ETIMEDOUT ) if keys %{$self->_callbacks};
            return;
        },
    );
}

sub _build__queue {
    my ($self) = @_;
    return [];
}

sub _build__callbacks {
    my ($self) = @_;
    return {};
}

around _choose_sync => sub {
    my ($orig, $self) = @_;
    my $sync;
    for( 1 .. 50 ) {
        $sync = $self->$orig();
        return $sync unless exists $self->_callbacks->{$sync};
    }
    die "Can't choose sync value after 50 iterations";
};

before _send_started => sub {
    my ($self) = @_;
    $self->_in_progress( $self->_in_progress + 1 );
    return;
};

after _recv_finished => sub {
    my ($self) = @_;
    $self->_in_progress( $self->_in_progress - 1 );
    return;
};

=back

=head1 SEE ALSO

L<MR::IProto::Connection>, L<MR::IProto::Cluster::Server>.

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
