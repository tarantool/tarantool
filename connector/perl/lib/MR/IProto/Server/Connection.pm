package MR::IProto::Server::Connection;

=head1 NAME

=head1 DESCRIPTION

=cut

use Mouse;
use AnyEvent::DNS;
use Scalar::Util qw/weaken/;

with 'MR::IProto::Role::Debuggable';

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

has fh => (
    is  => 'ro',
    isa => 'FileHandle',
    required => 1,
);

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

has hostname => (
    is  => 'ro',
    isa => 'Str',
    writer => '_hostname',
    lazy_build => 1,
);

has _handle => (
    is  => 'ro',
    isa => 'AnyEvent::Handle',
    lazy_build => 1,
);

has _recv_header => (
    is  => 'ro',
    isa => 'CodeRef',
    lazy_build => 1,
);

sub BUILD {
    my ($self) = @_;
    $self->_debug(sprintf "Connection accepted") if $self->debug >= 1;
    weaken($self);
    AnyEvent::DNS::reverse_verify $self->host, sub {
        my ($hostname) = @_;
        if ($hostname) {
            $self->_hostname($hostname);
            $self->_debug(sprintf "%s resolved as %s", $self->host, $hostname) if $self->debug >= 4;
        } else {
            $self->_hostname($self->host);
            $self->_debug(sprintf "Can't resolve %s", $self->host);
        }
        $self->_handle;
        $self->on_accept->($self) if $self->on_accept;
        return;
    };
    return;
}

sub DEMOLISH {
    my ($self) = @_;
    $self->_debug(sprintf "Object for connection was destroyed\n") if $self->debug >= 1;
    return;
}

sub close {
    my ($self) = @_;
    $self->on_close->($self) if $self->on_close;
    return;
}

sub _build_hostname {
    my ($self) = @_;
    return $self->host;
}

sub _build__handle {
    my ($self) = @_;
    weaken($self);
    my $peername = join ':', $self->host, $self->port;
    return AnyEvent::Handle->new(
        fh       => $self->fh,
        peername => $peername,
        on_read  => sub {
            my ($handle) = @_;
            $handle->unshift_read( chunk => 12, $self->_recv_header );
            return;
        },
        on_eof   => sub {
            my ($handle) = @_;
            $self->_debug("Connection closed by foreign host\n") if $self->debug >= 1;
            $handle->destroy();
            $self->on_close->($self) if $self->on_close;
            return;
        },
        on_error => sub {
            my ($handle, $fatal, $message) = @_;
            $handle->destroy();
            if ($self->on_error) {
                $self->_debug("error: $message\n") if $self->debug >= 1;
                $self->on_error->($self, $message);
            } else {
                $self->_debug("error: $message\n");
            }
            $self->on_close->($self) if $self->on_close;
            return;
        }
    );
    return;
}

sub _build__recv_header {
    my ($self) = @_;
    weaken($self);
    my $handler = $self->handler;
    return sub {
        my ($handle, $data) = @_;
        $self->_debug_dump('recv header: ', $data) if $self->debug >= 5;
        my ($cmd, $length, $sync) = unpack 'L3', $data;
        $handle->unshift_read(
            chunk => $length,
            sub {
                my ($handle, $data) = @_;
                $self->_debug_dump('recv payload: ', $data) if $self->debug >= 5;
                my $result;
                if (eval { $result = $handler->($self, $cmd, $data); 1 }) {
                    my $header = pack 'L3', $cmd, length $result, $sync;
                    if ($self->debug >= 6) {
                        $self->_debug_dump('send header: ', $header);
                        $self->_debug_dump('send payload: ', $result);
                    }
                    $handle->push_write($header . $result);
                } else {
                    warn $@;
                    $self->_debug("Failed to handle cmd=$cmd\n");
                }
                return;
            }
        );
        return;
    };
}

sub _debug {
    my ($self, $msg) = @_;
    $self->debug_cb->( sprintf "%s(%s:%d): %s", $self->hostname, $self->host, $self->port, $msg );
    return;
}

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
