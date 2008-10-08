package Zaptel::Config::Defaults;
#
# Written by Oron Peled <oron@actcom.co.il>
# Copyright (C) 2007, Xorcom
# This program is free software; you can redistribute and/or
# modify it under the same terms as Perl itself.
#
# $Id: Defaults.pm 4187 2008-04-21 21:27:59Z tzafrir $
#
use strict;

# Use the shell to source a file and expand a given list
# of variables.
sub do_source($@) {
	my $file = shift;
	my @vars = @_;
	my @output = `env -i sh -ec '. $file; export @vars; for i in @vars; do eval echo \$i=\\\$\$i; done'`;
	die "$0: Sourcing '$file' exited with $?" if $?;
	my %vars;
	
	foreach my $line (@output) {
		chomp $line;
		my ($k, $v) = split(/=/, $line, 2);
		$vars{$k} = $v if grep /^$k$/, @vars;
	}
	return %vars;
}

sub source_vars {
	my @vars = @_;
	my $default_file;
	my %system_files = (
			"/etc/default/zaptel"	=> 'Debian and friends',
			"/etc/sysconfig/zaptel"	=> 'Red Hat and friends',
		);

	if(defined $ENV{ZAPTEL_DEFAULTS}) {
		$default_file = $ENV{ZAPTEL_DEFAULTS};
	} else {
		foreach my $f (keys %system_files) {
			if(-r $f) {
				if(defined $default_file) {
					die "An '$f' collides with '$default_file'";
				}
				$default_file = $f;
			}
		}
	}
	if (! $default_file) {
		return ("", ());
	}
	my %vars = Zaptel::Config::Defaults::do_source($default_file, @vars);
	return ($default_file, %vars);
}

1;
