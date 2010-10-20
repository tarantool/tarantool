package MR::SilverBox;

use strict;
use warnings;
use Scalar::Util qw/looks_like_number/;
use List::MoreUtils qw/each_arrayref/;

use MR::IProto ();
use MR::Storage::Const ();

use constant WANT_RESULT => 1;

sub IPROTOCLASS () { 'MR::IProto' }
sub ERRSTRCLASS () { 'MR::Storage::Const::Errors::SilverBox' }

use vars qw/$VERSION/;
$VERSION = 0;

BEGIN { *confess = \&MR::IProto::confess }

sub new {
    my ($class, $arg) = @_;
    my $self;

    $arg = { %$arg };
    $self->{name}            = $arg->{name}      || ref$class || $class;
    $self->{timeout}         = $arg->{timeout}   || 23;
    $self->{retry    }       = $arg->{retry}     || 1;
    $self->{select_retry}    = $arg->{select_retry} || 3;
    $self->{softretry}       = $arg->{softretry} || 3;
    $self->{debug}           = $arg->{'debug'}   || 0;
    $self->{ipdebug}         = $arg->{'ipdebug'} || 0;
    $self->{raise}           = 1;
    $self->{raise}           = $arg->{raise} if exists $arg->{raise};
    $self->{default_raw}     = exists $arg->{default_raw} ? $arg->{default_raw} : 0;
    $self->{hashify}         = $arg->{'hashify'} if exists $arg->{'hashify'};
    $self->{select_timeout}  = $arg->{select_timeout} || $self->{timeout};
    $self->{iprotoclass}     = $arg->{iprotoclass} || $class->IPROTOCLASS;
    $self->{errstrclass}     = $arg->{errstrclass} || $class->ERRSTRCLASS;
    $self->{_last_error}     = 0;

    $arg->{namespaces} = [@{ $arg->{namespaces} }];
    my %namespaces;
    for my $ns (@{$arg->{namespaces}}) {
        $ns = { %$ns };
        my $namespace = $ns->{namespace};
        confess "ns[?] `namespace' not set" unless defined $namespace;
        confess "ns[$namespace] already defined" if $namespaces{$namespace} || $ns->{name}&&$namespaces{$ns->{name}};
        confess "ns[$namespace] no indexes defined" unless $ns->{indexes} && @{$ns->{indexes}};
        $namespaces{$namespace} = $ns;
        $namespaces{$ns->{name}} = $ns if $ns->{name};
        confess "ns[$namespace] bad format `$ns->{format}'" if $ns->{format} =~ m/[^&lLsScC ]/;
        $ns->{format} =~ s/\s+//g;
        my @f = split //, $ns->{format};
        $ns->{byfield_unpack_format} = [ map { /&/ ? 'w/a*' : "x$_" } @f ];
        $ns->{field_format}  = [         map { /&/ ? 'a*'   : $_    } @f ];
        $ns->{unpack_format}  = join('', @{$ns->{byfield_unpack_format}});
        $ns->{append_for_unpack} = '' unless defined $ns->{append_for_unpack};
        $ns->{check_keys} = {};
        $ns->{string_keys} = { map { $_ => 1 } grep { $f[$_] eq '&' } 0..$#f };
        my $inames = $ns->{index_names} = {};
        my $i = -1;
        for my $index (@{$ns->{indexes}}) {
            ++$i;
            confess "ns[$namespace]index[($i)] no name given" unless length $index->{index_name};
            my $index_name = $index->{index_name};
            confess "ns[$namespace]index[$index_name($i)] no indexes defined" unless $index->{keys} && @{$index->{keys}};
            confess "ns[$namespace]index[$index_name($i)] already defined" if $inames->{$index_name} || $inames->{$i};
            $index->{id} = $i unless defined $index->{id};
            $inames->{$i} = $inames->{$index_name} = $index;
            int $_ == $_ and $_ >= 0 and $_ < @f or confess "ns[$namespace]index[$index_name] bad key `$_'" for @{$ns->{keys}};
            $ns->{check_keys}->{$_} = int !! $ns->{string_keys}->{$_} for @{$index->{keys}};
            $index->{string_keys} ||= $ns->{string_keys};
        }
        if( @{$ns->{indexes}} > 1 ) {
            confess "ns[$namespace] default_index not given" unless defined $ns->{default_index};
            confess "ns[$namespace] default_index $ns->{default_index} does not exist" unless $inames->{$ns->{default_index}};
        } else {
            $ns->{default_index} ||= 0;
        }
    }
    $self->{namespaces} = \%namespaces;
    if (values %namespaces > 1) {
        confess "default_namespace not given" unless defined $arg->{default_namespace};
        confess "default_namespace $arg->{default_namespace} does not exist" unless $namespaces{$arg->{default_namespace}};
        $self->{default_namespace} = $arg->{default_namespace};
    } else {
        $self->{default_namespace} = $arg->{default_namespace} || $arg->{namespaces}->[0]->{namespace};
    }
    bless $self, $class;
    $self->_connect($arg->{'servers'});
    return $self;
}

sub _debug {
    if($_[0]->{warn}) {
        &{$_[0]->{warn}};
    } else {
        warn "@_[1..$#_]\n";
    }
}

sub _connect {
    my ($self, $servers) = @_;
    $self->{server} = $self->{iprotoclass}->new({
        servers       => $servers,
        name          => $self->{name},
        debug         => $self->{'ipdebug'},
        dump_no_ints  => 1,
    });
}

sub ErrorStr {
    my ($self, $code) = @_;
    return $self->{_last_error_msg} if $self->{_last_error} eq 'fail';
    return $self->{errstrclass}->ErrorStr($code || $self->{_last_error});
}

sub Error {
    return $_[0]->{_last_error};
}

sub _chat {
    my ($self, %param) = @_;
    my $orig_unpack = delete $param{unpack};

    $param{unpack} = sub {
        my $data = $_[0];
        confess __LINE__."$self->{name}: [common]: Bad response" if length $data < 4;
        my ($full_code, @err_code) = unpack('LX[L]CCCC', substr($data, 0, 4, ''));
        # $err_code[0] = severity: 0 -> ok, 1 -> transient, 2 -> permanent;
        # $err_code[1] = description;
        # $err_code[3] = da box project;
        return (\@err_code, \$data, $full_code);
    };

    my $timeout = $param{timeout} || $self->{timeout};
    my $retry = $param{retry} || $self->{retry};
    my $soft_retry = $self->{softretry};
    my $retry_count = 0;

    while ($retry > 0) {
        $retry_count++;

        $self->{_last_error} = 0x77777777;
        $self->{server}->SetTimeout($timeout);
        my $ret = $self->{server}->Chat1(%param);
        my $message;

        if (exists $ret->{ok}) {
            my ($ret_code, $data, $full_code) = @{$ret->{ok}};
            $self->{_last_error} = $full_code;
            if ($ret_code->[0] == 0) {
                my $ret = $orig_unpack->($$data,$ret_code->[3]);
                confess __LINE__."$self->{name}: [common]: Bad response (more data left)" if length $$data > 0;
                return $ret;
            }

            $message = $self->{errstrclass}->ErrorStr($full_code);
            $self->_debug("$self->{name}: $message") if $self->{debug} >= 1;
            if ($ret_code->[0] == 2) { #fatal error
                $self->_raise($message) if $self->{raise};
                return 0;
            }

            # retry if error is soft even in case of update e.g. ROW_LOCK
            if ($ret_code->[0] == 1 and --$soft_retry > 0) {
                --$retry if $retry > 1;
                sleep 1;
                next;
            }
        } else { # timeout has caused the failure if $ret->{timeout}
            $self->{_last_error} = 'fail';
            $message ||= $self->{_last_error_msg} = $ret->{fail};
            $self->_debug("$self->{name}: $message") if $self->{debug} >= 1;
        }

        last unless --$retry;

        sleep 1;
    };

    $self->_raise("no success after $retry_count tries\n") if $self->{raise};
}

sub _raise {
    my ($self, $msg) = @_;
    die $msg;
}

sub _validate_param {
    my ($self, $args, @pnames) = @_;
    my $param = ref $args->[-1] eq 'HASH' ? pop @$args: {};

    foreach my $pname (keys %$param) {
        confess "$self->{name}: unknown param $pname\n" if grep { $_ eq $pname } @pnames == 0;
    }

    $param->{namespace} = $self->{default_namespace} unless defined $param->{namespace};
    confess "$self->{name}: bad namespace `$param->{namespace}'" unless exists $self->{namespaces}->{$param->{namespace}};
    my $ns = $self->{namespaces}->{$param->{namespace}};
    $param->{use_index} = $ns->{default_index} unless defined $param->{use_index};
    confess "$self->{name}: bad index `$param->{use_index}'" unless exists $ns->{index_names}->{$param->{use_index}};
    $param->{index} = $ns->{index_names}->{$param->{use_index}};
    return ($param, map { /namespace/ ? $self->{namespaces}->{$param->{namespace}} : $param->{$_} } @pnames);
}


sub Insert {
    my ($param, $namespace) = $_[0]->_validate_param(\@_, qw/namespace _flags/);
    my ($self, @tuple) = @_;

    $self->_debug("$self->{name}: INSERT(@{[map {qq{`$_'}} @tuple]})") if $self->{debug} >= 3;

    my $flags = $param->{_flags} || 0;
    my $chkkey = $namespace->{check_keys};
    my $fmt = $namespace->{field_format};
    for (0..$#tuple) {
        confess "$self->{name}: ref in tuple $_=`$tuple[$_]'" if ref $tuple[$_];
        no warnings 'uninitialized';
        if(exists $chkkey->{$_}) {
            if($chkkey->{$_}) {
                confess "$self->{name}: undefined key $_" unless defined $tuple[$_];
            } else {
                confess "$self->{name}: not numeric key $_=`$tuple[$_]'" unless looks_like_number($tuple[$_]) && int($tuple[$_]) == $tuple[$_];
            }
        }
        $tuple[$_] = pack($fmt->[$_], $tuple[$_]);
    }

    $self->_debug("$self->{name}: INSERT[${\join '   ', map {join' ',unpack'(H2)*',$_} @tuple}]") if $self->{debug} >= 4;

    $self->_chat (
        msg => 13,
        payload => pack("LLL (w/a*)*", $namespace->{namespace}, $flags, scalar(@tuple), @tuple),
        unpack => sub { $self->_unpack_affected($flags, $namespace, @_) }
    );
}

sub _unpack_select {
    my ($self, $ns, $debug_prefix) = @_;
    local *@;
    $debug_prefix ||= "SELECT";
    confess __LINE__."$self->{name}: [$debug_prefix]: Bad response" if length $_[3] < 4;
    my $result_count = unpack('L', substr($_[3], 0, 4, ''));
    $self->_debug("$self->{name}: [$debug_prefix]: COUNT=[$result_count];") if $self->{debug} >= 3;
    my (@res);
    my $appe = $ns->{append_for_unpack};
    my $fmt  = $ns->{unpack_format};
    for(my $i = 0; $i < $result_count; ++$i) {
        confess __LINE__."$self->{name}: [$debug_prefix]: Bad response" if length $_[3] < 8;
        my ($len, $cardinality) = unpack('LL', substr($_[3], 0, 8, ''));
        $self->_debug("$self->{name}: [$debug_prefix]: ROW[$i]: LEN=[$len]; NFIELD=[$cardinality];") if $self->{debug} >= 4;
        confess __LINE__."$self->{name}: [$debug_prefix]: Bad response" if length $_[3] < $len;
        my $packed_tuple = substr($_[3], 0, $len, '');
        $self->_debug("$self->{name}: [$debug_prefix]: ROW[$i]: DATA=[@{[unpack '(H2)*', $packed_tuple]}];") if $self->{debug} >= 6;
        $packed_tuple .= $appe;
        my @tuple = eval { unpack($fmt, $packed_tuple) };
        confess "$self->{name}: [$debug_prefix]: ROW[$i]: can't unpack tuple [@{[unpack('(H2)*', $packed_tuple)]}]" if !@tuple || $@;
        $self->_debug("$self->{name}: [$debug_prefix]: ROW[$i]: FIELDS=[@{[map { qq{`$_'} } @tuple]}];") if $self->{debug} >= 5;
        push @res, \@tuple;
    }
    return \@res;
}

sub _unpack_select_multi {
    my ($self, $nss, $debug_prefix) = @_;
    $debug_prefix ||= "SMULTI";
    my (@rsets);
    my $i = 0;
    for my $ns (@$nss) {
        push @rsets, $self->_unpack_select($ns, "${debug_prefix}[$i]", $_[3]);
        ++$i;
    }
    return \@rsets;
}


sub _unpack_affected {
    my ($self, $flags, $ns) = @_;

    ($flags & WANT_RESULT) ? $self->_unpack_select($ns, "AFFECTED", $_[3])->[0] : unpack('L', substr($_[3],0,4,''));
}

sub NPRM () { 3 }
sub _pack_keys {
    my ($self, $ns, $idx) = @_;

    my $keys   = $idx->{keys};
    my $strkey = $ns->{string_keys};
    my $fmt    = $ns->{field_format};

    if (@$keys == 1) {
        $fmt    = $fmt->[$keys->[0]];
        $strkey = $strkey->{$keys->[0]};
        foreach (@_[NPRM..$#_]) {
            ($_) = @$_ if ref $_ eq 'ARRAY';
            unless ($strkey) {
                confess "$self->{name}: not numeric key [$_]" unless looks_like_number($_) && int($_) == $_;
                $_ = pack($fmt, $_);
            }
            $_ = pack('L(w/a*)*', 1, $_);
        }
    } else {
        foreach my $k (@_[NPRM..$#_]) {
            confess "bad key [@$keys][$k][@{[ref $k eq 'ARRAY' ? (@$k) : ()]}]" unless ref $k eq 'ARRAY' and @$k and @$k <= @$keys;
            for my $i (0..$#$k) {
                unless ($strkey->{$keys->[$i]}) {
                    confess "$self->{name}: not numeric key [$i][$k->[$i]]" unless looks_like_number($k->[$i]) && int($k->[$i]) == $k->[$i];
                }
                $k->[$i] = pack($fmt->[$keys->[$i]], $k->[$i]);
            }
            $k = pack('L(w/a*)*', scalar(@$k), @$k);
        }
    }
}

sub _PackSelect {
    my ($self, $param, $namespace, @keys) = @_;
    return '' unless @keys;
    $self->_pack_keys($namespace, $param->{index}, @keys);
    my $format = "";
    if ($param->{format}) {
        my $f = $namespace->{byfield_unpack_format};
        $param->{unpack_format} = join '', map { $f->[$_->{field}] } @{$param->{format}};
        $format = pack 'L*', scalar @{$param->{format}}, map {
            if($_->{full}) {
                $_->{offset} = 0;
                $_->{length} = 'max';
            }
            $_->{length} = 0xFFFFFFFF if $_->{length} eq 'max';
            @$_{qw/field offset length/}
        } @{$param->{format}};
    }
    return pack("LLLL a* La*", $namespace->{namespace}, $param->{index}->{id}, $param->{offset} || 0, $param->{limit} || scalar(@keys), $format, scalar(@keys), join('',@keys));
}

sub SelectUnion {
    confess "not supported yet";
    my ($param) = $_[0]->_validate_param(\@_, qw/raw raise/);
    my ($self, @reqs) = @_;
    return [] unless @reqs;
    local $self->{raise} = $param->{raise} if defined $param->{raise};
    confess "bad param" if grep { ref $_ ne 'ARRAY' } @reqs;
    $param->{raw} ||= $self->{default_raw};
    $param->{want} ||= 0;
    for my $req (@reqs) {
        my ($param, $namespace) = $self->_validate_param($req, qw/namespace use_index raw limit offset/);
        $req = {
            payload   => $self->_PackSelect($param, $namespace, $req),
            param     => $param,
            namespace => $namespace,
        };
    }
    my $r = $self->_chat(
        msg      => 18,
        payload  => pack("L (a*)*", scalar(@reqs), map { $_->{payload} } @reqs),
        unpack   => sub { $self->_unpack_select_multi([map { $_->{namespace} } @reqs], "SMULTI", @_) },
        retry    => $self->{select_retry},
        timeout  => $param->{timeout} || $self->{select_timeout},
    ) or return;
    confess __LINE__."$self->{name}: something wrong" if @$r != @reqs;
    my $ea = each_arrayref $r, \@reqs;
    while(my ($res, $req) = $ea->()) {
        $self->_PostSelect($res, { hashify => $req->{namespace}->{hashify}||$self->{hashify}, %$param, %{$req->{param}}, namespace => $req->{namespace} });
    }
    return $r;
}

sub _PostSelect {
    my ($self, $r, $param) = @_;
    if(!$param->{raw} && ref $param->{hashify} eq 'CODE') {
        $param->{hashify}->($param->{namespace}->{namespace}, $r);
    }
}

sub Select {
    confess q/Select isnt callable in void context/ unless defined wantarray;
    my ($param, $namespace) = $_[0]->_validate_param(\@_, qw/namespace use_index raw want next_rows limit offset raise hashify/);
    my ($self, @keys) = @_;
    local $self->{raise} = $param->{raise} if defined $param->{raise};
    @keys = @{$keys[0]} if ref $keys[0] eq 'ARRAY' and 1 == @{$param->{index}->{keys}} || ref $keys[0]->[0] eq 'ARRAY';

    $self->_debug("$self->{name}: SELECT($namespace->{namespace}/$param->{use_index})[@{[map{ref$_?qq{[@$_]}:$_}@keys]}]") if $self->{debug} >= 3;

    my ($msg,$payload);
    if(exists $param->{next_rows}) {
        confess "$self->{name}: One and only one key can be used to get N>0 rows after it" if @keys != 1 || !$param->{next_rows};
        $msg = 15;
        $self->_pack_keys($namespace, $param->{index}, @keys);
        $payload = pack("LL a*", $namespace->{namespace}, $param->{next_rows}, join('',@keys)),
    } else {
        $payload = $self->_PackSelect($param, $namespace, @keys);
        $msg = $param->{format} ? 21 : 17;
    }

    local $namespace->{unpack_format} = $param->{unpack_format} if $param->{unpack_format};

    my $r = [];
    if (@keys && $payload) {
        $r = $self->_chat(
            msg      => $msg,
            payload  => $payload,
            unpack   => sub { $self->_unpack_select($namespace, "SELECT", @_) },
            retry    => $self->{select_retry},
            timeout  => $param->{timeout} || $self->{select_timeout},
        ) or return;
    }

    $param->{raw} ||= $self->{default_raw};
    $param->{want} ||= !1;

    $self->_PostSelect($r, { hashify => $param->{hashify}||$namespace->{hashify}||$self->{hashify}, %$param, namespace => $namespace });

    return $r if $param->{want} eq 'arrayref';

    if (wantarray) {
        return @{$r};
    } else {
        confess "$self->{name}: too many keys in scalar context" if @keys > 1;
        return $r->[0];
    }
}

sub Delete {
    my ($param, $namespace) = $_[0]->_validate_param(\@_, qw/namespace/);
    my ($self, $key) = @_;

    $self->_debug("$self->{name}: DELETE($key)") if $self->{debug} >= 3;

    confess "$self->{name}\->Delete: for now key cardinality of 1 is only allowed" unless 1 == @{$param->{index}->{keys}};
    $self->_pack_keys($namespace, $param->{index}, $key);

    $self->_chat (
        msg => 20,
        payload => pack("L a*", $namespace->{namespace}, $key),
        unpack  => sub { $self->_unpack_affected(0, $namespace, @_) }
    );
}

sub OP_SET          () { 0 }
sub OP_ADD          () { 1 }
sub OP_AND          () { 2 }
sub OP_XOR          () { 3 }
sub OP_OR           () { 4 }
sub OP_SPLICE       () { 5 }

my %update_ops = (
    set         => OP_SET,
    add         => OP_ADD,
    and         => OP_AND,
    xor         => OP_XOR,
    or          => OP_OR,
    splice      => sub {
        confess "value for operation splice must be an ARRAYREF of <int[, int[, string]]>" if ref $_[0] ne 'ARRAY' || @{$_[0]} < 1;
        $_[0]->[0] = 0x7FFFFFFF unless defined $_[0]->[0];
        $_[0]->[0] = pack 'l', $_[0]->[0];
        $_[0]->[1] = defined $_[0]->[1] ? pack 'l', $_[0]->[1] : '';
        $_[0]->[2] = '' unless defined $_[0]->[2];
        return (OP_SPLICE, [ pack '(w/a)*', @{$_[0]} ]);
    },
    append      => sub { splice => [undef,  0,     $_[0]] },
    prepend     => sub { splice => [0,      0,     $_[0]] },
    cutbeg      => sub { splice => [0,      $_[0], ''   ] },
    cutend      => sub { splice => [-$_[0], $_[0], ''   ] },
    substr      => 'splice',
);

!ref $_ && m/^\D/ and $_ = $update_ops{$_} || die "bad link" for values %update_ops;

my %update_arg_fmt = (
    (map { $_ => 'l' } OP_ADD),
    (map { $_ => 'L' } OP_AND, OP_XOR, OP_OR),
);

my %ops_type = (
    (map { $_ => 'any'    } OP_SET),
    (map { $_ => 'number' } OP_ADD, OP_AND, OP_XOR, OP_OR),
    (map { $_ => 'string' } OP_SPLICE),
);

BEGIN {
    for my $op (qw/Append Prepend Cutbeg Cutend Substr/) {
        eval q/
            sub /.$op.q/ {
                my $param = ref $_[-1] eq 'HASH' ? pop : {};
                my ($self, $key, $field_num, $val) = @_;
                $self->UpdateMulti($key, [ $field_num => /.lc($op).q/ => $val ], $param);
            }
            1;
        / or die $@;
    }
}

sub UpdateMulti {
    my ($param, $namespace) = $_[0]->_validate_param(\@_, qw/namespace want_result/);
    my ($self, $key, @op) = @_;

    $self->_debug("$self->{name}: UPDATEMULTI($namespace->{namespace}=$key)[@{[map{qq{[@$_]}}@op]}]") if $self->{debug} >= 3;

    confess "$self->{name}\->UpdateMulti: for now key cardinality of 1 is only allowed" unless 1 == @{$param->{index}->{keys}};
    confess "$self->{name}: too many op" if scalar @op > 128;

    my $flags = $param->{_flags} || 0;
    $flags |= WANT_RESULT if $param->{want_result};

    my $fmt = $namespace->{field_format};

    foreach (@op) {
        confess "$self->{name}: bad op <$_>" if ref ne 'ARRAY' or @$_ != 3;
        my ($field_num, $op, $value) = @$_;
        my $field_type = $namespace->{string_keys}->{$field_num} ? 'string' : 'number';

        my $is_array = 0;
        if ($op eq 'bit_set') {
            $op = OP_OR;
        } elsif ($op eq 'bit_clear') {
            $op = OP_AND;
            $value = ~$value;
        } elsif ($op =~ /^num_(add|sub)$/) {
            $value = -$value if $1 eq 'sub';
            $op = OP_ADD;
        } else {
            confess "$self->{name}: bad op <$op>" unless exists $update_ops{$op};
            $op = $update_ops{$op};
        }

        while(ref $op eq 'CODE') {
            ($op, $value) = &$op($value);
            $op = $update_ops{$op} if exists $update_ops{$op};
        }

        confess "Are you sure you want to apply `$ops_type{$op}' operation to $field_type field?" if $ops_type{$op} ne $field_type && $ops_type{$op} ne 'any';

        $value = [ $value ] unless ref $value;
        confess "dunno what to do with ref `$value'" if ref $value ne 'ARRAY';

        confess "bad fieldnum: $field_num" if $field_num >= @$fmt;
        $value = pack($update_arg_fmt{$op} || $fmt->[$field_num], @$value);
        $_ = pack('LCw/a*', $field_num, $op, $value);
    }

    $self->_pack_keys($namespace, $param->{index}, $key);

    $self->_chat(
        msg      => 19,
        payload  => pack("LL a* L (a*)*" , $namespace->{namespace}, $flags, $key, scalar(@op), @op),
        unpack   => sub { $self->_unpack_affected($flags, $namespace, @_) }
    );
}

sub Update {
    my $param = ref $_[-1] eq 'HASH' ? pop : {};
    my ($self, $key, $field_num, $value) = @_;
    $self->UpdateMulti($key, [$field_num => set => $value ], $param);
}

sub AndXorAdd {
    my $param = ref $_[-1] eq 'HASH' ? pop : {};
    my ($self, $key, $field_num, $and, $xor, $add) = @_;
    my @upd;
    push @upd, [$field_num => and => $and] if defined $and;
    push @upd, [$field_num => xor => $xor] if defined $xor;
    push @upd, [$field_num => add => $add] if defined $add;
    $self->UpdateMulti($key, @upd, $param);
}

sub Bit {
    my $param = ref $_[-1] eq 'HASH' ? pop : {};
    my ($self, $key, $field_num, %arg) = @_;
    confess "$self->{name}: unknown op '@{[keys %arg]}'"  if grep { not /^(bit_clear|bit_set|set)$/ } keys(%arg);

    $arg{bit_clear} ||= 0;
    $arg{bit_set}   ||= 0;
    my @op;
    push @op, [$field_num => set       => $arg{set}]        if exists $arg{set};
    push @op, [$field_num => bit_clear => $arg{bit_clear}]  if $arg{bit_clear};
    push @op, [$field_num => bit_set   => $arg{bit_set}]    if $arg{bit_set};

    $self->UpdateMulti($key, @op, $param);
}

sub Num {
    my $param = ref $_[-1] eq 'HASH' ? pop : {};
    my ($self, $key, $field_num, %arg) = @_;
    confess "$self->{name}: unknown op '@{[keys %arg]}'"  if grep { not /^(num_add|num_sub|set)$/ } keys(%arg);

    $arg{num_add} ||= 0;
    $arg{num_sub} ||= 0;

    $arg{num_add} -= $arg{num_sub};
    my @op;
    push @op, [$field_num => set     => $arg{set}]     if exists $arg{set};
    push @op, [$field_num => num_add => $arg{num_add}]; # if $arg{num_add};
    $self->UpdateMulti($key, @op, $param);
}

1;
