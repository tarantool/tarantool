package MR::IProto;

use strict;

use Socket qw(PF_INET SOCK_STREAM SOL_SOCKET SO_SNDTIMEO SO_RCVTIMEO TCP_NODELAY);
use String::CRC32 qw(crc32);
use Time::HiRes qw/time sleep/;
use Fcntl;

use vars qw($VERSION $PROTO_TCP %sockets);
$VERSION = 0;

use overload '""' => sub { "$_[0]->{name}\[$_[0]->{_last_server}]" };

BEGIN {
    if (eval {require 5.8.3}) {
        require bytes;
        import bytes;
        require warnings;
        import warnings;
    }
}

$PROTO_TCP = 0;
%sockets = ();

sub DEFAULT_RETRY_DELAY         () { 0 }
sub DEFAULT_MAX_REQUEST_RETRIES () { 2 }
sub DEFAULT_TIMEOUT             () { 2 }

sub RR                          () { 1 }
sub HASH                        () { 2 }
sub KETAMA                      () { 3 }

sub confess (@) { die @_ };

sub DisconnectAll {
    close $_ foreach (values %sockets);
    %sockets = ();
}

sub new {
    my ($class, $args) = @_;
    my $self = {};
    bless $self, $class;

    $self->{debug}                = $args->{debug} || 0;
    $self->{balance}              = $args->{balance} && $args->{balance} eq 'hash-crc32' ? HASH() : RR();
    $self->{balance}              = KETAMA() if $args->{balance} && $args->{balance} eq 'ketama';
    $self->{rotateservers}        = 1 unless $args->{norotateservers};
    $self->{max_request_retries}  = $args->{max_request_retries} || DEFAULT_MAX_REQUEST_RETRIES();
    $self->{retry_delay}          = $args->{retry_delay} || DEFAULT_RETRY_DELAY();
    $self->{dump_no_ints}         = 1 if $args->{dump_no_ints};
    $self->{tcp_nodelay}          = 1 if $args->{tcp_nodelay};
    $self->{param}                = $args->{param};
    $self->{_last_server}         = '';

    $self->{name} = $args->{name} or ($self->{name}) = caller;

    my $servers = $args->{servers} || confess("${class}->new: no servers given");
    _parse_servers $self 'servers', $servers;

    if ($self->{balance} == RR()) {
        $self->{aviable_servers} = [@{$self->{servers}}]; #make copy
        $servers = $args->{broadcast_servers} || ''; # confess("${class}->new: no broadcast servers given");
        _parse_servers $self 'broadcast_servers', $servers;
    } elsif ($self->{balance} == KETAMA()) {
        $self->{'ketama'} = [];
        for my $s (@{$self->{'servers'}}) {
            $s->{'ok'} = 1;
            for my $i (0..10) {
               push @{$self->{'ketama'}}, [crc32($s->{'repr'}.$i), $s];
            }
        }
        @{$self->{'ketama'}} = sort {$a->[0] cmp $b->[0]} @{$self->{'ketama'}};
    }

    my $timeout = exists $args->{timeout} ? $args->{timeout} : DEFAULT_TIMEOUT();
    SetTimeout $self $timeout;

    confess "${class}: no servers given" unless @{$self->{servers}};
    return $self;
}

sub GetParam {
    return $_[0]->{param};
}

sub Chat {
    my ($self, %message) = @_;

    confess "wrong msg id" unless exists($message{msg});
    confess "wrong msg body" unless exists($message{payload}) or exists($message{data});
    confess "wrong msg reply" unless $message{no_reply} or exists($message{unpack});

    my $retries = $self->{max_request_retries};

    for (my $try = 1; $try <= $retries; $try++) {
        sleep $self->{retry_delay} if $try > 1 && $self->{retry_delay};
        $self->{debug} >= 1 && _debug $self "chat msg=$message{msg} $try of $retries total";
        my $ret = $self->Chat1(%message);

        if ($ret->{ok}) {
            return (wantarray ? @{$ret->{ok}} : $ret->{ok}->[0]);
        }
    }
    return undef;
}

