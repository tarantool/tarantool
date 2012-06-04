package MR::Tarantool::Box;

=pod

=head1 NAME

MR::Tarantool::Box - A driver for an efficient Tarantool/Box NoSQL in-memory storage.

=head1 SYNOPSIS

    my $box = MR::Tarantool::Box->new({
        servers => "127.0.0.1:33013",
        name    => "My Box",              # mostly used for debug purposes
        spaces => [ {
            indexes => [ {
                index_name   => 'idx1',
                keys         => [0],
            }, {
                index_name   => 'idx2',
                keys         => [1,2],
            }, ],
            space         => 1,               # space id, as set in Tarantool/Box config
            name          => "primary",       # self-descriptive space-id
            format        => "QqLlSsCc&$",    # pack()-compatible, Qq must be supported by perl itself,
                                              # & stands for byte-string, $ stands for utf8 string.
            default_index => 'idx1',
            fields        => [qw/ id f2 field3 f4 f5 f6 f7 f8 misc_string /], # turn each tuple into hash, field names according to format
        }, {
            #...
        } ],
        default_space => "primary",

        timeout   => 1.0,                 # seconds
        retry     => 3,
        debug     => 9,                   # output to STDERR some debugging info
        raise     => 0,                   # dont raise an exception in case of error
    });

    my $bool  = $box->Insert(1, 2,3, 4,5,6,7,8,"asdf")                            or die $box->ErrorStr;
    my $bool  = $box->Insert(2, 2,4, 4,5,6,7,8,"asdf",{space => "primary"})       or die $box->ErrorStr;
    my $tuple = $box->Insert(3, 3,3, 4,5,6,7,8,"asdf",{want_inserted_tuple => 1}) or die $box->ErrorStr;

    # Select by single-field key
    my $tuple  = $box->Select(1);                                                 # scalar context - scalar result: $tuple
    my @tuples = $box->Select(1,2,3);                                             # list   context - list   result: ($tuple, $tuple, ...)
    my $tuples = $box->Select([1,2,3],{space => "primary", use_index => "idx1"}); #                arrayref result: [$tuple, $tuple, ...]

    # Select by multi-field key
    my $tuples = $box->Select([[2,3]],{use_index => "idx2"}); # by full key
    my $tuples = $box->Select([[2]]  ,{use_index => "idx2"}); # by partial key

    my $bool  = $box->UpdateMulti(1,[ f4 => add => 3 ]);
    my $bool  = $box->UpdateMulti(2,[ f4 => add => 3 ],{space => "primary"});
    my $tuple = $box->UpdateMulti(3,[ f4 => add => 3 ],{want_updated_tuple => 1});

    my $bool  = $box->Delete(1);
    my $tuple = $box->Delete(2, {want_deleted_tuple => 1});

=head1 DESCRIPTION

=head2 METHODS

=cut

use strict;
use warnings;
use Scalar::Util qw/looks_like_number/;
use List::MoreUtils qw/each_arrayref zip/;
use Time::HiRes qw/sleep/;
use Encode;

use MR::IProto ();

use constant {
    WANT_RESULT       => 1,
    INSERT_ADD        => 2,
    INSERT_REPLACE    => 4,
};


sub IPROTOCLASS () { 'MR::IProto' }

use vars qw/$VERSION %ERRORS/;
$VERSION = 0.0.22;

BEGIN { *confess = \&MR::IProto::confess }

