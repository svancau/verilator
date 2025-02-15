#!/usr/bin/perl
if (!$::Driver) { use FindBin; exec("$FindBin::Bin/bootstrap.pl", @ARGV, $0); die; }
# DESCRIPTION: Verilator: Verilog Test driver/expect definition
#
# Copyright 2003 by Wilson Snyder. This program is free software; you can
# redistribute it and/or modify it under the terms of either the GNU
# Lesser General Public License Version 3 or the Perl Artistic License
# Version 2.0.

use IO::File;
use strict;
use vars qw($Self);

scenarios(dist => 1);

my $root = "..";
my $Debug;
my %Contributors;
my %Authors;

if (!-r "$root/.git") {
    skip("Not in a git repository");
} else {
    check();
}

ok(1);
1;

sub check {
    read_contributors("$root/docs/CONTRIBUTORS");
    read_user();
    read_authors();
    for my $author (sort keys %Authors) {
        print "Check: $author\n" if $Self->{verbose};
        if (!$Contributors{$author}) {
            error("Certify your contribution by appending '$author' to CONTRIBUTORS");
        }
    }
}

sub read_contributors {
    my $filename = shift;
    my $fh = IO::File->new("<$filename")
        or error("$! $filename");
    # Assumes git .mailmap format
    while (my $line = ($fh && $fh->getline)) {
        while ($line =~ /(.*)/g) {
            $line =~ s/ *<[^>]+>//;
            $Contributors{$1} = 1;
        }
    }
}

sub read_user {
    my $changes = `cd $root ; git diff-index --quiet HEAD --`;
    chomp $changes;
    if (!$changes) {
        print "No git changes\n" if $Self->{verbose};
    } else {
        # Uncommitted changes, so check the user's git name
        my $user = `git config user.name`;
        chomp $user;
        my $email = `git config user.email`;
        chomp $email;
        if ($user && $email) {
            $Authors{"$user <$email>"} = 1;
        }
    }
}

sub read_authors {
    # Check recent commits in case did commit
    my $git_auths = `git log '--pretty=format:%aN <%aE>' | head -20`;
    foreach my $line (split /\n/, $git_auths) {
        $line =~ s/ *<[^>]+>//;
        $Authors{$line} = 1;
    }
}
