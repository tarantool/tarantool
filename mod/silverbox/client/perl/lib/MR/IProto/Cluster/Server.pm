package MR::IProto::Cluster::Server;

=head1 NAME

=head1 DESCRIPTION

=cut

use Moose;
use Moose::Util::TypeConstraints;
use MR::IProto::Message;
use AnyEvent::Handle;
use Scalar::Util qw(weaken);

coerce 'MR::IProto::Cluster::Server'
    => from 'Str'
    => via {
        my ($host, $port) = split /:/, $_;
        __PACKAGE__->new(
            host => $host,
            port => $port,
        );
    };

has host => (
    is  => 'ro',
    isa => 'Str',
    required => 1,
);

has port => (
    is  => 'ro',
    isa => 'Int',
    required => 1,
);

has timeout => (
    is  => 'ro',
    isa => 'Num',
    default => 2,
);

has tcp_nodelay => (
    is  => 'ro',
    isa => 'Int',
    default => 1,
);

has tcp_keepalive => (
    is  => 'ro',
    isa => 'Int',
    default => 0,
);

has max_parallel => (
    is  => 'ro',
    isa => 'Int',
    default => 10,
);

has active => (
    is  => 'rw',
    isa => 'Bool',
    default => 1,
);

has debug => (
    is  => 'rw',
    isa => 'Int',
    default => 0,
);

has dump_no_ints => (
    is  => 'ro',
    isa => 'Bool',
);

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
    is  => 'ro',
    isa => 'Int',
    default => 0,
    traits  => ['Counter'],
    handles => {
        _inc_in_progress => 'inc',
        _dec_in_progress => 'dec',
    },
);

has _callbacks => (
    is  => 'ro',
    isa => 'HashRef',
    lazy_build => 1,
);

sub send_message {
    my ($self, $message, $callback) = @_;
    $message = MR::IProto::Message->new($message) if ref $message eq 'HASH';
    $self->_callbacks->{$callback} = $callback;
    push @{$self->_queue}, [ $message, $callback ];
    $self->_try_to_send_one_more();
    return;
}

sub _send_message {
    my ($self, $message, $callback) = @_;
    weaken($self);
    $self->_inc_in_progress();
    $self->_debug_dump(5, 'send header: ', $message->header);
    $self->_debug_dump(5, 'send payload: ', $message->payload);
    $self->_handle->push_write( $message->message );
    $self->_handle->push_read( chunk => 12, sub {
        my ($handle, $data) = @_;
        $self->_debug_dump(6, 'recv header: ', $data);
        my ($response_msg, $payload_length, $response_sync) = unpack('L3', $data);
        if( $response_msg == $message->msg and $response_sync == $message->sync ) {
            $handle->unshift_read( chunk => $payload_length, sub {
                my ($handle, $data) = @_;
                $self->_debug_dump(6, 'recv payload: ', $data);
                delete $self->_callbacks->{$callback};
                $self->_dec_in_progress();
                $callback->([$message->unpack->($data)]);
                $self->_try_to_send_one_more();
                return;
            });
        }
        else {
            $self->_dec_in_progress();
            $handle->_error(0, 0, sprintf "unexpected reply msg($response_msg != %s) or sync($response_sync != %s)", $message->msg, $message->sync);
            $self->_try_to_send_one_more();
            return;
        }
        return;
    });
    return;
}

sub _try_to_send_one_more {
    my ($self) = @_;
    if( $self->_in_progress < $self->max_parallel ) {
        if( my $task = shift @{ $self->_queue } ) {
            $self->_send_message(@$task);
        }
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
            return $self->timeout;
        },
        on_connect => sub {
            my ($handle) = @_;
            $self->_debug(1, "connected");
            return;
        },
        on_error   => sub {
            my ($handle, $fatal, $message) = @_;
            $self->_debug(0, ($fatal ? 'fatal ' : '') . 'error: ' . $message);
            $_->(undef, $message) foreach values %{$self->_callbacks};
            $self->_clear_handle();
            $self->_clear_callbacks();
            $self->_debug(1, 'closing socket');
            $handle->destroy();
            return;
        },
        on_timeout => sub {
            my ($handle) = @_;
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

sub _debug {
    my ($self, $level, $msg) = @_;
    return if $self->debug < $level;
    warn sprintf "%s:%d: %s\n", $self->host, $self->port, $msg;
    return;
}

sub _debug_dump {
    my ($self, $level, $msg, $datum) = @_;
    unless($self->dump_no_ints) {
        $msg .= join(' ', unpack('L*', $datum));
        $msg .= ' > ';
    }
    $msg .= join(' ', map { sprintf "%02x", $_ } unpack("C*", $datum));
    warn sprintf "%s:%d: %s\n", $self->host, $self->port, $msg;
    return;
}

no Moose;
__PACKAGE__->meta->make_immutable();

1;