%ERRORS = (
    0x00000000  => q{OK},
    0x00000100  => q{Non master connection, but it should be},
    0x00000200  => q{Illegal parametrs},
    0x00000300  => q{Uid not from this storage range},
    0x00000400  => q{Tuple is marked as read-only},
    0x00000500  => q{Tuple isn't locked},
    0x00000600  => q{Tuple is locked},
    0x00000700  => q{Failed to allocate memory},
    0x00000800  => q{Bad integrity},
    0x00000a00  => q{Unsupported command},

    0x00000b00  => q{Can't do select},

    0x00001800  => q{Can't register new user},
    0x00001a00  => q{Can't generate alert id},
    0x00001b00  => q{Can't del node},

    0x00001c00  => q{User isn't registered},
    0x00001d00  => q{Syntax error in query},
    0x00001e00  => q{Unknown field},
    0x00001f00  => q{Number value is out of range},
    0x00002000  => q{Insert already existing object},
    0x00002200  => q{Can not order result},
    0x00002300  => q{Multiple update/delete forbidden},
    0x00002400  => q{Nothing affected},
    0x00002500  => q{Primary key update forbidden},
    0x00002600  => q{Incorrect protocol version},
    0x00002700  => q{WAL failed},
    0x00003000  => q{Procedure return type is not supported in the binary protocol},
    0x00003100  => q{Tuple doesn't exist},
    0x00003200  => q{Procedure is not defined},
    0x00003300  => q{Lua error},
    0x00003400  => q{Space is disabled},
    0x00003500  => q{No such index in space},
    0x00003600  => q{Field was not found in the tuple},
    0x00003700  => q{Tuple already exists},
    0x00003800  => q{Duplicate key exists in a unique index},
    0x00003900  => q{Space does not exists},
);



=pod

=head3 new

    my $box = $class->new(\%args);

%args:

=over

=item B<spaces> => [ \%space, ... ]

%space:

=over

=item B<space> => $space_id_uint32

Space id as set in Tarantool/Box config.

=item B<name> => $space_name_string

Self-descriptive space id, which will be mapped into C<space>.

=item B<format> => $format_string

C<pack()>-compatible tuple format string, allowed formats: C<QqLlSsC(c&$)*>,
where C<&> stands for bytestring, C<$> stands for L</utf8> string. C<Qq> usable only if perl supports
int64 itself. Tuples' fields are packed/unpacked according to this C<format>.
C<< * >> at the end of C<format> enables L</LongTuple>.

=item B<hashify> => B<$coderef>

Specify a callback to turn each tuple into a good-looking hash.
It receives C<space> id and resultset as arguments. No return value needed.

    $coderef = sub {
        my ($space_id, $resultset) = @_;
        $_ = { FieldName1 => $_->[0], FieldName2 => $_->[1], ... } for @$resultset;
    };

=item B<fields> => B<$arrayref>

Specify an arrayref of fields names according to C<format> to turn each
tuple into a good-looking hash. Names must begin with C<< [A-Za-z] >>.
If L</LongTuple> enabled, last field will be used to fold tailing fields.

=item B<long_fields> => B<$arrayref>

Specify an arrayref of fields names according to C<< (xxx)* >> to turn
tailing fields into a good-looking array of hashes.
Names must begin with C<< [A-Za-z] >>.
Works with L</LongTuple> enabled only.


=item B<indexes> => [ \%index, ... ]

%index:

=over

=item B<id> => $index_id_uint32

Index id as set in Tarantool/Box config within current C<space>.
If not set, order position in C<indexes> is theated as C<id>.

=item B<name> => $index_name_string

Self-descriptive index id, which will be mapped into C<index_id>.

=item B<keys> => [ $field_no_uint32, ... ]

Properly ordered arrayref of fields' numbers which are indexed.

=back

=item B<default_index> => $default_index_name_string_or_id_uint32

Index C<id> or C<name> to be used by default for the current C<space> in B<select> operations.
Must be set if there are more than one C<\%index>es.

=item B<primary_key_index> => $primary_key_name_string_or_id_uint32

Index C<id> or C<name> to be used by default for the current C<space> in B<update> operations.
It is set to C<default_index> by default.

=back

=item B<default_space> => $default_space_name_string_or_id_uint32

Space C<space> or C<name> to be used by default. Must be set if there are
more than one C<\%space>s.

=item B<timeout> => $timeout_fractional_seconds_float || 23

A common timeout for network operations.

=item B<select_timeout> => $select_timeout_fractional_seconds_float || 2

Select queries timeout for network operations. See L</select_retry>.

=item B<retry> => $retry_int || 1

A common retries number for network operations.

=item B<select_retry> => $select_retry_int || 3

Select queries retries number for network operations.

Sometimes we need short timeout for select's and long timeout for B<critical> update's,
because in case of timeout we B<don't know if the update has succeeded>. For the same
reason we B<can't retry> update operation.

So increasing C<timeout> and setting C<< retry => 1 >> for updates lowers possibility of
such situations (but, of course, does not exclude them at all), and guarantees that
we dont do the same more then once.

=item B<soft_retry> => $soft_retry_int || 3

A common retries number for Tarantool/Box B<temporary errors> (these marked by 1 in the
lowest byte of C<error_code>). In that case we B<know for sure> that the B<request was
declined> by Tarantool/Box for some reason (a tuple was locked for another update, for
example), and we B<can> try it again.

This is also limited by C<retry>/C<select_retry>
(depending on query type).

=item B<retry_delay> => $retry_delay_fractional_seconds_float || 1

Specify a delay between retries for network operations.

=item B<raise> => $raise_bool || 1

Should we raise an exceptions? If so, exceptions are raised when no more retries left and
all tries failed (with timeout, fatal, or temporary error).

=item B<debug> => $debug_level_int || 0

Debug level, 0 - print nothing, 9 - print everything

=item B<name> => $name

A string used for self-description. Mainly used for debugging purposes.

=back

=cut

sub _make_unpack_format {
    my ($ns,$prefix) = @_;
    $ns->{format} =~ s/\s+//g;
    confess "${prefix} bad format `$ns->{format}'" unless $ns->{format} =~ m/^[\$\&lLsScCqQ]*(?:\([\$\&lLsScCqQ]+\)\*|\*)?$/;
    $ns->{long_tuple} = 1 if $ns->{format} =~ s/\*$//;
    $ns->{long_format} = '';
    my @f_long;
    if ($ns->{long_tuple}) {
        $ns->{format} =~ s/(  \( [^\)]* \)  | . )$//x;
        $ns->{long_format} = $1;
        $ns->{long_format} =~ s/[()]*//g;
        @f_long = split //, $ns->{long_format};
        $ns->{long_byfield_unpack_format} = [ map { m/[\&\$]/ ? 'w/a*' : "x$_" } @f_long ];
        $ns->{long_field_format}          = [ map { m/[\&\$]/ ? 'a*'   : $_    } @f_long ];
        $ns->{long_utf8_fields} = [ grep { $f_long[$_] eq '$' } 0..$#f_long ];
    }
    my @f = split //, $ns->{format};
    $ns->{byfield_unpack_format} = [ map { m/[\&\$]/ ? 'w/a*' : "x$_" } @f ];
    $ns->{field_format}          = [ map { m/[\&\$]/ ? 'a*'   : $_    } @f ];
    $ns->{unpack_format}  = join('', @{$ns->{byfield_unpack_format}});
    $ns->{unpack_format} .= '('.join('', @{$ns->{long_byfield_unpack_format}}).')*' if $ns->{long_tuple};
    $ns->{string_keys} = { map { $_ =>  1 } grep { $f[$_] =~ m/[\&\$]/ } 0..$#f };
    $ns->{utf8_fields} = { map { $_ => $_ } grep { $f[$_] eq '$' } 0..$#f };
}

sub new {
    my ($class, $arg) = @_;
    my $self;

    $arg = { %$arg };
    $self->{name}            = $arg->{name}      || ref$class || $class;
    $self->{timeout}         = $arg->{timeout}   || 23;
    $self->{retry}           = $arg->{retry}     || 1;
    $self->{retry_delay}     = $arg->{retry_delay} || 1;
    $self->{select_retry}    = $arg->{select_retry} || 3;
    $self->{softretry}       = $arg->{soft_retry} || $arg->{softretry} || 3;
    $self->{debug}           = $arg->{'debug'}   || 0;
    $self->{ipdebug}         = $arg->{'ipdebug'} || 0;
    $self->{raise}           = 1;
    $self->{raise}           = $arg->{raise} if exists $arg->{raise};
    $self->{select_timeout}  = $arg->{select_timeout} || $self->{timeout};
    $self->{iprotoclass}     = $arg->{iprotoclass} || $class->IPROTOCLASS;
    $self->{_last_error}     = 0;
    $self->{_last_error_msg} = '';

    $self->{hashify}         = $arg->{'hashify'} if exists $arg->{'hashify'};
    $self->{default_raw}     = $arg->{default_raw};
    $self->{default_raw}     = 1 if !defined$self->{default_raw} and defined $self->{hashify} and !$self->{hashify};

    $arg->{spaces} = $arg->{namespaces} = [@{ $arg->{spaces} ||= $arg->{namespaces} || confess "no spaces given" }];
    confess "no spaces given" unless @{$arg->{spaces}};
    my %namespaces;
    for my $ns (@{$arg->{spaces}}) {
        $ns = { %$ns };
        my $namespace = defined $ns->{space} ? $ns->{space} : $ns->{namespace};
        $ns->{space} = $ns->{namespace} = $namespace;
        confess "space[?] `space' not set" unless defined $namespace;
        confess "space[$namespace] already defined" if $namespaces{$namespace} or $ns->{name}&&$namespaces{$ns->{name}};
        confess "space[$namespace] no indexes defined" unless $ns->{indexes} && @{$ns->{indexes}};
        $namespaces{$namespace} = $ns;
        $namespaces{$ns->{name}} = $ns if $ns->{name};

        _make_unpack_format($ns,"space[$namespace]");

        $ns->{append_for_unpack} = '' unless defined $ns->{append_for_unpack};
        $ns->{check_keys} = {};

        my $inames = $ns->{index_names} = {};
        my $i = -1;
        for my $index (@{$ns->{indexes}}) {
            ++$i;
            confess "space[$namespace]index[($i)] no name given" unless length $index->{index_name};
            my $index_name = $index->{index_name};
            confess "space[$namespace]index[$index_name($i)] no indexes defined" unless $index->{keys} && @{$index->{keys}};
            confess "space[$namespace]index[$index_name($i)] already defined" if $inames->{$index_name} || $inames->{$i};
            $index->{id} = $i unless defined $index->{id};
            $inames->{$i} = $inames->{$index_name} = $index;
            int $_ == $_ and $_ >= 0 and $_ < @{$ns->{field_format}} or confess "space[$namespace]index[$index_name] bad key `$_'" for @{$ns->{keys}};
            $ns->{check_keys}->{$_} = int !! $ns->{string_keys}->{$_} for @{$index->{keys}};
            $index->{string_keys} ||= $ns->{string_keys};
        }
        if( @{$ns->{indexes}} > 1 ) {
            confess "space[$namespace] default_index not given" unless defined $ns->{default_index};
            confess "space[$namespace] default_index $ns->{default_index} does not exist" unless $inames->{$ns->{default_index}};
            $ns->{primary_key_index} = $ns->{default_index} unless defined $ns->{primary_key_index};
            confess "space[$namespace] primary_key_index $ns->{primary_key_index} does not exist" unless $inames->{$ns->{primary_key_index}};
        } else {
            $ns->{default_index} ||= 0;
            $ns->{primary_key_index} ||= 0;
        }
        $ns->{fields}      ||= $arg->{default_fields};
        $ns->{long_fields} ||= $arg->{default_long_fields};
        if($ns->{fields}) {
            confess "space[$namespace] fields must be ARRAYREF" unless ref $ns->{fields} eq 'ARRAY';
            confess "space[$namespace] fields number must match format" if @{$ns->{fields}} != int(!!$ns->{long_tuple})+@{$ns->{field_format}};
            m/^[A-Za-z]/ or confess "space[$namespace] fields names must begin with [A-Za-z]: bad name $_" for @{$ns->{fields}};
            $ns->{fields_hash} = { map { $ns->{fields}->[$_] => $_ } 0..$#{$ns->{fields}} };
        }
        if($ns->{long_fields}) {
            confess "space[$namespace] long_fields must be ARRAYREF" unless ref $ns->{long_fields} eq 'ARRAY';
            confess "space[$namespace] long_fields number must match format" if @{$ns->{long_fields}} != @{$ns->{long_field_format}};
            m/^[A-Za-z]/ or confess "space[$namespace] long_fields names must begin with [A-Za-z]: bad name $_" for @{$ns->{long_fields}};
        }
        $ns->{default_raw} = 1 if !defined$ns->{default_raw} and defined $ns->{hashify} and !$ns->{hashify};
    }
    $self->{namespaces} = \%namespaces;
    if (@{$arg->{spaces}} > 1) {
        $arg->{default_namespace} = $arg->{default_space} if defined $arg->{default_space};
        confess "default_space not given" unless defined $arg->{default_namespace};
        confess "default_space $arg->{default_namespace} does not exist" unless $namespaces{$arg->{default_namespace}};
        $self->{default_namespace} = $arg->{default_namespace};
    } else {
        $self->{default_namespace} = $arg->{default_space} || $arg->{default_namespace} || $arg->{spaces}->[0]->{space};
        confess "default_space $self->{default_namespace} does not exist" unless $namespaces{$self->{default_namespace}};
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
        max_request_retries => 1,
        retry_delay   => $self->{retry_delay},
    });
}

=pod

=head3 Error

Last error code, or 'fail' for some network reason, oftenly a timeout.

    $box->Insert(@tuple) or die sprintf "Error %X", $box->Error; # die "Error 202"

=head3 ErrorStr

Last error code and description in a single string.

    $box->Insert(@tuple) or die $box->ErrorStr;                  # die "Error 00000202: Illegal Parameters"

=cut

sub ErrorStr {
    return $_[0]->{_last_error_msg};
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
        my ($full_code, @err_code) = unpack('LX[L]CSC', substr($data, 0, 4, ''));
        # $err_code[0] = severity: 0 -> ok, 1 -> transient, 2 -> permanent;
        # $err_code[1] = description;
        # $err_code[2] = da box project;
        return (\@err_code, \$data, $full_code);
    };

    my $timeout = $param{timeout} || $self->{timeout};
    my $retry = $param{retry} || $self->{retry};
    my $soft_retry = $self->{softretry};
    my $retry_count = 0;

    my $callback  = delete $param{callback};
    my $return_fh = delete $param{return_fh};
    my $_cb = $callback || $return_fh;

    die "Can't use raise and callback together" if $callback && $self->{raise};

    my $is_retry = sub {
        my ($data) = @_;
        $retry_count++;
        if($data) {
            my ($ret_code, $data, $full_code) = @$data;
            return 0 if $ret_code->[0] == 0;
            # retry if error is soft even in case of update e.g. ROW_LOCK
            if ($ret_code->[0] == 1 and --$soft_retry > 0) {
                --$retry if $retry > 1;
                return 1;
            }
        }
        return 1 if --$retry;
        return 0;
    };

    my $message;
    my $process = sub {
        my ($data, $error) = @_;
        my $errno = $!;
        if (!$error && $data) {
            my ($ret_code, $data, $full_code) = @$data;

            $self->{_last_error} = $full_code;
            $self->{_last_error_msg} = $message = $ret_code->[0] == 0 ? "" : sprintf "Error %08X: %s", $full_code, $$data || $ERRORS{$full_code & 0xFFFFFF00} || 'Unknown error';
            $self->_debug("$self->{name}: $message") if $ret_code->[0] != 0 && $self->{debug} >= 1;

            if ($ret_code->[0] == 0) {
                my $ret = $orig_unpack->($$data,$ret_code->[2]);
                confess __LINE__."$self->{name}: [common]: Bad response (more data left)" if length $$data > 0;
                return $ret unless $_cb;
                return &$_cb($ret);
            }

            if ($ret_code->[0] == 2) { #fatal error
                $self->_raise($message) if $self->{raise};
                return 0 unless $_cb;
                return &$_cb(0, $error);
            }
        } else { # timeout has caused the failure if $ret->{timeout}
            $self->{_last_error} = 'fail';
            $message ||= $self->{_last_error_msg} = $error;
            $self->_debug("$self->{name}: $message") if $self->{debug} >= 1;
            $self->_raise("$self->{name}: no success after $retry_count tries: $message\n") if $self->{raise};
            return 0 unless $_cb;
            return &$_cb(0, $error);
        }
    };

    if ($callback) {
        $self->{_last_error} = 0x77777777;
        $self->{server}->SetTimeout($timeout);
        return 1 if eval { $self->{server}->send({%param, is_retry => $is_retry, max_request_retries => $retry}, $process); 1 };
        return 0;
    }

    $param{continue} = $process if $return_fh;

    my $ret;
    while ($retry > 0) {
        $self->{_last_error} = 0x77777777;
        $self->{server}->SetTimeout($timeout);

        $ret = $self->{server}->Chat1(%param);
        return $ret->{ok} if $param{continue} && $ret->{ok};
        last unless &$is_retry($ret->{ok});
        sleep $self->{retry_delay};
    };

    $self->_raise("no success after $retry_count tries\n") if $self->{raise} && !$ret->{ok};
    return &$process($ret->{ok}, $ret->{fail});
}

sub _raise {
    my ($self, $msg) = @_;
    die "$self->{name}: $msg\n";
}

sub _validate_param {
    my ($self, $args, @pnames) = @_;
    my $param = $args && @$args && ref $args->[-1] eq 'HASH' ? {%{pop @$args}} : {};

    my %pnames = map { $_ => 1 } @pnames;
    $pnames{space} = 1;
    $pnames{namespace} = 1;
    $pnames{callback} = 1;
    foreach my $pname (keys %$param) {
        confess "$self->{name}: unknown param $pname\n" unless exists $pnames{$pname};
    }

    $param->{namespace} = $param->{space} if exists $param->{space} and defined $param->{space};
    $param->{namespace} = $self->{default_namespace} unless defined $param->{namespace};
    confess "$self->{name}: bad space `$param->{namespace}'" unless exists $self->{namespaces}->{$param->{namespace}};

    my $ns = $self->{namespaces}->{$param->{namespace}};
    $param->{use_index} = $pnames{use_index} ? $ns->{default_index} : $ns->{primary_key_index} unless defined $param->{use_index};
    confess "$self->{name}: bad index `$param->{use_index}'" unless exists $ns->{index_names}->{$param->{use_index}};
    $param->{index} = $ns->{index_names}->{$param->{use_index}};

    if(exists $pnames{raw}) {
        $param->{raw} = $ns->{default_raw}   unless defined $param->{raw};
        $param->{raw} = $self->{default_raw} unless defined $param->{raw};
    }

    return ($param, $ns, map { $param->{$_} } @pnames);
}

=pod

=head3 Call

Call a stored procedure. Returns an arrayref of the result tuple(s) upon success.

    my $results = $box->Call('stored_procedure_name', \@procedure_params, \%options) or die $box->ErrorStr; # Call failed
    my $result_tuple = @$results && $results->[0] or warn "Call succeeded, but returned nothing";

=over

=item B<@procedure_params>

An array of bytestrings to be passed as is to the procecedure.

=item B<%options>

=over

=item B<unpack_format>

Format to unpack the result tuple, the same as C<format> option for C<new()>

=back

=back

=cut

sub Call {
    my ($param, $namespace) = $_[0]->_validate_param(\@_, qw/flags raise unpack unpack_format/);
    my ($self, $sp_name, $tuple) = @_;

    my $flags = $param->{flags} || 0;
    local $self->{raise} = $param->{raise} if defined $param->{raise};

    $self->_debug("$self->{name}: CALL($sp_name)[${\join '   ', map {join' ',unpack'(H2)*',$_} @$tuple}]") if $self->{debug} >= 4;
    confess "All fields must be defined" if grep { !defined } @$tuple;

    confess "Required `unpack_format` option wasn't defined"
        unless exists $param->{unpack} or exists $param->{unpack_format} and $param->{unpack_format};

    my $unpack_format = $param->{unpack_format};
    if($unpack_format) {
        $unpack_format = join '', @$unpack_format if ref $unpack_format;
        my $f = { format => $unpack_format };
        _make_unpack_format($f, "CALL");
        $unpack_format = $f->{unpack_format};
    }

    local $namespace->{unpack_format} = $unpack_format if $unpack_format; # XXX
    local $namespace->{append_for_unpack} = ''         if $unpack_format; # shit...

    $tuple = [ map {
        my $x = $_;
        Encode::_utf8_off($x) if Encode::is_utf8($x,0);
        $x;
    } @$tuple ];

    $self->_chat (
        msg      => 22,
        payload  => pack("L w/a* L(w/a*)*", $flags, $sp_name, scalar(@$tuple), @$tuple),
        unpack   => $param->{unpack} || sub { $self->_unpack_select($namespace, "CALL", @_) },
        callback => $param->{callback},
    );
}

=pod

=head3 Add, Insert, Replace

    $box->Add(@tuple) or die $box->ErrorStr;         # only store a new tuple
    $box->Replace(@tuple, { space => "secondary" }); # only store an existing tuple
    $box->Insert(@tuple, { space => "main" });       # store anyway

Insert a C<< @tuple >> into the storage into C<$options{space}> or C<default_space> space.
All of them return C<true> upon success.

All of them have the same parameters:

=over

=item B<@tuple>

A tuple to insert. All fields must be defined. All fields will be C<pack()>ed according to C<format> (see L</new>)

=item B<%options>

=over

=item B<space> => $space_id_uint32_or_name_string

Specify storage space to work on.

=back

=back

The difference between them is the behaviour concerning tuple with the same primary key:

=over

=item *

B<Add> will succeed if and only if duplicate-key tuple B<does not exist>

=item *

B<Replace> will succeed if and only if a duplicate-key tuple B<exists>

=item *

B<Insert> will succeed B<anyway>. Duplicate-key tuple will be B<overwritten>

=back

=cut

sub Add { # store tuple if tuple identified by primary key _does_not_ exist
    my $param = @_ && ref $_[-1] eq 'HASH' ? pop : {};
    $param->{action} = 'add';
    $_[0]->Insert(@_[1..$#_], $param);
}

sub Set { # store tuple _anyway_
    my $param = @_ && ref $_[-1] eq 'HASH' ? pop : {};
    $param->{action} = 'set';
    $_[0]->Insert(@_[1..$#_], $param);
}

sub Replace { # store tuple if tuple identified by primary key _does_ exist
    my $param = @_ && ref $_[-1] eq 'HASH' ? pop : {};
    $param->{action} = 'replace';
    $_[0]->Insert(@_[1..$#_], $param);
}

sub Insert {
    my ($param, $namespace) = $_[0]->_validate_param(\@_, qw/want_result want_inserted_tuple _flags action raw/);
    my ($self, @tuple) = @_;

    $self->_debug("$self->{name}: INSERT(NS:$namespace->{namespace},TUPLE:[@{[map {qq{`$_'}} @tuple]}])") if $self->{debug} >= 3;

    $param->{want_result} = $param->{want_inserted_tuple} if !defined $param->{want_result};

    my $flags = $param->{_flags} || 0;
    $flags |= WANT_RESULT if $param->{want_result};

    $param->{action} ||= 'set';
    if ($param->{action} eq 'add') {
        $flags |= INSERT_ADD;
    } elsif ($param->{action} eq 'replace') {
        $flags |= INSERT_REPLACE;
    } elsif ($param->{action} ne 'set') {
        confess "$self->{name}: Bad insert action `$param->{action}'";
    }
    my $chkkey = $namespace->{check_keys};
    my $fmt = $namespace->{field_format};
    my $long_fmt = $namespace->{long_field_format};
    my $chk_divisor = $namespace->{long_tuple} ? @$long_fmt : @$fmt;
    confess "Wrong fields number in tuple" if 0 != (@tuple - @$fmt) % $chk_divisor;
    for (0..$#tuple) {
        confess "$self->{name}: ref in tuple $_=`$tuple[$_]'" if ref $tuple[$_];
        no warnings 'uninitialized';
        Encode::_utf8_off($_) if Encode::is_utf8($_,0);
        if(exists $chkkey->{$_}) {
            if($chkkey->{$_}) {
                confess "$self->{name}: undefined key $_" unless defined $tuple[$_];
            } else {
                confess "$self->{name}: not numeric key $_=`$tuple[$_]'" unless looks_like_number($tuple[$_]) && int($tuple[$_]) == $tuple[$_];
            }
        }
        $tuple[$_] = pack($_ < @$fmt ? $fmt->[$_] : $long_fmt->[$_ % @$long_fmt], $tuple[$_]);
    }

    $self->_debug("$self->{name}: INSERT[${\join '   ', map {join' ',unpack'(H2)*',$_} @tuple}]") if $self->{debug} >= 4;

    my $cb = sub {
        my ($r) = @_;

        if($param->{want_result}) {
            $self->_PostSelect($r, $param, $namespace);
            $r = $r && $r->[0];
        }

        return $param->{callback}->($r) if $param->{callback};
        return $r;
    };

    my $r = $self->_chat (
        msg      => 13,
        payload  => pack("LLL (w/a*)*", $namespace->{namespace}, $flags, scalar(@tuple), @tuple),
        unpack   => sub { $self->_unpack_affected($flags, $namespace, @_) },
        callback => $param->{callback} && $cb,
    ) or return;

    return 1 if $param->{callback};
    return $cb->($r);
}

sub _unpack_select {
    my ($self, $ns, $debug_prefix) = @_;
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
        confess "$self->{name}: [$debug_prefix]: ROW[$i]: can't unpack tuple [@{[unpack('(H2)*', $packed_tuple)]}]: $@" if !@tuple || $@;
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
    ($flags & WANT_RESULT) ? $self->_unpack_select($ns, "AFFECTED", $_[3]) : unpack('L', substr($_[3],0,4,''))||'0E0';
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
            Encode::_utf8_off($_) if Encode::is_utf8($_,0);
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
                Encode::_utf8_off($k->[$i]) if Encode::is_utf8($k->[$i],0);
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
    if ($param->{format}) { #broken
        confess "broken" if $namespace->{long_tuple};
        my $f = $namespace->{byfield_unpack_format};
        $param->{unpack_format} = join '', map { $f->[$_->{field}] } @{$param->{format}};
        $format = pack 'l*', scalar @{$param->{format}}, map {
            $_ = { %$_ };
            if($_->{full}) {
                $_->{offset} = 0;
                $_->{length} = 'max';
            }
            $_->{length} = 0x7FFFFFFF if $_->{length} eq 'max';
            @$_{qw/field offset length/}
        } @{$param->{format}};
    }
    return pack("LLLL a* La*", $namespace->{namespace}, $param->{index}->{id}, $param->{offset} || 0, $param->{limit} || ($param->{default_limit_by_keys} ? scalar(@keys) : 0x7FFFFFFF), $format, scalar(@keys), join('',@keys));
}

sub _PostSelect {
    my ($self, $r, $param, $namespace) = @_;
    if(!$param->{raw}) {
        my @utf8_fields = values %{$namespace->{utf8_fields}};
        my $long_utf8_fields = $namespace->{long_utf8_fields};
        if(@utf8_fields or $long_utf8_fields && @$long_utf8_fields) {
            my $long_tuple = $namespace->{long_tuple};
            for my $row (@$r) {
                Encode::_utf8_on($row->[$_]) for @utf8_fields;
                if ($long_tuple && @$long_utf8_fields) {
                    my $i = @{$namespace->{field_format}};
                    my $n = int( (@$row-$i-1) / @$long_utf8_fields );
                    Encode::_utf8_on($row->[$_]) for map do{ $a=$_; map $a+$i+@$long_utf8_fields*$_, 0..$n }, @$long_utf8_fields;
                }
            }
        }

        my $hashify = $param->{hashify} || $namespace->{hashify} || $self->{hashify};
        if ($hashify) {
            $hashify->($namespace->{namespace}, $r);
        } elsif( $namespace->{fields} ) {
            my @f = @{$namespace->{fields}};
            my @f_long;
            my $last;
            if ($namespace->{long_tuple}) {
                $last = pop @f;
                @f_long = @{$namespace->{long_fields}} if $namespace->{long_fields};
            }
            for my $row (@$r) {
                my $h = { zip @{$namespace->{fields}}, @{[splice(@$row,0,0+@f)]} };
                if($last) {
                    $row = [ map +{ zip @f_long, @{[splice(@$row,0,0+@f_long)]} }, 0..((@$row-1)/@f_long) ] if @f_long;
                    $h->{$last} = $row;
                }
                $row = $h;
            }
        }
    }
}

=pod

=head3 Select

Select tuple(s) from storage

    my $key = $id;
    my $key = [ $firstname, $lastname ];
    my @keys = ($key, ...);

    my $tuple  = $box->Select($key)              or $box->Error && die $box->ErrorStr;
    my $tuple  = $box->Select($key, \%options)   or $box->Error && die $box->ErrorStr;

    my @tuples = $box->Select(@keys)             or $box->Error && die $box->ErrorStr;
    my @tuples = $box->Select(@keys, \%options)  or $box->Error && die $box->ErrorStr;

    my $tuples = $box->Select(\@keys)            or die $box->ErrorStr;
    my $tuples = $box->Select(\@keys, \%options) or die $box->ErrorStr;

=over

=item B<$key>, B<@keys>, B<\@keys>

Specify keys to select. All keys must be defined.

Contextual behaviour:

=over

=item *

In scalar context, you can select one C<$key>, and the resulting tuple will be returned.
Check C<< $box->Error >> to see if there was an error or there is just no such key
in the storage

=item *

In list context, you can select several C<@keys>, and the resulting tuples will be returned.
Check C<< $box->Error >> to see if there was an error or there is just no such keys
in the storage

=item *

If you select C<< \@keys >> then C<< \@tuples >> will be returned upon success. C<< @tuples >> will
be empty if there are no such keys, and false will be returned in case of error.

=back

Other notes:

=over

=item *

If you select using index on multiple fields each C<< $key >> should be given as a key-tuple C<< $key = [ $key_field1, $key_field2, ... ] >>.

=back

=item B<%options>

=over

=item B<space> => $space_id_uint32_or_name_string

Specify storage (by id or name) space to select from.

=item B<use_index> => $index_id_uint32_or_name_string

Specify index (by id or name) to use.

=item B<limit> => $limit_uint32

Max tuples to select. It is set to C<< MAX_INT32 >> by default.

=item B<raw> => $bool

Don't C<hashify> (see L</new>), disable L</utf8> processing.

=item B<hash_by> => $by

Return a hashref of the resultset. If you C<hashify> the result set,
then C<$by> must be a field name of the hash you return,
otherwise it must be a number of field of the tuple.
C<False> will be returned in case of error.

=back

=back

=cut

my @select_param_ok = qw/use_index raw want next_rows limit offset raise hashify timeout format hash_by callback return_fh default_limit_by_keys/;
sub Select {
    confess q/Select isnt callable in void context/ unless defined wantarray;
    my ($param, $namespace) = $_[0]->_validate_param(\@_, @select_param_ok);
    my ($self, @keys) = @_;
    local $self->{raise} = $param->{raise} if defined $param->{raise};
    @keys = @{$keys[0]} if @keys && ref $keys[0] eq 'ARRAY' and 1 == @{$param->{index}->{keys}} || (@keys && ref $keys[0]->[0] eq 'ARRAY');

    $self->_debug("$self->{name}: SELECT(NS:$namespace->{namespace},IDX:$param->{use_index})[@{[map{ref$_?qq{[@$_]}:$_}@keys]}]") if $self->{debug} >= 3;

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

    $param->{want} ||= !1;
    my $wantarray = wantarray;

    my $cb = sub {
        my ($r) = (@_);

        $self->_PostSelect($r, $param, $namespace) if $r;

        if ($r && defined(my $p = $param->{hash_by})) {
            my %h;
            if (@$r) {
                if (ref $r->[0] eq 'HASH') {
                    confess "Bad hash_by `$p' for HASH" unless exists $r->[0]->{$p};
                    $h{$_->{$p}} = $_ for @$r;
                } elsif (ref $r->[0] eq 'ARRAY') {
                    confess "Bad hash_by `$p' for ARRAY" unless $p =~ m/^\d+$/ && $p >= 0 && $p < @{$r->[0]};
                    $h{$_->[$p]} = $_ for @$r;
                } else {
                    confess "i dont know how to hash_by ".ref($r->[0]);
                }
            }
            $r = \%h;
        }

        if ($param->{callback}) {
            return $param->{callback}->($r);
        }

        if ($param->{return_fh} && ref $param->{return_fh} eq 'CODE') {
            return $param->{return_fh}->($r);
        }

        return unless $r;

        return $r if defined $param->{hash_by};
        return $r if $param->{want} eq 'arrayref';
        $wantarray = wantarray if $param->{return_fh};

        if ($wantarray) {
            return @{$r};
        } else {
            confess "$self->{name}: too many keys in scalar context" if @keys > 1;
            return $r->[0];
        }
    };

    if (@keys && $payload) {
        $r = $self->_chat(
            msg      => $msg,
            payload  => $payload,
            unpack   => sub { $self->_unpack_select($namespace, "SELECT", @_) },
            retry    => $param->{return_fh} ? 1 : $self->{select_retry},
            timeout  => $param->{timeout} || $self->{select_timeout},
            callback => $param->{callback} ? $cb : 0,
            return_fh=> $param->{return_fh} ? $cb : 0,
        ) or return;
        return $r if $param->{return_fh};
        return 1 if $param->{callback};
    } else {
        $r = [];
    }

    return $cb->($r);
}

sub SelectUnion {
    confess "not supported yet";
    my ($param) = $_[0]->_validate_param(\@_, qw/raw raise/);
    my ($self, @reqs) = @_;
    return [] unless @reqs;
    local $self->{raise} = $param->{raise} if defined $param->{raise};
    confess "bad param" if grep { ref $_ ne 'ARRAY' } @reqs;
    $param->{want} ||= 0;
    for my $req (@reqs) {
        my ($param, $namespace) = $self->_validate_param($req, @select_param_ok);
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
        timeout  => $param->{select_timeout} || $self->{timeout},
        callback => $param->{callback},
    ) or return;
    confess __LINE__."$self->{name}: something wrong" if @$r != @reqs;
    my $ea = each_arrayref $r, \@reqs;
    while(my ($res, $req) = $ea->()) {
        $self->_PostSelect($res, { %$param, %{$req->{param}} }, $req->{namespace});
    }
    return $r;
}

=pod

=head3 Delete

Delete tuple from storage. Return false upon error.

    my $n_deleted = $box->Delete($key) or die $box->ErrorStr;
    my $n_deleted = $box->Delete($key, \%options) or die $box->ErrorStr;
    warn "Nothing was deleted" unless int $n_deleted;

    my $deleted_tuple_set = $box->Delete($key, { want_deleted_tuples => 1 }) or die $box->ErrorStr;
    warn "Nothing was deleted" unless @$deleted_tuple_set;

=over

=item B<%options>

=over

=item B<space> => $space_id_uint32_or_name_string

Specify storage space (by id or name) to work on.

=item B<want_deleted_tuple> => $bool

if C<$bool> then return deleted tuple.

=back

=back

=cut

sub Delete {
    my ($param, $namespace) = $_[0]->_validate_param(\@_, qw/want_deleted_tuple want_result raw/);
    my ($self, $key) = @_;

    $param->{want_result} = $param->{want_deleted_tuple} if !defined $param->{want_result};

    my $flags = 0;
    $flags |= WANT_RESULT if $param->{want_result};

    $self->_debug("$self->{name}: DELETE(NS:$namespace->{namespace},KEY:$key,F:$flags)") if $self->{debug} >= 3;

    confess "$self->{name}\->Delete: for now key cardinality of 1 is only allowed" unless 1 == @{$param->{index}->{keys}};
    $self->_pack_keys($namespace, $param->{index}, $key);

    my $cb = sub {
        my ($r) = @_;

        if($param->{want_result}) {
            $self->_PostSelect($r, $param, $namespace);
            $r = $r && $r->[0];
        }

        return $param->{callback}->($r) if $param->{callback};
        return $r;
    };

    my $r = $self->_chat(
        msg      => $flags ? 21 : 20,
        payload  => $flags ? pack("L L a*", $namespace->{namespace}, $flags, $key) : pack("L a*", $namespace->{namespace}, $key),
        unpack   => sub { $self->_unpack_affected($flags, $namespace, @_) },
        callback => $param->{callback} && $cb,
    ) or return;

    return 1 if $param->{callback};
    return $cb->($r);
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
        return (OP_SPLICE, [ pack '(w/a*)*', @{$_[0]} ]);
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

=pod

=head3 UpdateMulti

Apply several update operations to a tuple.

    my @op = ([ f1 => add => 10 ], [ f1 => and => 0xFF], [ f2 => set => time() ], [ misc_string => cutend => 3 ]);

    my $n_updated = $box->UpdateMulti($key, @op) or die $box->ErrorStr;
    my $n_updated = $box->UpdateMulti($key, @op, \%options) or die $box->ErrorStr;
    warn "Nothing was updated" unless int $n_updated;

    my $updated_tuple_set = $box->UpdateMulti($key, @op, { want_result => 1 }) or die $box->ErrorStr;
    warn "Nothing was updated" unless @$updated_tuple_set;

Different fields can be updated at one shot.
The same field can be updated more than once.
All update operations are done atomically.
Returns false upon error.

=over

=item B<@op> = ([ $field => $op => $value ], ...)

=over

=item B<$field>

Field-to-update number or name (see L</fields>).

=item B<$op>

=over

=item B<set>

Set C<< $field >> to C<< $value >>

=item B<add>, B<and>, B<xor>, B<or>

Apply an arithmetic operation to C<< $field >> with argument C<< $value >>
Currently arithmetic operations are supported only for int32 (4-byte length) fields (and C<$value>s too)

=item B<splice>, B<substr>

Apply a perl-like L<splice|perlfunc/splice> operation to C<< $field >>. B<$value> = [$OFFSET, $LENGTH, $REPLACE_WITH].
substr is just an alias.

=item B<append>, B<prepend>

Append or prepend C<< $field >> with C<$value> string.

=item B<cutbeg>, B<cutend>

Cut C<< $value >> bytes from beginning or end of C<< $field >>.

=back

=back

=item B<%options>

=over

=item B<space> => $space_id_uint32_or_name_string

Specify storage space (by id or name) to work on.

=item B<want_updated_tuple> => $bool

if C<$bool> then return updated tuple.

=back

=cut

sub UpdateMulti {
    my ($param, $namespace) = $_[0]->_validate_param(\@_, qw/want_updated_tuple want_result _flags raw/);
    my ($self, $key, @op) = @_;

    $self->_debug("$self->{name}: UPDATEMULTI(NS:$namespace->{namespace},KEY:$key)[@{[map{$_?qq{[@$_]}:q{-}}@op]}]") if $self->{debug} >= 3;

    confess "$self->{name}\->UpdateMulti: for now key cardinality of 1 is only allowed" unless 1 == @{$param->{index}->{keys}};
    confess "$self->{name}: too many op" if scalar @op > 128;

    $param->{want_result} = $param->{want_updated_tuple} if !defined $param->{want_result};

    my $flags = $param->{_flags} || 0;
    $flags |= WANT_RESULT if $param->{want_result};

    my $fmt = $namespace->{field_format};
    my $fields_hash = $namespace->{fields_hash};

    foreach (@op) {
        confess "$self->{name}: bad op <$_>" if ref ne 'ARRAY' or @$_ != 3;
        my ($field_num, $op, $value) = @$_;

        if($field_num =~ m/^[A-Za-z]/) {
            confess "no such field $field_num in space $namespace->{name}($namespace->{space})" unless exists $fields_hash->{$field_num};
            $field_num = $fields_hash->{$field_num};
        }

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

    my $cb = sub {
        my ($r) = @_;

        if($param->{want_result}) {
            $self->_PostSelect($r, $param, $namespace);
            $r = $r && $r->[0];
        }

        return $param->{callback}->($r) if $param->{callback};
        return $r;
    };

    my $r = $self->_chat(
        msg      => 19,
        payload  => pack("LL a* L (a*)*" , $namespace->{namespace}, $flags, $key, scalar(@op), @op),
        unpack   => sub { $self->_unpack_affected($flags, $namespace, @_) },
        callback => $param->{callback} && $cb,
    ) or return;

    return 1 if $param->{callback};
    return $cb->($r);
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

=head2 AnyEvent

C<< Insert, UpdateMulti, Select, Delete, Call >> methods can be given the following options:

=over

=item B<callback> => sub { my ($data, $error) = @_; }

Do an async request using AnyEvent.
C<< $data >> contains unpacked and processed according to request options data.
C<< $error >> contains a message string in case of error.
Set up C<< raise => 0 >> to use this option.

=back

=head2 "Continuations"

C<< Select >> methods can be given the following options:

=over

=item B<return_fh> => 1

The request does only send operation on network, and returns
C<< { fh => $IO_Handle, continue => $code } >> or false if send operation failed.
C<< $code >> reads data from network, unpacks, processes according to options and returns it.

You should handle timeouts and retries manually (using select() call for example).
Usage example:

    my $continuation = $box->Select(13,{ return_fh => 1 });
    ok $continuation, "select/continuation";

    my $rin = '';
    vec($rin,$continuation->{fh}->fileno,1) = 1;
    my $ein = $rin;
    ok 0 <= select($rin,undef,$ein,2), "select/continuation/select";

    my $res = $continuation->{continue}->();
    use Data::Dumper;
    is_deeply $res, [13, 'some_email@test.mail.ru', 1, 2, 3, 4, '123456789'], "select/continuation/result";

=back

=head2 LongTuple

If C<format> given to L</new>, or C<unpack_format> given to L</Call> ends with a star (C<< * >>)
I<long tuple> is enabled. Last field or group of fields of C<format> represent variable-length
tail of the tuple. C<long_fields> option given to L</new> will fold the tail into array of hashes.

    $box->Insert(1,"2",3);
    $box->Insert(3,"2",3,4,5);
    $box->Insert(5,"2",3,4,5,6,7);

If we set up

    format => "L&CL*",
    fields => [qw/ a b c d /], # d is the folding field here
    # no long_fields - no folding into hash

we'll get:

    $result = $box->Select([1,2,3,4,5]);
    $result = [
        { a => 1, b => "2", c => 3, d => [] },
        { a => 3, b => "2", c => 3, d => [4,5] },
        { a => 5, b => "2", c => 3, d => [4,5,6,7] },
    ];

And if we set up

    format => "L&C(LL)*",
    fields => [qw/ a b c d /], # d is the folding field here
    long_fields => [qw/ d1 d2 /],

we'll get:

    $result = [
        { a => 1, b => "2", c => 3, d => [] },
        { a => 3, b => "2", c => 3, d => [{d1=>4, d2=>5}] },
        { a => 5, b => "2", c => 3, d => [{d1=>4, d2=>5}, {d1=>6, d2=>7}] },
    ];


=head2 utf8

Utf8 strings are supported very simply. When pushing any data to tarantool (with any query, read or write),
the utf8 flag is set off, so all data is pushed as bytestring. When reading response, for fields marked
a dollar sign C<< $ >> (see L</new>) (including such in L</LongTuple> tail) utf8 flag is set on.
That's all. Validity is on your own.


=head1 LICENCE AND COPYRIGHT

This is free software; you can redistribute it and/or modify it under the same terms as the Perl 5 programming language system itself.

=head1 SEE ALSO

=over

=item *

L<http://tarantool.org>

=item *

L<MR::Tarantool::Box::Singleton>

=back

=cut


1;
