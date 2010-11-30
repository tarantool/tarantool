package MR::IProto;

=head1 NAME

MR::IProto - iproto network protocol client

=head1 SYNOPSIS

IProto client can be created with full control of
its behaviour:

    my $client = MR::IProto->new(
        cluster => MR::IProto::Cluster->new(
            servers => [
                MR::IProto::Cluster::Server->new(
                    host => 'xxx.xxx.xxx.xxx',
                    port => xxxx,
                ),
                ...
            ],
        ),
    );

Or without it:

    my $client = MR::IProto->new(
        servers => 'xxx.xxx.xxx.xxx:xxxx,xxx.xxx.xxx.xxx:xxxx',
    );

Messages can be prepared and processed using objects (recomended,
but requires some more CPU):

    my $request = MyProject::Message::MyOperation::Request->new(
        arg1 => 1,
        arg2 => 2,
    );
    my $response = $client->send($request);
    # $response isa My::Project::Message::MyOperation::Response.
    # Of course, both message classes (request and reply) must
    # be implemented by user.

Or without them (not recommended because of unclean architecture):

    my $response = $client->send({
        msg    => x,
        data   => [...],
        pack   => 'xxx',
        unpack => sub {
            my ($data) = @_;
            return (...);
        },
    });

Messages can be sent synchronously:

    my $response = $client->send($response);
    # exception is raised if error is occured
    # besides $@ you can check $! to identify reason of error

Or asynchronously:

    use AnyEvent;
    my $callback = sub {
        my ($reply, $error) = @_;
        # on error $error is defined and $! can be set
        return;
    };
    $client->send($request, $callback);
    # callback is called when reply is received or error is occured

=head1 DESCRIPTION

This client is used to communicate with cluster of balanced servers using
iproto network protocol.

To use it nicely you should to implement two subclasses of
L<MR::IProto::Message> for each message type, one for request message
and another for reply.
This classes must be named as C<prefix::*::suffix>, where I<prefix>
must be passed to constructor of L<MR::IProto> as value of L</prefix>
attribute and I<suffix> is either C<Request> or C<Response>.
This classes must be loaded before first message through client object
will be sent.

To send messages asyncronously you should to implement event loop by self.
L<AnyEvent> is recomended.

=cut

use Mouse;
use Errno;
use MRO::Compat;
use Scalar::Util qw(weaken);
use Time::HiRes;
use MR::IProto::Cluster;
use MR::IProto::Error;

=head1 ATTRIBUTES

=over

=item prefix

Prefix of the class name in which hierarchy subclasses of L<MR::IProto::Message>
are located. Used to find reply message classes.

=cut

has prefix => (
    is  => 'ro',
    isa => 'Str',
    default => sub { ref shift },
);

=item cluster

Instance of L<MR::IProto::Cluster>. Contains all servers between which
requests can be balanced.
Also can be specified in I<servers> parameter of constructor as a list of
C<host:port> pairs separated by comma.

=cut

has cluster => (
    is  => 'ro',
    isa => 'MR::IProto::Cluster',
    required => 1,
    coerce   => 1,
    handles  => [qw( timeout )],
    trigger  => sub {
        my ($self, $new) = @_;
        foreach my $server ( @{$new->servers} ) {
            $server->debug_cb($self->debug_cb) unless $server->has_debug_cb();
        }
        return;
    },
);

=item max_request_retries

Max amount of request retries which must be sent to different servers
before error is returned.

=cut

has max_request_retries => (
    is  => 'ro',
    isa => 'Int',
    default => 2,
);

=item retry_delay

Delay between request retries.

=cut

has retry_delay => (
    is  => 'ro',
    isa => 'Num',
    default => 0,
);

=item debug

Debug level.

=cut

has debug => (
    is  => 'rw',
    isa => 'Int',
    default => 0,
);

=item debug_cb

Callback which is called when debug message is written.

=cut

has debug_cb => (
    is  => 'rw',
    isa => 'CodeRef',
    lazy_build => 1,
);

=back

=cut

has _reply_class => (
    is  => 'ro',
    isa => 'HashRef[ClassName]',
    lazy_build => 1,
);

=head1 PUBLIC METHODS

=over

=item new( [ %args | \%args ] )

Constructor.
See L</ATTRIBUTES> and L</BUILDARGS> for more information about allowed arguments.

=item send( [ $message | \%args ], $callback? )

Send C<$message> to server and receive reply.

If C<$callback> is passed then request is done asyncronously and reply is passed
to callback as first argument.
Method B<must> be called in void context to prevent possible errors.
Only client errors can be raised in async mode. All communication errors are
passed to callback as second argument. Additional information can be extracted
from C<$!> variable.

