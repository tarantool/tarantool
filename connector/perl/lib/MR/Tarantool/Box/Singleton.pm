package MR::Tarantool::Box::Singleton;

=pod

=head1 NAME

MR::Tarantool::Box::Singleton - A singleton wrapper for L<MR::Tarantool::Box>.

Provides connection-persistence and replica fallback.
Please read L<"MR::Tarantool::Box manual"|MR::Tarantool::Box> first.

=head1 SYNOPSIS

    package Some::Tarantool::Box::Singleton;
    use MR::Tarantool::Box::Singleton;
    use base 'MR::Tarantool::Box::Singleton';

    BEGIN { # generates "TUPLE_$field_name" constants, and methods: FIELDS, FIELDS_HASH
        __PACKAGE__->mkfields(qw/ id f1 f2 f3 field4 f5 f6 f7 misc_string /); # applicable for DEFAULT_SPACE only
    }

    sub SERVER   { Some::Config->GetBoxServer()   }
    sub REPLICAS { Some::Config->GetBoxReplicas() }

    sub DEFAULT_SPACE { 0 }

    sub SPACES   {[{
        space         => 0,
        indexes => [ {
            index_name   => 'primary_id',
            keys         => [TUPLE_id],
        }, {
            index_name   => 'secondary_f1f2',
            keys         => [TUPLE_f1, TUPLE_f2],
        }, ],
        format        => 'QqLlSsCc&',
        default_index => 'primary_id',
    }, {
        space         => 1,
        indexes => [ {
            index_name   => 'primary_id',
            keys         => [0],
        }, ],
        format        => '&&&&',
        fields        => [qw/ string1 str2 s3 s4 /],
    }]}

=head1 DESCRIPTION

=head2 METHODS

=cut

use strict;
use warnings;

use MR::Tarantool::Box;
use Class::Singleton;
use Carp qw/confess cluck/;
use List::Util qw/shuffle/;

use base qw/Class::Singleton/;

=pod

=head3 mkfields

    BEGIN {
        $CLASS->mkfields(@names);
    }

=over

=item *

Generates constants "TUPLE_$fieldname" => $fieldposition in C<$CLASS>.
Just Like if you say C<< use constant TUPLE_id => 0, TUPLE_f1 => 1, ...; >>

=item *

Generates C<$CLASS> variable C<< @fields >> containing field names,
and a C<$CLASS> method C<FIELDS> returning C<< @fields >>.

=item *

Generates C<$CLASS> variable C<< %fields >> containing field names mapping to positions,
and a C<$CLASS> method C<FIELDS_HASH> returning C<< \%fields >>.

=item *

These C<< @fields >> are applied to the C<< DEFAULT_SPACE >>,
if I<< fields >> were not set explicitly for that space.

=back

=cut

sub _mkfields {
    my($class, $f, $F, @fields) = @_;
    no strict 'refs';
    confess "$f are already defined for $class" if @{"${class}::${f}"};
    @{"${class}::${f}"} = @fields;
    %{"${class}::${f}"} = map { $fields[$_] => $_ } 0..$#fields;
    eval qq{ sub ${class}::${F}TUPLE_$fields[$_] () { $_ } } for 0..$#fields;
    eval qq{ sub ${class}::${F}FIELDS      () {   \@${class}::${f} } };
    eval qq{ sub ${class}::${F}FIELDS_HASH () { \\\%${class}::${f} } };
}

