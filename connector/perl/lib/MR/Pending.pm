package MR::Pending;
use Mouse;
use Time::HiRes qw/time/;

has maxtime => (
    is        => 'rw',
    isa       => 'Num',
    predicate => "_has_maxtime",
    default   => 6.0,
);

has itertime => (
    is        => 'rw',
    isa       => 'Num',
    predicate => "_has_itertime",
    default   => 0.1,
);

has name => (
    is        => 'rw',
    isa       => 'Str',
    required  => 1,
);

has onidle => (
    is        => 'rw',
    isa       => 'CodeRef',
    predicate => "_has_onidle",
);

has _pending => (
    is        => 'ro',
    isa       => 'HashRef[MR::Pending::Item]',
    default   => sub { {} },
);

has exceptions => (
    is       => 'rw',
    isa      => 'Int',
    default  => 0,
);

has _exceptions => (
    is       => 'ro',
    isa      => 'ArrayRef',
    default  => sub { [] },
);

has _waitresult => (
    is   => 'rw',
    isa  => 'ArrayRef',
);

around BUILDARGS => sub {
    my $orig  = shift;
    my $class = shift;
    my %args = @_;
    if(my $p = delete $args{pending}) {
        $args{_pending} = { map { $_->id => $_ } @$p };
    }
    $class->$orig(%args);
};

sub runcatch {
    my ($self, $code, @param) = @_;
    my $ret;
    unless(eval { $ret = &$code(@param); 1 }) {
        push @{$self->_exceptions}, $@;
        $self->exceptions($self->exceptions + 1);
    }
    return $ret;
}

sub add {
    my ($self, @p) = @_;
    my $p = $self->_pending;
    for my $add (@p) {
        die if exists $p->{$add->id};
        $p->{$add->id} = $add;
    }
    return $self;
}

sub remove {
    my ($self, @p) = @_;
    my $p = $self->_pending;
    for my $del (@p) {
        die unless exists $p->{$del->id};
        delete $p->{$del->id};
    }
    return $self;
}

sub send {
    my ($self) = @_;
    my $pending = $self->_pending;
    foreach my $shard ( grep { $pending->{$_}->is_sleeping } keys %$pending ) {
        my $pend = $pending->{$shard};
        if ($pend->try < $pend->retry) {
            next unless $pend->is_timeout;
            $pend->set_pending_mode(scalar $self->runcatch($pend->onretry, ($pend->id, $pend, $self)));
        } else {
            delete $pending->{$shard};
            $self->runcatch($pend->onerror, ($pend->id, "no success after @{[$pend->try]} retries", $pend, $self));
        }
    }
    return $self;
}

sub wait {
    my ($self) = @_;
    my $pending = $self->_pending;

    my $in = '';
    vec($in, $_->fileno, 1) = 1 for grep { $_->is_pending } values %$pending;

    my $n;
    while(1) {
        my $ein = my $rin = $in;
        $n = CORE::select($rin, undef, $ein, $self->itertime);
        $self->_waitresult([$rin,$ein]);
        if ($n < 0) {
            next if $!{EINTR};
            warn $self->name.": select() failed: $!";
            return undef;
        }
    }

    if ($n == 0) {
        $self->runcatch($self->onidle, ($self)) if $self->_has_onidle;
        return 0;
    }

    return $n;
}

sub recv {
    my ($self) = @_;
    my $pending = $self->_pending;
    my ($rin, $ein) = @{$self->_waitresult};

    for my $shard (grep { $pending->{$_}->is_pending } keys %$pending) {
        my $pend = $pending->{$shard};
        my $fileno = $pend->fileno;
        if (vec($rin, $fileno, 1)) {
            if (my $list = $pend->continue) {
                if (ref $list) {
                    if(defined(my $okay = $self->runcatch($pend->onok, ($pend->id, $list, $pend, $self)))) {
                        if($okay) {
                            delete $pending->{$shard};
                        } else {
                            $pend->set_sleeping_mode;
                        }
                    }
                }
            } else {
                $pend->close("error while receiving (".$pend->last_error.")");
            }
        } elsif (vec($ein, $fileno, 1)) {
            $pend->close("connection reset (".$pend->last_error.")");
        } elsif ($pend->is_timeout) {
            $pend->close("timeout (".$pend->last_error.")");
        }
    }

    return $self;
}

sub finish {
    my ($self) = @_;
    my $timeout = !$self->exceptions;
    my $pending = $self->_pending;
    for my $shard (grep { !$pending->{$_}->is_done } keys %$pending) {
        my $pend = delete $pending->{$shard};
        $pend->close($timeout ? "timeout" : "aborted due to external exception");
        $self->runcatch($pend->onerror, ($pend->id, "timeout", $pend, $self)) if $timeout;
    }
    return $self;
}

