package MR::IProto::Cluster;

=head1 NAME

MR::IProto::Cluster - cluster of servers

=head1 DESCRIPTION

This class is used to implement balancing between several servers.

=cut

use Moose;
use Moose::Util::TypeConstraints;
use MR::IProto::Cluster::Server;

=head1 EXPORTED CONSTANTS

=over

=item RR

Round robin algorithm

=item HASH

Hashing algorithm using CRC32

=item KETAMA

Ketama algorithm

=back

=cut

use Exporter 'import';
our @EXPORT_OK = qw( RR HASH KETAMA );

coerce 'MR::IProto::Cluster'
    => from 'Str'
    => via { __PACKAGE__->new( servers => $_ ) };

subtype 'MR::IProto::Cluster::Servers'
    => as 'ArrayRef[MR::IProto::Cluster::Server]'
    => where { scalar @_ };
coerce 'MR::IProto::Cluster::Servers'
    => from 'Str'
    => via {
        my $type = find_type_constraint('MR::IProto::Cluster::Server');
        [ map $type->coerce($_), split /,/, $_ ];
    };

use constant {
    RR     => 1,
    HASH   => 2,
    KETAMA => 3,
};

enum 'MR::IProto::Balance' => (
    RR,
    HASH,
    KETAMA,
);
coerce 'MR::IProto::Balance'
    => from 'Str',
    => via {
        $_ eq 'hash-crc32' ? HASH
            : $_ eq 'ketama' ? KETAMA
            : RR;
    };

=head1 ATTRIBUTES

=over

=item balance

Balancing algorithms.
Possible values are constants: RR, HASH, KETAMA.
Or their string analogs: 'round-robin', 'hash-crc32', 'ketama'.

=cut

has balance => (
    is  => 'ro',
    isa => 'MR::IProto::Balance',
    default => RR,
    coerce  => 1,
);

=item servers

ArrayRef of L<MR::IProto::Cluster::Server>.

=cut

has servers => (
    is  => 'ro',
    isa => 'MR::IProto::Cluster::Servers',
    required => 1,
    coerce   => 1,
);

=back

=cut

has _ketama => (
    is  => 'ro',
    isa => '',
    lazy_build => 1,
);

has _rr_servers => (
    is  => 'rw',
    isa => 'MR::IProto::Cluster::Servers',
    lazy_build => 1,
);

=head1 PUBLIC METHODS

=over

=item server( $key? )

Get server from balancing using C<$key>.

=cut

sub server {
    my ($self, $key) = @_;
    my $n = @{$self->servers};
    return $self->servers->[0] if $n == 1;
    my $method = $self->balance == RR ? '_balance_rr'
        : $self->balance == KETAMA ? '_balance_ketama'
        : '_balance_hash';
    return $self->$method($key);
}

=item timeout( $new? )

Used to set C<$new> timeout value to all servers.
If argument is skipped and timeout is equal for all servers then returns
it value, if timeout is different then returns undef.

=cut

sub timeout {
    my $self = shift;
    if(@_) {
        my $timeout = shift;
        $_->timeout($timeout) foreach @{$self->servers};
        return $timeout;
    }
    else {
        my $timeout;
        foreach my $t ( map $_->timeout, @{$self->servers} ) {
            return if defined $timeout && $timeout != $t;
            $timeout = $t unless defined $timeout;
        }
        return $timeout;
    }
}

=back

=cut

sub _build__ketama {
    my $self = shift;
    my @ketama;
    foreach my $server (@{$self->servers}) {
        for my $i (0..10) {
           push @ketama, [crc32($server->host.$server->port.$i), $server];
        }
    }
    return sort { $a->[0] cmp $b->[0] } @ketama;
}

sub _build__rr_servers {
    my ($self) = @_;
    return [ @{ $self->servers } ];
}

sub _balance_rr {
    my ($self) = @_;
    $self->_clear_rr_servers() if @{$self->_rr_servers} == 0;
    return splice(@{$self->_rr_servers}, int(rand(@{$self->_rr_servers})), 1);
}

sub _balance_hash {
    my ($self, $key) = @_;
    my ($hash_, $hash, $server);

    $hash = $hash_ = crc32($key) >> 16;
    for (0..19) {
        $server = $self->servers->[$hash % @{$self->servers}];
        return $server if $server->active;
        $hash += crc32($_, $hash_) >> 16;
    }

    return $self->servers->[rand @{$self->servers}]; #last resort
}

sub _balance_ketama {
    my ($self, $key) = @_;

    my $idx = crc32($key);

    foreach (@{$self->_ketama}) {
       next unless ($_);
       return $_->[1] if ($_->[0] >= $idx);
    }

    return $self->_ketama->[0]->[1];
}

=head1 SEE ALSO

L<MR::IProto>, L<MR::IProto::Cluster::Server>.

=cut

no Moose;
__PACKAGE__->meta->make_immutable();

1;