sub Chat1 {
    my ($self, %message) = @_;

    confess "wrong msg id" unless exists($message{msg});
    confess "wrong msg body" unless exists($message{payload}) or exists($message{data});
    confess "wrong msg reply" unless $message{no_reply} or exists($message{unpack});

    my $server = _select_server $self $message{key};
    unless($server) {
        $self->{_error} .= 'All servers blocked by remote_stor_pinger';
        return;
    }

    my ($ret) = _chat $self $server, \%message;

    if ($ret->{fail} && $self->{rotateservers}) {
        $self->{debug} >= 2 && _debug $self "chat: failed";
    	_mark_server_bad $self;
    }

    return $ret;
}

# private methods
# order of method declaration is important, see perlobj

sub _parse_servers {
    my ($self, $key, $line) = @_;
    my @servers;
    my $weighted = $self->{balance} == HASH();
    foreach my $server (split(/,/, $line)) {
        if ($weighted) {
            my ($host, $port, $weight) = split /:/, $server;
            $weight ||= 1;
            push @servers, { host => $host, port => $port, ok => 1, repr => "$host:$port" } for (1..$weight);
        } else {
            my ($host, $port) = $server =~ /(.+):(\d+)/;
            push @servers, { host => $host, port => $port, repr => "$host:$port" }

        }
    }
    $self->{$key} = \@servers;
}

sub _chat {
    my ($self, $server, $message) = @_;
    _a_init $self $server, $message
        and _a_send $self
        and !$message->{no_reply}
        and _a_recv $self;
    return _a_close $self;
}

sub _a_init {
    my ($self, $server, $message, $async) = @_;

    _a_clear $self;
    $self->{_server} = $server;
    $self->{_message} = $message;
    $self->{_async} = !!$async;

    my $payload = $message->{payload} || do { no warnings 'uninitialized'; pack($message->{pack} || 'L*', @{$message->{data}}) };
    $self->{_sync} = exists $message->{sync} ? $message->{sync} : int(rand 0xffffffff);
    my $header = pack('LLL', $message->{msg}, length($payload), $self->{_sync});
    $self->{_write_buf} = $header . $payload;
    $self->{debug} >= 5 && _debug_dump $self '_pack_request: header ', $header;
    $self->{debug} >= 5 && _debug_dump $self '_pack_request: payload ', $payload;

    $self->{_start_time} = time;
    $self->{_connecting} = 1;
    $self->{sock} = $sockets{$server->{repr}} || _connect $self $server, \$self->{_error}, $async;
    _set_blocking($self->{sock},!$async) if $self->{sock};
    return $self->{sock};
}

sub _a_send {
    my ($self) = @_;

    $self->{_connecting} = 0;
    $self->{_timedout} = 0;
    while(length $self->{_write_buf}) {
        local $! = 0;
        my $res = syswrite($self->{sock}, $self->{_write_buf});
        if(defined $res && $res == 0) {
            _debug $self "$!" and next if $!{EINTR};
            $self->{_error} .= "Server unexpectedly closed connection (${\length $self->{_write_buf}} bytes unwritten)";
            return;
        }
        if(!defined $res) {
            _debug $self "$!" and next if $!{EINTR};
            $self->{_timedout} = !!$!{EAGAIN};
            $self->{_error} .= $! unless $self->{_async} && $!{EAGAIN};
            return;
        }
        substr $self->{_write_buf}, 0, $res, '';
    }

    die "We should never get here" if length $self->{_write_buf};

    delete $self->{_write_buf};
    $self->{_read_header} = 1;
    $self->{_to_read} = 12;

    return 1;
}

