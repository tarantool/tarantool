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

has _no_reply => (
    is  => 'ro',
    isa => 'ArrayRef',
    lazy_build => 1,
);

has _on_drain => (
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
    if( $self->_in_progress < $self->max_parallel ) {
        $self->_in_progress( $self->_in_progress + 1 );
        $self->_send(@_);
    }
    else {
        push @{$self->_queue}, [@_];
    }
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

=item _send( $msg, $payload, $callback, $no_reply )

Send message to server.

=cut

sub _send {
    my ($self, $msg, $payload, $callback, $no_reply, $sync) = @_;
    $sync = $self->_choose_sync() unless defined $sync;
    my $header = $self->_pack_header($msg, length $payload, $sync);
    my $server = $self->server;
    $self->_callbacks->{$sync} = $callback;
    $server->_send_started($sync, $msg, $payload);
    if( $server->debug >= 5 ) {
        $server->_debug_dump('send header: ', $header);
        $server->_debug_dump('send payload: ', $payload);
    }
    my $handle = $self->_handle;
    $handle->push_write( $header . $payload );
    if( $no_reply ) {
        push @{$self->_no_reply}, $sync;
        $handle->on_drain( $self->_on_drain ) unless defined $handle->{on_drain};
    }
    else {
        $handle->push_read( chunk => 12, $self->_read_reply );
    }
    return;
}

sub _build__read_reply {
    my ($self) = @_;
    my $server = $self->server;
    weaken($self);
    weaken($server);
    return sub {
        my ($handle, $data) = @_;
        my $dump_resp = $server->debug >= 6;
        $server->_debug_dump('recv header: ', $data) if $dump_resp;
        my ($msg, $payload_length, $sync) = $self->_unpack_header($data);
        $handle->unshift_read( chunk => $payload_length, sub {
            my ($handle, $data) = @_;
            $server->_debug_dump('recv payload: ', $data) if $dump_resp;
            $server->_recv_finished($sync, $msg, $data);
            $self->_finish_and_start();
            delete($self->_callbacks->{$sync})->($msg, $data);
            return;
        });
        return;
    };
}

sub _try_to_send {
    my ($self) = @_;
    while( $self->_in_progress < $self->max_parallel && (my $task = shift @{ $self->_queue }) ) {
        $self->_in_progress( $self->_in_progress + 1 );
        $self->_send(@$task);
    }
    return;
}

sub _finish_and_start {
    my ($self) = @_;
    if( my $task = shift @{$self->_queue} ) {
        $self->_send(@$task);
    }
    else {
        $self->_in_progress( $self->_in_progress - 1 );
    }
    return;
}

sub _build__handle {
    my ($self) = @_;
    my $server = $self->server;
    $server->_debug("connecting") if $server->debug >= 4;
    weaken($self);
    weaken($server);
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
            $server->_debug("connected") if $server->debug >= 1;
            return;
        },
        on_error   => sub {
            my ($handle, $fatal, $message) = @_;
            $server->_debug(($fatal ? 'fatal ' : '') . 'error: ' . $message);
            my @callbacks;
            foreach my $sync ( keys %{$self->_callbacks} ) {
                $server->_recv_finished($sync, undef, undef, $message);
                $self->_in_progress( $self->_in_progress - 1 );
                push @callbacks, $self->_callbacks->{$sync};
            }
            $server->active(0);
            $self->_clear_handle();
            $self->_clear_callbacks();
            $self->_clear_no_reply();
            $server->_debug('closing socket') if $server->debug >= 1;
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

sub _build__on_drain {
    my ($self) = @_;
    my $server = $self->server;
    weaken($self);
    weaken($server);
    return sub {
        my ($handle) = @_;
        if( $self->_has_no_reply() ) {
            foreach my $sync ( @{$self->_no_reply} ) {
                $server->_recv_finished($sync, undef, undef);
                $self->_in_progress( $self->_in_progress - 1 );
                delete($self->_callbacks->{$sync})->(undef, undef);
            }
            $self->_clear_no_reply();
            $self->_try_to_send();
            $handle->on_drain(undef);
        }
        return;
    };
}

sub _build__queue {
    my ($self) = @_;
    return [];
}

sub _build__callbacks {
    my ($self) = @_;
    return {};
}

sub _build__no_reply {
    my ($self) = @_;
    return [];
}

around _choose_sync => sub {
    my ($orig, $self) = @_;
    my $sync;
    my $callbacks = $self->_callbacks;
    for( 1 .. 50 ) {
        $sync = $self->$orig();
        return $sync unless exists $callbacks->{$sync};
    }
    die "Can't choose sync value after 50 iterations";
};

=back

=head1 SEE ALSO

L<MR::IProto::Connection>, L<MR::IProto::Cluster::Server>.

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