sub mkfields     { $_[0]->_mkfields('fields',      '',      @_[1..$#_]) }
sub mklongfields { $_[0]->_mkfields('long_fields', 'LONG_', @_[1..$#_]) }

=pod

=head3 declare_stored_procedure

    $CLASS->declare_stored_procedure(%args);
    
    $CLASS->declare_stored_procedure(
        name             => "box.do.something",                        # internal procedure name, in da box
        method_name      => "CallMyTestingStoredProcedure",            # will generate method named
        options          => { default => options },                    # MR::Tarantool::Box->Call \%options
        params           => [ qw{ P1 P2 P3 Param4 }],                  # names
    
        unpack_format    => "&LSC(L$)*",
    
        params_format    => [qw{ C S L a* }],
        params_default   => [ 1, 2, undef, 'the_default' ],            # undef's are mandatory params
    );
    
    ...
    
    my $data = $CLASS->CallMyTestingStoredProcedure(
        P1 => $val1,
        P2 => $val2,
        P3 => $val3,
        Param4 => $val3,
        { option => $value }, # optional
    ) or warn $CLASS->ErrorStr;

Declare a stored procedure. This generates C<$CLASS> method C<< $args{method_name} >> which
calls Tarantool/Box procedure C<< $args{name} >>, using C<< $args{options} >> as default
C<< \%options >> for C<< MR::Tarantool::Box->Call >> call. The generated method has the following
prototype:

    $CLASS->CallMyTestingStoredProcedure( %sp_params, \%optional_options );

Parameters description:

=over

=item B<%args>:

=over

=item B<name> => $tarantool_box_sp_name

The name of procedure in Tarantool/Box to call.

=item B<method_name> => $class_method_name

Class method name to generate.

=item B<options> => \%options

Options to pass to L<MR::Taranatool::Box->Call|MR::Taranatool::Box/Call> method.

=item B<params> => \@names

Procedure input parameters' names

=item B<params_default> => \@defaults

Procedure input parameters default values. Undefined or absent value makes
its parameter mandatory.

=item B<params_format> => \@format

C<< pack() >>-compatible format to pack input parameters. Must match C<params>.

=item B<unpack_format> => $format

C<< pack() >>-compatible format to unpack procedure output.

=back

=item B<%sp_params>:

C<< Name => $value >> pairs.

=item B<%optional_options>:

Options to pass to L<MR::Taranatool::Box->Call|MR::Taranatool::Box/Call> method.
This overrides C<< %options >> values key-by-key.

=back

=cut

sub declare_stored_procedure {
    my($class, %opts) = @_;
    my $name = delete $opts{name} or confess "No `name` given";
    my $options = $opts{options} || {};

    confess "no `params` given; it must be an arrayref" if !exists $opts{params} or ref $opts{params} ne 'ARRAY';
    my @params = @{$opts{params}};

    my $pack;
    if(my $fn = $opts{pack}) {
        confess "`params_format` and `params_default` are not applicable while `pack` is in use" if exists $opts{params_format} or exists $opts{params_default};
        if(ref $fn) {
            confess "`pack` can be code ref or a method name, nothing else" unless ref $fn eq 'CODE';
            $pack = $fn;
        } else {
            confess "`pack` method $fn is not provided by class ${class}" unless $class->can($fn);
            $pack = sub { $class->$fn(@_) };
        }
    } else {
        confess "no `pack` nor `params_format` given; it must be an arrayref with number of elements exactly as in `params`" if !exists $opts{params_format} or ref $opts{params_format} ne 'ARRAY' or @{$opts{params_format}} != @params;
        confess "`params_default` is given but it must be an arrayref with number of elements no more then in `params`" if exists $opts{params_format} and (ref $opts{params_format} ne 'ARRAY' or @{$opts{params_format}} > @params);
        my @fmt = @{$opts{params_format}};
        my @def = @{$opts{params_default}||[]};
        $pack = sub {
            my $p = $_[0];
            for my $i (0..$#params) {
                $p->[$i] = $def[$i] if !defined$p->[$i] and $i < @def;
                confess "All params must be defined" unless defined $p->[$i];
                $p->[$i] = pack $fmt[$i], $p->[$i];
            }
            return $p;
        };
    }

    my $unpack;
    if(my $fn = $opts{unpack}) {
        if(ref $fn) {
            confess "`unpack` can be code ref or a method name, nothing else" unless ref $fn eq 'CODE';
            $unpack = $fn;
        } else {
            confess "`unpack` method $fn is not provided by class ${class}" unless $class->can($fn);
            $unpack = sub { $class->$fn(@_) };
        }
        if ($opts{unpack_raw}) {
            $options->{unpack} = $unpack;
            undef $unpack;
        }
        $options->{unpack_format} = '&*';
    } else {
        confess "no `unpack` nor `unpack_format` given" if !exists $opts{unpack_format};
        my $f = $opts{unpack_format};
        $f = join '', @$f if ref $f;
        $options->{unpack_format} = $f;
    }

    my $method = $opts{method_name} or confess "`method_name` not given";
    confess "bad `method_name` $method" unless $method =~ m/^[a-zA-Z]\w*$/;
    my $fn = "${class}::${method}";
    confess "Method $method is already defined in class $class" if defined &{$fn};
    do {
        no strict 'refs';
        *$fn = sub {
            my $p0 = @_ && ref $_[-1] eq 'HASH' ? pop : {};
            my $param = { %$options, %$p0 };
            my ($class, %params) = @_;
            my $res = $class->Call($name, $pack->([@params{@params}]), $param) or return;
            return $res unless $unpack;
            return $unpack->($res);
        }
    };
    return $method;
}

sub Param {
    confess "bad Param call" unless $_[2];
    return $_[2] && @{$_[2]} && ref $_[2]->[-1] eq 'HASH' && pop @{$_[2]} || {};
}

=pod

=head3 Configuration methods

=over

=item B<SERVER>

Must return a string of ip:port of I<master> server.

=item B<REPLICAS>

Must return a comma separated string of ip:port pairs of I<replica> servers (see L</is_replica>).
Server is chosen from the list randomly.

=item B<MR_TARANTOOL_BOX_CLASS>

Must return name of the class implementing L<MR::Tarantool::Box> interface, or it's descendant.

=item B<SPACES>, B<RAISE>, B<TIMEOUT>, B<SELECT_TIMEOUT>, B<RETRY>, B<SELECT_RETRY>, B<SOFT_RETRY>, B<DEBUG>

See corresponding arguments of L<MR::Tarantool::Box->new|MR::Tarantool::Box/new> method.

=back

=cut

sub DEBUG           () { 0 }
sub IPDEBUG         () { 0 }

sub TIMEOUT         () { 23 }
sub SELECT_TIMEOUT  () {  2 }

sub RAISE           () { 1 }

sub RETRY           () { 1 }
sub SELECT_RETRY    () { 3 }
sub SOFT_RETRY      () { 3 }
sub RETRY_DELAY     () { 1 }

sub SERVER          () { die }
sub REPLICAS        () { []  }

sub MR_TARANTOOL_BOX_CLASS () { 'MR::Tarantool::Box' }

sub SPACES          () { die }
sub DEFAULT_SPACE   () { undef }

sub _new_instance {
    my ($class) = @_;
    my ($config) = $class->can('_config') ? $class->_config : {};
    $config->{param} ||= {};

    $config->{servers}                     ||= $class->SERVER;

    $config->{param}->{name}               ||= $class;
    $config->{param}->{spaces}             ||= $class->SPACES;
    $config->{param}->{default_fields}     ||= [ $class->FIELDS ]      if $class->can('FIELDS');
    $config->{param}->{default_long_fields}||= [ $class->LONG_FIELDS ] if $class->can('LONG_FIELDS');

    $config->{param}->{raise}                = $class->RAISE unless defined $config->{param}->{raise};
    $config->{param}->{timeout}            ||= $class->TIMEOUT;
    $config->{param}->{select_timeout}     ||= $class->SELECT_TIMEOUT;
    $config->{param}->{debug}              ||= $class->DEBUG;
    $config->{param}->{ipdebug}            ||= $class->IPDEBUG;

    $config->{param}->{retry}              ||= $class->RETRY;
    $config->{param}->{select_retry}       ||= $class->SELECT_RETRY;
    $config->{param}->{softretry}          ||= $class->SOFT_RETRY;
    $config->{param}->{retry_delay}        ||= $class->RETRY_DELAY;

    my $replicas = delete $config->{replicas} || $class->REPLICAS || [];
    $replicas = [ split /,/, $replicas ] unless ref $replicas eq 'ARRAY';

    $class->CheckConfig($config);

    return bless {
        box      => $class->MR_TARANTOOL_BOX_CLASS->new({ servers => $config->{servers}, %{$config->{param}} }),
        replicas => [ map { $class->MR_TARANTOOL_BOX_CLASS->new({ servers => $_, %{$config->{param}} }) } shuffle @$replicas ],
    }, $class;
}

sub CheckConfig {}

=pod

=head3 Add, Insert, Replace, UpdateMulti, Delete

These methods operate on C<< SERVER >> only.
See corresponding methods of L<MR::Tarantool::Box> class.

=head3 Select, Call

These methods operate on C<< SERVER >> at first, and then B<may>
try to query C<< REPLICAS >>.

See corresponding methods of L<MR::Tarantool::Box> class.

These methods have additional C<< %options >> params:

=over

=item B<is_replica> => \$is_result_from_replica

If this option is set, then if the query to C<< SERVER >> fails,
C<< REPLICAS >> will be queried one-by-one until query succeeds or
the list ends, and C<< $is_result_from_replica >> will be set to
C<< true >>, no matter whether any query succeeds or not.

=back

=cut

BEGIN {

    foreach my $method (qw/Insert UpdateMulti Delete Add Set Replace Bit Num AndXorAdd Update/) {
        no strict 'refs';
        *$method = sub {
            use strict;
            my ($class, @args) = @_;
            my $param = $class->Param($method, \@args);
            my $self = $class->instance;
            $self->{_last_box} = $self->{box};
            $self->{box}->$method(@args, $param);
        };
    }

    foreach my $method (qw/Select SelectUnion Call/) {
        no strict 'refs';
        *$method = sub {
            use strict;
            my ($class, @args) = @_;
            my $param = $class->Param($method, \@args);

            if ($param->{format}) {
                my @F;
                my $F = $class->FIELDS_HASH;
                my @format = ref $param->{format} eq 'ARRAY' ? @{$param->{format}} : %{$param->{format}};
                confess "Odd number of elements in format" if @format % 2;
                $param->{format} = [];
                while( my ($field, $fmt) = splice(@format, 0, 2) ) {
                    confess "Bad format for field `$field'" unless $fmt;
                    confess "Unknown field `$field'" unless exists $F->{$field};
                    push @F, $field;
                    push @{$param->{format}}, {
                        field  => $F->{$field},
                        $fmt eq 'full' ? (
                            full => 1,
                        ) : (
                            offset => $fmt->{offset} || 0,
                            length => (exists $fmt->{length} ? $fmt->{length}||0 : 'max'),
                        ),
                    };
                }
                $param->{hashify} = sub { $class->_hashify(\@F, @_) };
            }

            die "${class}\->${method}: is_replica must be a SCALARREF" if exists $param->{is_replica} && ref $param->{is_replica} ne 'SCALAR';
            my $is_rep = delete $param->{is_replica};
            $$is_rep = 0 if $is_rep;
            my $self = $class->instance;
            my @rep = $is_rep ? @{ $self->{replicas} } : ();
            my ($ret,@ret);
            for(my $box = $self->{box}; $box; $box = shift @rep) {
                $self->{_last_box} = $box;
                if(wantarray) {
                    @ret = $box->$method(@args, $param);
                } elsif(defined wantarray) {
                    $ret = $box->$method(@args, $param);
                } else {
                    $box->$method(@args, $param);
                }
                last if !$box->Error or !$is_rep or !@rep;
                ++$$is_rep;
            }
            return wantarray ? @ret : $ret;
        };
    }
}

=pod

=head3 B<Error>, B<ErrorStr>

Return error code or description (see <MR::Tarantool::Box|MR::Tarantool::Box/Error>).

=cut

sub Error {
    my ($class, @args) = @_;
    $class->instance->{_last_box}->Error(@args);
}

sub ErrorStr {
    my ($class, @args) = @_;
    $class->instance->{_last_box}->ErrorStr(@args);
}

=pod

=head1 LICENCE AND COPYRIGHT

This is free software; you can redistribute it and/or modify it under the same terms as the Perl 5 programming language system itself.

=head1 SEE ALSO

L<http://tarantool.org>

L<MR::Tarantool::Box>

=cut


1;