sub _a_recv {
    my ($self) = @_;
    $self->{_connecting} = 0;
    $self->{_timedout} = 0;
    while(1) {
        local $! = 0;
        my $res;
        while($self->{_to_read} and $res = sysread($self->{sock}, my $buffer, $self->{_to_read} ) ) {
            $self->{_to_read} -= $res;
            $self->{_buf} .= $buffer;
            $self->{debug} >= 6 && _debug_dump $self '_a_recv: ', $buffer;
        }
        if(defined $res && $res == 0 && $self->{_to_read}) {
            _debug $self "$!" and next if $!{EINTR};
            $self->{_error} .= "Server unexpectedly closed connection ($self->{_to_read} bytes unread)";
            return;
        }
        if(!defined $res) {
            _debug $self "$!" and next if $!{EINTR};
            $self->{_timedout} = !!$!{EAGAIN};
            $self->{_error} .= $! unless $self->{_async} && $!{EAGAIN};
            return;
        }

        last unless $self->{_read_header};
        if($self->{_read_header} && length $self->{_buf} >= 12) {
            my ($response_msg, $to_read, $response_sync) = unpack('L3', substr($self->{_buf},0,12,''));
            unless ($response_msg == $self->{_message}->{msg} and $response_sync == $self->{_sync}) {
                $self->{_error} .= "unexpected reply msg($response_msg != $self->{_message}->{msg}) or sync($response_sync != $self->{_sync})";
                return;
            }
            $self->{_read_header} = 0;
            $self->{_to_read} = $to_read - length $self->{_buf};
        }
    }

    die "We should never get here" if $self->{_to_read};
    return 1;
}

sub _a_close {
    my ($self, $error) = @_;
    $error = '' unless defined $error;
    $self->{_timedout} ||= $error eq 'timeout';
    $self->{_error} ||= $error;
    my $ret;
    if ($self->{_error} || $self->{_timedout}) {
        _close_sock $self $self->{_server}; # something went wrong, close socket just in case
        $self->{_error} = "Timeout ($self->{_error})" if $self->{_timedout};
        $self->{debug} >= 0 && _debug $self "failed with: $self->{_error}";
        $ret = { fail => $self->{_error}, timeout => $self->{_timedout} };
    } else {
        $self->{debug} >= 5 && _debug_dump $self '_unpack_body: ', $self->{_buf};
        $ret = { ok => [ $self->{_message}->{no_reply} ? (0) : $self->{_message}->{unpack}->($self->{_buf}) ] };
    }
    _a_clear $self;
    return $ret;
}

sub _a_is_reading {
    my ($self) = @_;
    return $self->{_to_read} && $self->{sock};
}

sub _a_is_writing {
    my ($self) = @_;
    return exists $self->{_write_buf} && $self->{sock};
}

sub _a_is_connecting {
    my ($self) = @_;
    return $self->{_connecting} && $self->{sock};
}

sub _a_clear {
    my ($self) = @_;
    $self->{_buf} = $self->{_error} = '';
    $self->{_to_read} = undef;
    $self->{_data} = undef;
    $self->{_server} = undef;
    $self->{_message} = undef;
    $self->{_sync} = undef;
    $self->{_read_header} = undef;
    $self->{_to_read} = undef;
    $self->{_timedout} = undef;
    $self->{_connecting} = undef;
    delete $self->{_write_buf};
}

sub _select_server {
    my ($self, $key) = @_;
    my $n = @{$self->{servers}};
    while($n--) {
        if ($self->{balance} == RR()) {
            $self->{current_server} ||= $self->_balance_rr;
        } elsif ($self->{balance} == KETAMA()) {
            $self->{current_server} = $self->_balance_ketama($key);
        } else {
            $self->{current_server} = $self->_balance_hash($key);
        }
        last unless $self->{current_server};
        last if $self->{current_server};
    }
    if($self->{current_server}) {
        $self->{_last_server} = $self->{current_server}->{repr};
    }
    return $self->{current_server};
}

sub _mark_server_bad {
    my ($self) = @_;
    if ($self->{balance} == HASH()) {
        delete($self->{current_server}->{ok});
    }
    delete($self->{current_server});
}

sub _balance_rr {
    my ($self) = @_;
    if (scalar(@{$self->{servers}}) == 1) {
        return $self->{servers}->[0];
    } else {
        $self->{aviable_servers} = [@{$self->{servers}}] if (scalar(@{$self->{aviable_servers}}) == 0);
        return splice(@{$self->{aviable_servers}}, int(rand(@{$self->{aviable_servers}})), 1);
    }
}