In sync mode (when C<$callback> argument is skipped) all errors are raised
and C<$!> is also set. Response is returned from method, so method B<must>
be called in scalar context.

Request C<$message> can be instance of L<MR::IProto::Message> subclass.
In this case reply will be also subclass of L<MR::IProto::Message>.
Or it can be passed as C<\%args> hash reference with keys described
in L</_send>.

=cut

sub send {
    my ($self, $message, $callback) = @_;
    if($callback) {
        die "Method must be called in void context if you want to use async" if defined wantarray;
        $self->_send($message, $callback);
        return;
    }
    else {
        die "Method must be called in scalar context if you want to use sync" unless defined wantarray;
        my ($data, $error, $errno);
        $self->_send($message, sub {
            ($data, $error) = @_;
            $errno = $!;
            return;
        }, 1);
        $! = $errno;
        die $error if $error;
        return $data;
    }
}

=item send_bulk( \@messages, $callback? )

Send all of messages in C<\@messages> and return result (sync-mode) or
call callback (async-mode) after all replies was received.
Result is returned as array reference, which values can be instances of
L<MR::IProto::Response> or L<MR::IProto::Error> if request was passed
as object, or hash with keys C<data> and C<error> if message was passed
as C<\%args>.
Replies in result can be returned in order different then order of requests.

B<Warning!> Anyway asynchronous connection is used.

See L</_send> for more information about message data. Either
C<$message> or C<\%args> allowed as content of C<\@messages>.

=cut

sub send_bulk {
    my ($self, $messages, $callback) = @_;
    my @result;
    my $cv = AnyEvent->condvar();
    if($callback) {
        die "Method must be called in void context if you want to use async" if defined wantarray;
        $cv->begin( sub { $callback->(\@result) } );
    }
    else {
        die "Method must be called in scalar context if you want to use sync" unless defined wantarray;
        $cv->begin();
    }
    foreach my $message ( @$messages ) {
        $cv->begin();
        $self->_send($message, sub {
            my ($data, $error) = @_;
            push @result, blessed($data) ? $data
                : { data => $data, error => $error };
            $cv->end();
            return;
        });
    }
    $cv->end();
    if($callback) {
        return;
    }
    else {
        $cv->recv();
        return \@result;
    }
}

sub Chat {
    my $self = shift;
    my $message = @_ == 1 ? shift : { @_ };
    $message->{retry} = 1 if ref $message eq 'HASH';
    my $data;
    eval { $data = $self->send($message); 1 } or return;
    return wantarray ? @$data : $data->[0];
}

sub Chat1 {
    my $self = shift;
    my $message = @_ == 1 ? shift : { @_ };
    my $data;
    return eval { $data = $self->send($message); 1 } ? { ok => $data }
        : { fail => $@, timeout => $! == Errno::ETIMEDOUT };
}

sub SetTimeout {
    my ($self, $timeout) = @_;
    $self->timeout($timeout);
    return;
}

=back

=head1 PROTECTED METHODS

=over

=item BUILDARGS( [ %args | \%args ] )

For compatibility with previous version of client and simplicity
some additional arguments to constructor is allowed:

=over

=item servers

C<host:port> pairs separated by comma used to create
L<MR::IProto::Cluster::Server> objects.

=item timeout, tcp_nodelay, tcp_keepalive, dump_no_ints

Are passed directly to constructor of L<MR::IProto::Cluster::Server>.

=item balance

Is passed directly to constructor of L<MR::IProto::Cluster>.

=back

See L<Mouse::Manual::Construction/BUILDARGS> for more information.

=cut

around BUILDARGS => sub {
    my $orig = shift;
    my $class = shift;
    my %args = @_ == 1 ? %{shift()} : @_;
    $args{prefix} = $args{name} if exists $args{name};
    if( $args{servers} ) {
        my $cluster_class = $args{cluster_class} || 'MR::IProto::Cluster';
        my $server_class = $args{server_class} || 'MR::IProto::Cluster::Server';
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
                $server_class->new(
                    %srvargs,
                    host => $host,
                    port => $port,
                );
            } split /,/, delete $args{servers}
        ];
        $args{cluster} = $cluster_class->new(%clusterargs);
    }
    return $class->$orig(%args);
};

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
    my $re = sprintf '^%s::', $self->prefix;
    my %reply = map { $_->msg => $_ }
        grep $_->can('msg'),
        grep /$re/,
        # MR::IProto::Response->meta->subclasses();
        @{ mro::get_isarev('MR::IProto::Response') };
    return \%reply;
}

