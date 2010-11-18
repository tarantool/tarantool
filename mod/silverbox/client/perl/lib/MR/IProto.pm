package MR::IProto;

=head1 NAME

=head1 DESCRIPTION

=cut

use Moose;
use AnyEvent;
use Errno;
use Scalar::Util qw(weaken);
use MR::IProto::Cluster;

has prefix => (
    is  => 'ro',
    isa => 'Str',
    default => __PACKAGE__,
);

has cluster => (
    is  => 'ro',
    isa => 'MR::IProto::Cluster',
    required => 1,
    coerce   => 1,
    trigger  => sub {
        my ($self, $new) = @_;
        foreach my $server ( @{$new->servers} ) {
            $server->debug_cb($self->debug_cb) unless $server->has_debug_cb();
        }
        return;
    },
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

has debug_cb => (
    is  => 'rw',
    isa => 'CodeRef',
    lazy_build => 1,
);

has _callbacks => (
    is  => 'ro',
    isa => 'HashRef[CodeRef]',
    lazy_build => 1,
);

has _reply_class => (
    is  => 'ro',
    isa => 'HashRef[ClassName]',
    lazy_build => 1,
);

=head1 PUBLIC METHODS

=over

=cut

sub chat {
    my ($self, $message, $callback) = @_;
    if($callback) {
        die "Method must be called in void context if you want to use async" if defined wantarray;
        $self->_chat($message, $callback);
        return;
    }
    else {
        die "Method must be called in scalar context if you want to use sync" unless defined wantarray;
        my ($data, $error, $errno);
        my $exit = AnyEvent->condvar();
        $self->chat($message, sub {
            ($data, $error) = @_;
            $errno = $!;
            $exit->send();
            return;
        });
        $exit->recv();
        $! = $errno;
        die $error if $error;
        return $data;
    }
}

sub _chat {
    my ($self, $message, $callback) = @_;
    die "Callback must be specified" unless $callback;
    die "Method must be called in void context" if defined wantarray;

    my ($req_msg, $key, $body, $unpack);
    # MR::IProto::Message OO-API
    if( blessed($message) ) {
        $req_msg = $message->msg;
        $key = $message->key;
        $body = $message->data;
    }
    # Old-style compatible API
    else {
        $req_msg = $message->{msg};
        $body = exists $message->{payload} ? $message->{payload}
            : ref $message->{data} ? pack delete $message->{pack} || 'L*', @{$message->{data}}
            : $message->{data};
        $unpack = $message->{no_reply} ? sub { 0 } : $message->{unpack};
    }

    my $req_sync;
    for( 1 .. 10 ) {
        $req_sync = int(rand 0xffffffff);
        last unless exists $self->_callbacks->{$req_sync};
    }
    $self->_callbacks->{$req_sync} = $callback;
    my $header = pack 'L3', $req_msg, length $body, $req_sync;
    my $server = $self->cluster->server( $key );

    weaken($self);
    $server->send_message($req_sync, $header, $body, sub {
        my ($resp_msg, $resp_sync, $data, $error) = @_;
        if ($error) {
            if( $self->rotateservers ) {
                $self->_debug(2, "chat: failed");
                $server->active(0);
            }
            delete($self->_callbacks->{$req_sync})->(undef, $error);
        }
        else {
            if ($unpack) {
                $data = [ $unpack->($data) ];
            }
            else {
                if( my $data_class = $self->_reply_class->{$resp_msg} ) {
                    $data = $data_class->new($data);
                }
                else {
                    $error = sprintf "Unknown message code %d", $resp_msg;
                    $data = undef;
                }
            }
            delete($self->_callbacks->{$resp_sync})->($data, $error);
        }
        return;
    });
    return;
}

sub Chat {
    my $self = shift;
    my $message = @_ == 1 ? shift : { @_ };
    my $retries = $self->max_request_retries;
    for (my $try = 1; $try <= $retries; $try++) {
        sleep $self->retry_delay if $try > 1 && $self->retry_delay;
        $self->_debug(1, sprintf "chat msg=%d $try of $retries total", $message->{msg});
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
    my $data = eval { $self->chat($message) };
    return $@ ? { fail => $@, timeout => $! == Errno::ETIMEDOUT }
        : { ok => $data };
}

=back

=head1 PROTECTED METHODS

=over

=cut

sub BUILDARGS {
    my $class = shift;
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
    return $class->SUPER::BUILDARGS(%args);
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

sub _build__callbacks {
    my ($self) = @_;
    return {};
}

sub _build__reply_class {
    my ($self) = @_;
    my $re = sprintf '^%s::.*::Reply$', $self->prefix;
    my %reply = map { $_->msg => $_ }
        grep $_->can('msg'),
        grep /$re/,
        MR::IProto::Message->meta->subclasses();
    return \%reply;
}

sub _debug {
    my ($self, $level, $msg) = @_;
    return if $self->debug < $level;
    $self->debug_cb->($msg);
    return;
}

=back

=cut

no Moose;
__PACKAGE__->meta->make_immutable();

1;