sub _balance_hash {
    my ($self, $key) = @_;
    my ($hash_, $hash, $server);

    $hash = $hash_ = crc32($key) >> 16;
    for (0..19) {
        $server = $self->{servers}->[$hash % @{$self->{servers}}];
        return $server if $server->{ok};
        $hash += crc32($_, $hash_) >> 16;
    }

    return $self->{servers}->[rand @{$self->{servers}}]; #last resort
}

sub _balance_ketama {
    my ($self, $key) = @_;

    my $idx = crc32($key);

    for my $a (@{$self->{'ketama'}}) {
       next unless ($a);
       return $a->[1] if ($a->[0] >= $idx);
    }

    return $self->{'ketama'}->[0]->[1];
}

sub _set_sock_timeout ($$) { # not a class method!
    my ($sock, $tv) = @_;
    return (
        setsockopt $sock, SOL_SOCKET, SO_SNDTIMEO, $tv
         and setsockopt $sock, SOL_SOCKET, SO_RCVTIMEO, $tv
    );
}

sub _set_blocking ($$) {
    my ($sock, $blocking) = @_;
    my $flags = 0;
    fcntl($sock, F_GETFL, $flags) or die $!;
    if($blocking) {
        $flags &= ~O_NONBLOCK;
    } else {
        $flags |=  O_NONBLOCK;
    }
    fcntl($sock, F_SETFL, $flags) or die $!;
}

sub _connect {
    my ($self, $server, $err, $async) = @_;

    $self->{_timedout} = 0;
    $self->{debug} >= 4 && _debug $self "connecting";

    my $sock = "$server->{host}:$server->{port}";
    my $proto = $PROTO_TCP ||= getprotobyname('tcp');
    do {
        no strict 'refs';
        socket($sock, PF_INET, SOCK_STREAM, $proto);
    };

    _set_sock_timeout $sock, $self->{timeout_timeval} unless $async;
    _set_blocking($sock,0) if $async;

    my $sin = Socket::sockaddr_in($server->{'port'}, Socket::inet_aton($server->{'host'}));
    while(1) {
        local $! = 0;
        unless(connect($sock, $sin)) {
            _debug $self "$!" and next if $!{EINTR};
            $self->{_timedout} = !!$!{EINPROGRESS};
            if (!$async || !$!{EINPROGRESS}) {
                $$err .= "cannot connect: $!";
                close $sock;
                return undef;
            }
        }
        last;
    }

    if($self->{tcp_nodelay}) {
        setsockopt($sock, $PROTO_TCP, TCP_NODELAY, 1);
    }

    $self->{debug} >= 1 && _debug $self "connected";
    return $sockets{$server->{repr}} = $sock;
}

sub SetTimeout {
    my ($self, $timeout) = @_;
    $self->{timeout} = $timeout;

    my $sec  = int $timeout; # seconds
    my $usec = int( ($timeout - $sec) * 1_000_000 ); # micro-seconds
    $self->{timeout_timeval} = pack "LL", $sec, $usec; # struct timeval;

    _set_sock_timeout $sockets{$_}, $self->{timeout_timeval}
        for
            grep {exists $sockets{$_}}
                map {$_->{repr}}
                    @{ $self->{servers} };
}

sub _close_sock {
    my ($self, $server) = @_;

    if ($sockets{$server->{repr}}) {
        $self->{debug} >= 1 && _debug $self 'closing socket';
        close $sockets{$server->{repr}};
        delete $sockets{$server->{repr}};
    }
}

sub _debug {
    my ($self, $msg)= @_;
    my $server = $self->{current_server} && $self->{current_server}->{repr};
    my $sock = $self->{sock} || 'none';
    $server &&= "$server($sock) ";

    warn("$self->{name}: $server$msg\n");
    1;
}

sub _debug_dump {
    my ($self, $msg, $datum) = @_;
    my $server = $self->{current_server} && $self->{current_server}->{repr};
    my $sock = $self->{sock} || 'none';
    $server &&= "$server($sock) ";

    unless($self->{dump_no_ints}) {
        $msg .= join(' ', unpack('L*', $datum));
        $msg .= ' > ';
    }
    $msg .= join(' ', map { sprintf "%02x", $_ } unpack("C*", $datum));
    warn("$self->{name}: $server$msg\n");
}