=item _send( [ $message | \%args ], $callback? )

Pure asyncronious internal implementation of send.

C<$message> is an instance of L<MR::IProto::Message>.
If C<\%args> hash reference is passed instead of C<$message> then it can
contain following keys:

=over

=item msg

Message code.

=item key

Depending on this value balancing between servers is implemented.

=item data

Message data. Already packed or unpacked. Unpacked data must be passed as
array reference and additional parameter I<pack> must be passed.

=item pack

First argument of L<pack|perlfunc/pack> function.

=item unpack

Code reference which is used to unpack reply.

=item no_reply

Message have no reply.

=item retry

Is retry is allowed. Values of attributes L</max_request_retries> and
L</retry_delay> is used if retry is allowed.

=back

=cut

sub _send {
    my ($self, $message, $callback, $sync) = @_;
    die "Callback must be specified" unless $callback;
    die "Method must be called in void context" if defined wantarray;

    my ($req_msg, $key, $body, $unpack, $retry, $response_class, $no_reply);
    # MR::IProto::Message OO-API
    if( blessed($message) ) {
        $req_msg = $message->msg;
        $key = $message->key;
        $body = $message->data;
        $response_class = $self->_reply_class->{$req_msg};
        die sprintf "Cannot find response class for message code $req_msg\n" unless $response_class;
        $no_reply = $response_class->isa('MR::IProto::NoResponse');
        $retry = $message->retry ? $self->max_request_retries : 1;
    }
    # Old-style compatible API
    else {
        $req_msg = $message->{msg};
        $body = exists $message->{payload} ? $message->{payload}
            : ref $message->{data} ? pack delete $message->{pack} || 'L*', @{$message->{data}}
            : $message->{data};
        $no_reply = $message->{no_reply};
        $unpack = $no_reply ? sub { 0 } : $message->{unpack} or die "unpack or no_reply must be specified";
        $retry = $message->{retry} ? $self->max_request_retries : 1;
    }

    my $try = 1;
    weaken($self);
    my ($sub, $server);
    my $xsync = $sync ? 'sync' : 'async';
    my $do_try = sub {
        my ($by_resp) = @_;
        $self->_debug(2, sprintf "send msg=%d try %d of %d total", $req_msg, $try, $by_resp ? $self->max_request_retries : $retry );
        $server = $self->cluster->server( $key );
        $server->$xsync->send($req_msg, $body, $sub, $no_reply);
    };
    my $next_try = $sync
        ? sub {
            my ($by_resp) = @_;
            Time::HiRes::sleep($self->retry_delay);
            $do_try->($by_resp);
            return;
        }
        : sub {
            my ($by_resp) = @_;
            my $timer;
            $timer = AnyEvent->timer(
                after => $self->retry_delay,
                cb    => sub {
                    undef $timer;
                    $do_try->($by_resp);
                    return;
                },
            );
        };
    $sub = sub {
        my ($resp_msg, $data, $error) = @_;
        if ($error) {
            $self->_debug(2, "send: failed");
            if( $try++ < $retry ) {
                $next_try->();
            }
            else {
                undef $next_try;
                my $errobj = $unpack ? undef : MR::IProto::Error->new(
                    request => $message,
                    error   => $error,
                    errno   => 0+$!,
                );
                $callback->($errobj, $error);
            }
        }
        else {
            my $ok = eval {
                die "Request and reply message code is different: $resp_msg != $req_msg\n"
                    unless $no_reply || $resp_msg == $req_msg;
                if ($unpack) {
                    $data = [ $unpack->($data) ];
                }
                else {
                    $data = $response_class->new( data => $data, request => $message );
                }
                1;
            };
            if($ok) {
                if( !$unpack && $data->retry && $try++ < $self->max_request_retries ) {
                    $next_try->(1);
                }
                else {
                    undef $next_try;
                    $callback->($data);
                }
            }
            else {
                undef $next_try;
                my $error = $@;
                my $errobj = $unpack ? undef : MR::IProto::Error->new(
                    request => $message,
                    error   => $error,
                    errno   => 0+$!,
                );
                $callback->($errobj, $error);
            }
        }
        return;
    };
    $do_try->();
    return;
}

sub _debug {
    my ($self, $level, $msg) = @_;
    return if $self->debug < $level;
    $self->debug_cb->($msg);
    return;
}

=back

=head1 SEE ALSO

L<MR::IProto::Cluster>, L<MR::IProto::Cluster::Server>, L<MR::IProto::Message>.

=cut

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