sub iter {
    my ($self) = @_;

    $self->send or return;
    return if $self->exceptions;

    my $res = $self->wait;
    return if $self->exceptions;
    return unless defined $res;
    return 1 unless $res;

    $self->recv or return;
    return if $self->exceptions;

    return 1;
}

sub work {
    my ($self) = @_;

    my $pending = $self->_pending;
    my $time0 = time;

    while(%$pending and time() - $time0 <= $self->maxtime) {
        last unless $self->iter;
    }
    $self->finish;
    $self->check_exceptions('raise');
}

sub check_exceptions {
    my ($self, $raise) = @_;
    my $e = $self->_exceptions;
    return unless $e && @$e;
    my $str = "$$: PENDING EXCEPTIONS BEGIN\n".join("\n$$:###################\n", @$e)."$$: PENDING EXCEPTIONS END";
    die $str if $raise;
    warn $str if defined $raise;
    return $str;
}

no Mouse;
__PACKAGE__->meta->make_immutable();




package MR::Pending::Item;
use Mouse;
use Time::HiRes qw/time/;
use Carp;

has id => (
    is        => 'ro',
    isa       => 'Str',
    required  => 1,
);

has $_ => (
    is        => 'ro',
    isa       => 'CodeRef',
    predicate => "_has_$_",
    required  => 1,
) for qw/onok onerror onretry/;

has $_ => (
    is        => 'rw',
    isa       => 'Num',
    predicate => "_has_$_",
) for qw/timeout retry_delay/;

has retry => (
    is        => 'rw',
    isa       => 'Int',
    predicate => "_has_retry",
);

has status_unknown => (
    is       => 'rw',
    isa      => 'Bool',
    default  => 0,
);

has _done => (
    is       => 'rw',
    isa      => 'Bool',
    default  => 0,
);

has _time => (
    is       => 'rw',
    isa      => 'Num',
    default  => 0,
);

has _connection => (
    is       => 'rw',
    isa      => 'Maybe[MR::IProto::Connection::Sync]',
    clearer  => '_clear__connection',
    predicate=> '_has__connection',
    handles  => [qw/last_error/],
);

has fileno => (
    is      => 'ro',
    isa     => 'Int',
    lazy    => 1,
    default => sub { Carp::confess "not connected!" unless $_[0]->_connection; $_[0]->_connection->fh->fileno },
    clearer => '_clear_fileno',
);

has _continue => (
    is       => 'rw',
    isa      => 'Maybe[CodeRef]',
    clearer  => '_clear__continue',
);

has _postprocess => (
    is       => 'rw',
    isa      => 'Maybe[CodeRef]',
    clearer  => '_clear__postprocess',
);

has try => (
    is       => 'ro',
    isa      => 'Int',
    default  => 0,
    writer   => '_set_try',
);

# has bornat => (
#     is       => 'ro',
#     isa      => 'Str',
#     default  => sub { "[".join("-", $_[0], $$, time(), Carp::longmess())."]"; },
# );

sub is_done     { return  $_[0]->_done }
sub is_pending  { return !$_[0]->_done &&  $_[0]->_has__connection }
sub is_sleeping { return !$_[0]->_done && !$_[0]->_has__connection }

sub set_pending_mode {
    my ($self, $cont) = @_;
    $self->_done(0);
    $self->_clear__connection;
    $self->_clear__continue;
    $self->_clear__postprocess;
    $self->_clear_fileno;
    if($cont) {
        $self->_connection($cont->{connection});
        $self->_continue($cont->{continue});
        $self->_postprocess($cont->{postprocess});
    }
    if (@_ > 1) {
        $self->status_unknown(0);
        $self->_set_try($self->try + 1);
    }
    $self->_time(time);
    return $self;
}

sub set_sleeping_mode {
    $_[0]->set_pending_mode;
}

sub is_timeout {
    my ($self, $timeout) = @_;
    $timeout ||= $self->is_pending ? $self->timeout : $self->retry_delay;
    return time() - $self->_time > $timeout;
}

sub continue {
    my ($self) = @_;
    my $is_cont = 0;
    my @list;
    if (eval{@list = $self->_continue->($is_cont); 1}) {
        if ($is_cont) {
            $self->_clear_fileno;
            $self->_connection($list[0]->{connection});
            $self->_continue($list[0]->{continue});
            $self->_time(time);
            return 1;
        } else {
            $self->_done(1);
            if (my $pp = $self->_postprocess) {
                &$pp(\@list);
            }
            return \@list;
        }
    }
    return 0;
}

sub close {
    my ($self, $reason) = @_;
    if ($self->is_pending) {
        $self->_connection->Close($reason);
        $self->status_unknown(1);
    }
    $self->set_sleeping_mode;
}

sub DEMOLISH {
    my ($self) = @_;
    warn "$$ FORGOTTEN $self" if $self->is_pending;
    #Carp::cluck "$$ FORGOTTEN $self" if $self->is_pending;
}

no Mouse;
__PACKAGE__->meta->make_immutable();

1;
