package MR::IProto;

=head1 NAME

=head1 DESCRIPTION

=cut

use Moose;
use AnyEvent;
use Errno;
use MR::IProto::Cluster;
use MR::IProto::Message;

has cluster => (
    is  => 'ro',
    isa => 'MR::IProto::Cluster',
    required => 1,
    coerce   => 1,
);

has rotateservers => (
    is  => 'ro',
    isa => 'Bool',
    default => 1,
);

has max_request_retries => (
    is  => 'ro',
    isa => 'Int',
    default => 2,
);

has retry_delay => (
    is  => 'ro',
    isa => 'Int',
    default => 0,
);

has debug => (
    is  => 'rw',
    isa => 'Int',
    default => 0,
);

=head1 PUBLIC METHODS

=over

=cut

sub chat {
    my ($self, $message, $callback) = @_;
    die "Callback must be specified" unless $callback;
    die "Method must be called in void context" if defined wantarray;
    my $server = $self->cluster->server($message->key);
    $server->send_message($message, $callback);
    return;
}

sub Chat {
    my $self = shift;
    my $message = @_ == 1 ? shift : { @_ };
    $message = MR::IProto::Message->new($message) if ref $message eq 'HASH';
    my $retries = $self->max_request_retries;
    for (my $try = 1; $try <= $retries; $try++) {
        sleep $self->retry_delay if $try > 1 && $self->retry_delay;
        $self->_debug(1, sprintf "chat msg=%d $try of $retries total", $message->msg);
        my $ret = $self->Chat1($message);
        if ($ret->{ok}) {
            return wantarray ? @{$ret->{ok}} : $ret->{ok}->[0];
        }
    }
    return;
}

sub Chat1 {
    my $self = shift;
    my $message = @_ == 1 ? shift : { @_ };
    $message = MR::IProto::Message->new($message) if ref $message eq 'HASH';
    my $server = $self->cluster->server($message->key);
    my ($data, $error, $timeout);
    my $exit = AnyEvent->condvar();
    $server->send_message($message, sub {
        ($data, $error) = @_;
        $timeout = $! == Errno::ETIMEDOUT;
        $exit->send();
        return;
    });
    $exit->recv();
    if( $error && $self->rotateservers ) {
        $self->_debug(2, "chat: failed");
        $server->active(0);
    }
    return $error ? { fail => $error, timeout => $timeout }
        : { ok => $data };
}

=back

=head1 PROTECTED METHODS

=over

=cut

sub BUILDARGS {
    my $self = shift;
    my %args = @_ == 1 ? %{shift()} : @_;
    $args{rotateservers} = 0 if delete $args{norotateservers};
    if( $args{servers} ) {
        my %srvargs;
        $srvargs{debug} = $args{debug} if exists $args{debug};
        $srvargs{timeout} = delete $args{timeout} if exists $args{timeout};
        $srvargs{tcp_nodelay} = delete $args{tcp_nodelay} if exists $args{tcp_nodelay};
        $srvargs{tcp_keepalive} = delete $args{tcp_keepalive} if exists $args{tcp_keepalive};
        $srvargs{dump_no_ints} = delete $args{dump_no_ints} if exists $args{dump_no_ints};
        my %clusterargs;
        $clusterargs{balance} = delete $args{balance} if exists $args{balance};
        $clusterargs{servers} = [
            map {
                my ($host, $port) = /^(.+):(\d+)$/;
                MR::IProto::Cluster::Server->new(
                    %srvargs,
                    host => $host,
                    port => $port,
                );
            } split /,/, delete $args{servers}
        ];
        $args{cluster} = MR::IProto::Cluster->new(%clusterargs);
    }
    return $self->SUPER::BUILDARGS(%args);
}

sub _debug {
    my ($self, $level, $msg) = @_;
    return if $self->debug < $level;
    warn sprintf "%s\n", $msg;
    return;
}

=back

=cut

no Moose;
__PACKAGE__->meta->make_immutable();

1;
