# $Id: Const.pm,v 1.12 2010/06/17 13:35:13 dubravsky Exp $

use strict;
use Exporter;

package MR::Storage::Const;
use base qw/Exporter/;

use vars qw/@EXPORT_OK %EXPORT_TAGS %ERRORS/;

my (@ERRORS);
$EXPORT_TAGS{all}    = \@EXPORT_OK;
$EXPORT_TAGS{errors} = \@ERRORS;

BEGIN {
    sub constantiate($$;@) {
        my ($const, @arrs) = @_;
        my $pkg = (caller)[0];
        my @a;
        eval qq{ sub $pkg\::$_ () { $const->{$_} }; push \@a, q{$_}; 'TRUE'.$_; } or die $@
        for keys %$const;
        push @$_, @a for @arrs;
    }

    # Errors. Numbers >=0 are returned by server, <0 are internals for the module.
    constantiate {
        SE_OK                                       =>    0,
        SE_UNKNOWN                                  =>   -1,
        SE_COMM                                     =>   -2,
    }, \@ERRORS, \@EXPORT_OK;
}

%ERRORS = (
    0x00000000  => q{OK},
    0x00000100  => q{Non master connection, but it should be},
    0x00000200  => q{Illegal parametrs},
    0x00000300  => q{Uid not from this storage range},
    0x00000400  => q{Node is marked as read-only},
    0x00000500  => q{Node isn't locked},
    0x00000600  => q{Node is locked},
    0x00000700  => q{Some memory issues},
    0x00000800  => q{Bad integrity},
    0x00000a00  => q{Unsupported command},

    0x00000b00  => q{Can't do select},
    0x00000c00  => q{Silverspoon for first uid is not found},
    0x00000d00  => q{Silverspoon for second uid is not found},
    0x00000e00  => q{Silverspoons for both uids are not found},
    0x00000f00  => q{Can't do 2pc prepare on first sp},
    0x00001000  => q{Can't do 2pc prepare on second sp},
    0x00001100  => q{Can't do 2pc prepare on both sps},
    0x00001200  => q{Can't do 2pc commit on first sp},
    0x00001300  => q{Can't do 2pc commit on second sp},
    0x00001400  => q{Can't do 2pc commit on both sps},
    0x00001500  => q{Can't do 2pc abort on first sp},
    0x00001600  => q{Can't do 2pc abort on second sp},
    0x00001700  => q{Can't do 2pc abort on both sps},

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
    0x00002700  => q{Bad record ID},
    0x00002800  => q{WAL failed},
    0x00002900  => q{Bad event ID},
    0x00002a00  => q{Read-only mode},
    0x00002b00  => q{Event is locked},
    0x00002c00  => q{Event is already liked by this user},
    0x00002d00  => q{Event is not liked by this user},
    0x00002e00  => q{The message has been already deleted by this user},
    0x00002f00  => q{User can't delete message},
    0x00003100  => q{Node not found},
    0x00003200  => q{User isn't member of the dialogue},
    0x00003300  => q{Dialogue not found},
    0x00003400  => q{Error during iconv},
    0x00003500  => q{Event isn't contained in this node},
    0x00003600  => q{Proxy reply: destination node timed out},
);

sub ErrorText {
    my $e = $_[1] & 0xFFFFFF00;
    return $ERRORS{$e} if exists $ERRORS{$e};
    return 'Unknown error';
}

sub ErrorStr {
    my $e = $_[1] ? ($_[1] % 0xFF) ? 'Error' : 'Warning' : '';
    $e &&= sprintf "$e %08X: ", $_[1];
    return sprintf "%s%s", $e, $_[0]->ErrorText($_[1]);
}

package MR::Storage::Const::Errors;
use base qw/MR::Storage::Const/;

package MR::Storage::Const::Errors::SilverBox;
use base qw/MR::Storage::Const::Errors/;

1;