package MR::IProto::Async;
use Time::HiRes qw/time/;
use List::Util qw/min shuffle/;

sub DEFAULT_TIMEOUT()           { 10 }
sub DEFAULT_TIMEOUT_SINGLE()    {  4 }
sub DEFAULT_TIMEOUT_CONNECT()   {  1 }
sub DEFAULT_RETRY()             {  3 }
sub IPROTO_CLASS()              { 'MR::IProto' }

use overload '""' => sub { $_[0]->{name}.'[async]' };

BEGIN { *confess = \&MR::IProto::confess }

sub new {
    my ($class, %opt) = @_;
    ($opt{name}) = caller unless $opt{name};
    my $self = bless {
        name                        => $opt{name},
        iproto_class                => $opt{iproto_class}         || $class->IPROTO_CLASS,
        timeout                     => $opt{timeout}              || $class->DEFAULT_TIMEOUT(),          # over-all-requests timeout
        timeout_single              => $opt{timeout_single}       || $class->DEFAULT_TIMEOUT_SINGLE(),   # per-request timeout
        timeout_connect             => $opt{timeout_connect}      || $class->DEFAULT_TIMEOUT_CONNECT(),  # timeout to do connect()
        max_request_retries         => $opt{max_request_retries}  || $class->DEFAULT_RETRY(),
        debug                       => $opt{debug}                || 0,
        servers                     => [ ],
        requests                    => [ @{$opt{requests}||[]} ],
        nreqs                       => $opt{requests} ? scalar(@{$opt{requests}}) : 0,
        servers_used                => {},
        nserver                     => 0,
        working                     => {},
    }, $class;

    _parse_servers $self 'servers', $opt{servers};
    confess "no servers given" unless @{$self->{servers}};
    @{$self->{servers}} = shuffle @{$self->{servers}};

    return $self;
}

sub Request { # MR::IProto::Chat()-compatible
    my ($self, %message) = @_;

    confess "wrong msg id" unless exists($message{msg});
    confess "wrong msg body" unless exists($message{payload}) or exists($message{data});
    confess "wrong msg reply" unless $message{no_reply} or exists($message{unpack});

    push @{$self->{requests}}, \%message;
}

sub Results {
    my ($self) = @_;

    my (@done);
    my $timeout_single = $self->{timeout_single};
    my $timeout_connect = $self->{timeout_connect};
    my $nreqs = @{$self->{requests}};

    unless ($nreqs) {
        warn "No requests; return nothing;\n";
        return;
    }

    my $t0 = time;

    my $err = 'timeout';
    my $working = $self->{working};
    confess "we have something what we should not" if %$working;

    while(scalar@{$self->{requests}}) {
        int _init_req $self $self->{requests}->[0], $working
            or last;
        shift @{$self->{requests}};
    }

    unless (%$working) {
        warn "Connection error; return nothing;\n";
        return;
    }

    my $deadline = $self->{timeout} + time;

    MAINLOOP:
    while((my $timeout = $deadline - time) > 0 && %$working) {
        my ($r,$w) = ('','');
        vec($r, $_, 1) = 1 for grep {  $working->{$_}->{_conn}->_a_is_reading } keys %$working;
        vec($w, $_, 1) = 1 for grep { !$working->{$_}->{_conn}->_a_is_reading } keys %$working;
        my $t1 = time;
        $timeout = min $timeout, map { ($_->{_conn}->_a_is_connecting() ? $timeout_connect : $timeout_single) - ($t1 - $_->{_conn}->{_start_time}) } values %$working;
        my $found = select($r, $w, undef, $timeout);

        my $time = time;
        for my $fd (keys %$working) {
            my $req = $working->{$fd};
            my $conn = $req->{_conn};

            my $restart = 0;
            if(vec($w, $fd, 1) && ($conn->_a_is_writing || $conn->_a_is_connecting)) {
                $restart = 1 if !$conn->_a_send && !$conn->{_timedout};
            } elsif(vec($r, $fd, 1) && $conn->_a_is_reading) {
                if($conn->_a_recv) {
                    delete $working->{$fd};
                    _server_free $self $req->{_server};
                    push @done, $req;
                    if(@{$self->{requests}}) {
                        _init_req $self shift@{$self->{requests}}, $working or _die $self "shit1";
                    }
                } elsif(!$conn->{_timedout}) {
                    $restart = 2;
                }
            } else {
                if($conn->{_start_time} + ($conn->_a_is_connecting() ? $timeout_connect : $timeout_single) < $time) {
                    $restart = -1;
                }
            }

            next unless $restart;

            delete $working->{$fd};
            $conn->_a_close($restart < 0 ? 'timeout' : 'shit happened!');
            my $oldserver = $req->{_server};
            my $res = _init_req $self $req, $working;
            if( !$res ) {
                $err = 'one of requests has failed, aborting';
                last MAINLOOP;
            } elsif (! int $res ) {
                push @{$self->{requests}}, $req;
            }
            _server_free $self $oldserver;
        }
    }

    my $t9 = time;
    warn sprintf "$self->{name}: fetched %d requests for %.4f sec (%d/%d done)\n", $self->{nreqs}, $t9-$t0, scalar@done, $nreqs;

    if(%$working) {
        warn "$self->{name}: ${\scalar values %$working} requests not fetched\n";
        $_->{_conn}->_a_close($err) for values %$working;
        # warn "return nothing\n";
        # return;
    }

    for my $req (@done) {
        my $result = $req->{_conn}->_a_close;
        # warn "ret nothing\n" and return unless $result->{ok};
        $req = $result->{ok};
    }

    return grep { $_ } @done;
}

