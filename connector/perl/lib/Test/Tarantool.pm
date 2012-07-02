use warnings;
use strict;
use utf8;

package Test::Tarantool;
use Carp;
use File::Temp qw(tempfile tempdir);
use File::Path 'rmtree';
use File::Spec::Functions qw(catfile rel2abs);
use Cwd;
use IO::Socket::INET;

=head1 NAME

Test::Tarantool - finds and starts tarantool on free port.

=head1 SYNOPSIS

my $t = run Test::Tarantool ( cfg => $file_spaces_cfg );

=head1 DESCRIPTION

The module tries to find and then to start B<tarantool_box>.

The module is used inside tests.


=head1 METHODS

=head2 run

Constructor. Receives the following arguments:

=over

=item cfg

path to tarantool.cfg

=back

=cut


sub run {
    my ($module, %opts) = @_;

    my $cfg_file = delete $opts{cfg} or croak "config file not defined";
    croak "File not found: $cfg_file" unless -r $cfg_file;
    open my $fh, '<:encoding(UTF-8)', $cfg_file or die "$@\n";
    local $/;
    my $cfg = <$fh>;

    my %self = (
        admin_port => $module->_find_free_port,
        primary_port => $module->_find_free_port,
        secondary_port => $module->_find_free_port,
        cfg_data => $cfg,
        master => $$,
        cwd => getcwd,
        add_opts => \%opts
    );

    $opts{script_dir} = rel2abs $opts{script_dir} if $opts{script_dir};

    my $self = bless \%self => $module;
    $self->_start_tarantool;
    $self;
}


=head2 started

Returns true if tarantool is found and started

=cut

sub started {
    my ($self) = @_;
    return $self->{started};
}


=head2 log

Returns tarantool logs

=cut

sub log {
    my ($self) = @_;
    return '' unless $self->{log} and -r $self->{log};
    open my $fh, '<encoding(UTF-8)', $self->{log};
    local $/;
    my $l = <$fh>;
    return $l;
}


sub config_body {
    my ($self) = @_;
    return $self->{config_body} || '';
}

sub _start_tarantool {
    my ($self) = @_;
    $self->{temp} = tempdir;
    $self->{cfg} = catfile $self->{temp}, 'tarantool.cfg';
    $self->{log} = catfile $self->{temp}, 'tarantool.log';
    $self->{pid} = catfile $self->{temp}, 'tarantool.pid';



    $self->{config_body} = $self->{cfg_data};
    $self->{config_body} .= "\n\n";
    $self->{config_body} .= "slab_alloc_arena = 1.1\n";
    $self->{config_body} .= sprintf "pid_file = %s\n", $self->{pid};

    $self->{config_body} .= sprintf "%s = %s\n", $_, $self->{$_}
        for (qw(admin_port primary_port secondary_port));

    $self->{config_body} .= sprintf qq{logger = "cat > %s"\n}, $self->{log};

    for (keys %{ $self->{add_opts} }) {
        my $v = $self->{add_opts}{ $_ };

        if ($v =~ /^\d+$/) {
            $self->{config_body} .= sprintf qq{%s = %s\n}, $_, $v;
        } else {
            $self->{config_body} .= sprintf qq{%s = "%s"\n}, $_, $v;
        }
    }

    return unless open my $fh, '>:raw', $self->{cfg};
    print $fh $self->{config_body};
    close $fh;

    chdir $self->{temp};

    system "tarantool_box -c $self->{cfg} --check-config > $self->{log} 2>&1";
    goto EXIT if $?;

    system "tarantool_box -c $self->{cfg} --init-storage >> $self->{log} 2>&1";
    goto EXIT if $?;

    unless ($self->{child} = fork) {
        exec "tarantool_box -c $self->{cfg}";
        die "Can't start tarantool_box: $!\n";
    }

    $self->{started} = 1;


    # wait for starting tarantool
    for (my $i = 0; $i < 100; $i++) {
        last if IO::Socket::INET->new(
            PeerAddr => '127.0.0.1', PeerPort => $self->primary_port
        );

        sleep 0.01;
    }

    EXIT:
        chdir $self->{cwd};
}


=head2 primary_port

Returns tarantool primary port

=cut

sub primary_port { return $_[0]->{primary_port} }


=head2 tarantool_pid

Returns B<PID>

=cut

sub tarantool_pid { return $_[0]->{child} }


=head2 kill

Kills tarantool

=cut

sub kill :method {
    my ($self) = @_;

    if ($self->{child}) {
        kill 'TERM' => $self->{child};
        waitpid $self->{child}, 0;
        delete $self->{child};
    }
}

=head2 DESTROY

Destructor. Kills tarantool, removes temporary files.

=cut

sub DESTROY {
    my ($self) = @_;
    return unless $self->{master} == $$;
    $self->kill;
    rmtree $self->{temp} if $self->{temp};
}

{
    my $start_port;

    sub _find_free_port {
        $start_port = 10000 unless defined $start_port;

        while( ++$start_port < 60000 ) {
            return $start_port if IO::Socket::INET->new(
                Listen => 5,
                LocalAddr => '127.0.0.1',
                LocalPort => $start_port,
                Proto => 'tcp',
                (($^O eq 'MSWin32') ? () : (ReuseAddr => 1)),
            );
        }

        croak "Can't find free port";
    }
}

=head1 COPYRIGHT AND LICENSE

Copyright (C) 2011 Dmitry E. Oboukhov <unera@debian.org>
Copyright (C) 2011 Roman V. Nikolaev <rshadow@rambler.ru>

This program is free software, you can redistribute it and/or
modify it under the terms of the Artistic License.

=head1 VCS

The project is placed git repo on github:
L<https://github.com/unera/dr-tarantool/>.

=cut

1;
