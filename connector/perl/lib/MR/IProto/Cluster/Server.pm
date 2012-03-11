package MR::IProto::Cluster::Server;

=head1 NAME

MR::IProto::Cluster::Server - server

=head1 DESCRIPTION

This class is used to implement all communication with one server.

=cut

use Mouse;
use Mouse::Util::TypeConstraints;
use MR::IProto::Connection::Async;
use MR::IProto::Connection::Sync;
use MR::IProto::Message;

with 'MR::IProto::Role::Debuggable';

coerce 'MR::IProto::Cluster::Server'
    => from 'Str'
    => via {
        my ($host, $port, $weight) = split /:/, $_;
        __PACKAGE__->new(
            host => $host,
            port => $port,
            defined $weight ? ( weight => $weight ) : (),
        );
    };

has prefix => (
    is  => 'ro',
    isa => 'Str',
    default => 'MR::IProto',
);

=head1 ATTRIBUTES

=over

=item host

Host name or IP address.

=cut

has host => (
    is  => 'ro',
    isa => 'Str',
    required => 1,
);

=item port

Port number.

=cut

has port => (
    is  => 'ro',
    isa => 'Int',
    required => 1,
);

=item weight

Server weight.

=cut

has weight => (
    is  => 'ro',
    isa => 'Int',
    default => 1,
);

=item connect_timeout

Timeout of connect operation.

=cut

has connect_timeout => (
    is  => 'rw',
    isa => 'Num',
    default => 2,
);

=item timeout

Timeout of read and write operations.

=cut

has timeout => (
    is  => 'rw',
    isa => 'Num',
    default => 2,
    trigger => sub {
        my ($self, $new) = @_;
        $self->async->set_timeout($new) if $self->has_async();
        $self->sync->set_timeout($new) if $self->has_sync();
        return;
    },
);

=item tcp_nodelay

Enable TCP_NODELAY.

=cut

has tcp_nodelay => (
    is  => 'ro',
    isa => 'Int',
    default => 1,
);

=item tcp_keepalive

Enable SO_KEEPALIVE.

=cut

has tcp_keepalive => (
    is  => 'ro',
    isa => 'Int',
    default => 0,
);

=item max_parallel

Max amount of simultaneous request.

=cut

has max_parallel => (
    is  => 'ro',
    isa => 'Int',
    default => 10,
);

=item active

Is server used in balancing.

=cut

has active => (
    is  => 'rw',
    isa => 'Bool',
    default => 1,
);

has on_close => (
    is  => 'rw',
    isa => 'CodeRef',
);

has async => (
    is  => 'ro',
    isa => 'MR::IProto::Connection::Async',
    lazy_build => 1,
);

has sync => (
    is  => 'ro',
    isa => 'MR::IProto::Connection::Sync',
    lazy_build => 1,
);

=back

=head1 PROTECTED METHODS

=over

=cut

sub _build_async {
    my ($self) = @_;
    return MR::IProto::Connection::Async->new( server => $self );
}

sub _build_sync {
    my ($self) = @_;
    return MR::IProto::Connection::Sync->new( server => $self );
}

sub _build_debug_cb {
    my ($self) = @_;
    my $prefix = $self->prefix;
    return sub {
        my ($msg) = @_;
        chomp $msg;
        warn "$prefix: $msg\n";
        return;
    };
}

=item _send_started( $sync, $message, $data )

This method is called when message is started to send.

=cut

sub _send_started {
    return;
}

=item _recv_finished( $sync, $message, $data, $error )

This method is called when message is received.

=cut

sub _recv_finished {
    return;
}

sub _debug {
    my ($self, $msg) = @_;
    $self->debug_cb->( sprintf "%s:%d: %s", $self->host, $self->port, $msg );
    return;
}

=back

=head1 SEE ALSO

L<MR::IProto>, L<MR::IProto::Cluster>.

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
