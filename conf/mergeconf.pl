#!/usr/bin/perl -w

use strict;
use Getopt::Long;
my $help = 0;

sub usage
{
    print <<EOF;
    usage: mergeconf.pl <base_config_file> <override_config_file>

    Merge the contents of <override_config_file> with <base_config_file>
    and write the merged result to stdout.

    --help,-h           this help message
EOF
    exit 1
}

GetOptions ("help|?"   => \$help)
            or usage();

usage() if $help or scalar(@ARGV) != 2;
my $base = $ARGV[0];
my $override = $ARGV[1];


my %overrideVals;
my $currentSection = '';
if (-e $override) {
    open INPUT, "<$override" or die "Cannot open $override\n";
    while (<INPUT>) {
        if (m/^\[([^\]]+)\]\s*$/) {
            $currentSection = $1;
        }
        elsif (m/^([^=]+)=(.+)$/) {
            my $abs_prop = "$currentSection-$1";
            $overrideVals{$abs_prop} = $2;
        }
    }
    close INPUT;
}
else {
    $override = "";
}

my $created_at = localtime;
printf ("#\n# Created: %s\n", $created_at);
printf ("# By merging %s and %s\n", $base, $override) if $override;

$currentSection = '';
open OUTPUT, "<$base" or die "Cannot open $base\n";
while (<OUTPUT>) {
    if (m/^\[([^\]]+)\]\s*$/) {
        my $newSection = $1;

        # Write out any new values that exist in the override file, but not in the base file
        my $printedOne = 0;
        foreach my $key (keys %overrideVals) {
            if ($key =~ m/^([^-]+)-(.+)$/) {
                if ($1 eq $currentSection) {
                    printf "%s=%s\n", $2, $overrideVals{$key};
                    delete $overrideVals{$key};
                    $printedOne = 1;
                }
            }
        }
        print "\n" if $printedOne;
        $currentSection = $newSection;
        print "[$currentSection]\n";
    }
    elsif (m/^([^=]+)=(.+)$/) {
        my $abs_prop = "$currentSection-$1";
        if ($overrideVals{$abs_prop}) {
            printf ("$1=%s\n", $overrideVals{$abs_prop});
            delete $overrideVals{$abs_prop};
        }
        else {
            print
        }
    }
    else {
        print;
    }
}
close OUTPUT;

# Write out any new values that exist in the override file, but not in the base file for the last section
foreach my $key (sort keys %overrideVals) {
    if ($key =~ m/^([^-]+)-(.+)$/) {
        if ($1 ne $currentSection) {
            $currentSection = $1;
            print "\n[$currentSection]\n";
        }
        printf "%s=%s\n", $2, $overrideVals{$key};
    }
}