sub _init_req {
    my ($self, $req) = @_;

    $req->{_try} ||= 0;
    return if $req->{_try} >= $self->{max_request_retries};

    $req->{_conn} = undef;
    $req->{_server} = undef;

    my $server = _select_server $self or return '0E0';
    my $conn = $self->{iproto_class}->new({
        servers => $server->{repr},
        debug   => $self->{debug},
    });
    $conn->_select_server; # fiction

    my $sock = $conn->_a_init($server, $req, 'async') or do{ $conn->_a_close; warn "can't init connection: $server->{repr}"; return '0E0'; };
    my $fd = fileno $sock or do{ $conn->_a_close; _die $self "connection has no FD", $server->{repr}; };

    my $working = $self->{working};
    _die $self "FD $fd already exists in workers", $server->{repr} if exists $working->{$fd};
    $working->{ $fd } = $req;
    _server_use $self $server;

    $req->{_conn} = $conn;
    $req->{_server} = $server;
    ++$req->{_try};
    ++$self->{nreqs};

    return $conn && 1;
}

sub _select_server {
    my ($self, $disable_servers) = @_;
    $disable_servers ||= {};

    my $servers = $self->{servers};
    my $servers_used = $self->{servers_used};
    my $nserver = $self->{nserver};

    my $i = 1;
    while($i <= @$servers) {
        my $repr = $servers->[($i+$nserver)%@$servers]->{repr};
        next if $servers_used->{$repr};
        next if $disable_servers->{$repr};
        last;
    } continue {
        ++$i;
    }

    return if $i > @$servers;

    $self->{nserver} = $nserver = ($i+$nserver)%@$servers;
    return $servers->[$nserver];
}

sub _server_use {
    my ($self,$server) = @_;
    _die $self "want to use used server:$server->{repr}:" if $self->{servers_used}->{$server->{repr}};
    ++ $self->{servers_used}->{$server->{repr}};
}

sub _server_free {
    my ($self,$server) = @_;
    _die $self "want to free free server:$server->{repr}:" unless $self->{servers_used}->{$server->{repr}};
    $self->{servers_used}->{$server->{repr}} = 0;
}

sub _parse_servers {
    my ($self, $key, $line) = @_;

    my @servers;
    foreach my $server (split(/,/, $line)) {
        my ($host, $port) = $server =~ /(.+):(\d+)/ or confess "bad server: $server!";
        push @servers, { host => $host, port => $port, repr => "$host:$port" }
    }

    $self->{$key} = \@servers;
}

sub _die {
    my ($self, @e) = @_;
    die "$self->{name}: ".join('; ', @e);
}

1;

