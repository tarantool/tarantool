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

has debug_cb => (
    is  => 'rw',
    isa => 'CodeRef',
    lazy_build => 1,
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

has _read_reply => (
    is  => 'ro',
    isa => 'CodeRef',
    lazy_build => 1,
);

sub send_message {
    my $self = shift;
    push @{$self->_queue}, [ @_ ];
    $self->_try_to_send();
    return;
}

sub _send_message {
    my ($self, $sync, $header, $payload, $callback) = @_;
    weaken($self);
    $self->_inc_in_progress();
    $self->_callbacks->{$sync} = $callback;
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
        my ($msg, $payload_length, $sync) = unpack('L3', $data);
        $handle->unshift_read( chunk => $payload_length, sub {
            my ($handle, $data) = @_;
            $self->_debug_dump(6, 'recv payload: ', $data);
            $self->_dec_in_progress();
            delete($self->_callbacks->{$sync})->($msg, $sync, $data);
            $self->_try_to_send();
            return;
        });
        return;
    };
}

sub _try_to_send {
    my ($self) = @_;
    while( $self->_in_progress < $self->max_parallel && (my $task = shift @{ $self->_queue }) ) {
        $self->_send_message(@$task);
    }
    return;
}

sub _build_debug_cb {
    my ($self) = @_;
    my $prefix = $self->prefix;
    return sub {
        my ($msg) = @_;
        warn sprintf "%s: %s\n", $prefix, $msg;
        return;
    };
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
            foreach( values %{$self->_callbacks} ) {
                $self->_dec_in_progress();
                $_->(undef, undef, undef, $message);
            }
            $self->_clear_handle();
            $self->_clear_callbacks();
            $self->_debug(1, 'closing socket');
            $handle->destroy();
            $self->_try_to_send();
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
    $self->debug_cb->( sprintf "%s:%d: %s", $self->host, $self->port, $msg );
    return;
}

sub _debug_dump {
    my ($self, $level, $msg, $datum) = @_;
    return if $self->debug < $level;
    unless($self->dump_no_ints) {
        $msg .= join(' ', unpack('L*', $datum));
        $msg .= ' > ';
    }
    $msg .= join(' ', map { sprintf "%02x", $_ } unpack("C*", $datum));
    $self->debug_cb->( sprintf "%s:%d: %s", $self->host, $self->port, $msg );
    return;
}

no Moose;
__PACKAGE__->meta->make_immutable();

1;
