package MR::IProto::Server;

=head1 NAME

=head1 DESCRIPTION

=cut

use Mouse;
use AnyEvent::Handle;
use AnyEvent::Socket;
use Scalar::Util qw/weaken/;
use MR::IProto::Server::Connection;

with 'MR::IProto::Role::Debuggable';

has prefix => (
    is  => 'ro',
    isa => 'Str',
    default => sub { ref shift },
);

has host => (
    is  => 'ro',
    isa => 'Str',
    default => '0.0.0.0',
);

has port => (
    is  => 'ro',
    isa => 'Int',
    required => 1,
);

has handler => (
    is  => 'ro',
    isa => 'CodeRef',
    required => 1,
);

has on_accept => (
    is  => 'ro',
    isa => 'CodeRef',
);

has on_close => (
    is  => 'ro',
    isa => 'CodeRef',
);

has on_error => (
    is  => 'ro',
    isa => 'CodeRef',
);

has _guard => (
    is  => 'ro',
    lazy_build => 1,
);

has _connections => (
    is  => 'ro',
    isa => 'HashRef',
    default => sub { {} },
);

has _recv_payload => (
    is  => 'ro',
    isa => 'CodeRef',
    lazy_build => 1,
);

sub run {
    my ($self) = @_;
    $self->_guard;
    return;
}

sub _build_debug_cb {
    my ($self) = @_;
    my $prefix = $self->prefix;
    return sub {
        my ($msg) = @_;
        chomp $msg;
        warn sprintf "%s: %s\n", $prefix, $msg;
        return;
    };
}

sub _build__guard {
    my ($self) = @_;
    weaken($self);
    return tcp_server $self->host, $self->port, sub {
        my ($fh, $host, $port) = @_;
        my $connection = MR::IProto::Server::Connection->new(
            fh        => $fh,
            host      => $host,
            port      => $port,
            handler   => $self->handler,
            on_accept => $self->on_accept,
            on_close  => sub {
                my ($connection) = @_;
                my $key = sprintf "%s:%d", $connection->host, $connection->port;
                delete $self->_connections->{$key};
                $self->on_close->($connection) if $self->on_close;
                return;
            },
            on_error  => $self->on_error,
            debug     => $self->debug,
            debug_cb  => $self->debug_cb,
        );
        $self->_connections->{"$host:$port"} = $connection;
        return;
    };
}

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
