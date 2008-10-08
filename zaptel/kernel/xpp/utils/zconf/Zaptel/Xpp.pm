package Zaptel::Xpp;
#
# Written by Oron Peled <oron@actcom.co.il>
# Copyright (C) 2007, Xorcom
# This program is free software; you can redistribute and/or
# modify it under the same terms as Perl itself.
#
# $Id: Xpp.pm 4309 2008-05-20 14:54:58Z tzafrir $
#
use strict;
use Zaptel::Xpp::Xbus;

=head1 NAME

Zaptel::Xpp - Perl interface to the Xorcom Astribank drivers.

=head1 SYNOPSIS

  # Listing all Astribanks:
  use Zaptel::Xpp;
  # scans hardware:
  my @xbuses = Zaptel::Xpp::xbuses("SORT_CONNECTOR");
  for my $xbus (@xbuses) {
    print $xbus->name." (".$xbus->label .", ". $xbus->connector .")\n";
    for my $xpd ($xbus->xpds) {
      print " - ".$xpd->fqn,"\n";
    }
  }
=cut


my $proc_base = "/proc/xpp";

# Nominal sorters for xbuses
sub by_name {
	return $a->name cmp $b->name;
}

sub by_connector {
	return $a->connector cmp $b->connector;
}

sub by_label {
	my $cmp = $a->label cmp $b->label;
	return $cmp if $cmp != 0;
	return $a->connector cmp $b->connector;
}

=head1 xbuses([sort_order])

Scans system (/proc and /sys) and returns a list of Astribank (Xbus) 
objects. The optional parameter sort_order is the order in which 
the Astribanks will be returns:

=over

=item SORT_CONNECTOR

Sort by the connector string. For USB this defines the "path" to get to
the device through controllers, hubs etc.

=item SORT_LABEL

Sorts by the label of the Astribank. The label field is unique to the
Astribank. It can also be viewed through 'lsusb -v' without the drivers
loaded (the iSerial field in the Device Descriptor). 

=item SORT_NAME

Sort by the "name". e.g: "XBUS-00". The order of Astribank names depends
on the load order, and hence may change between different runs.

=item custom function

Instead of using a predefined sorter, you can pass your own sorting
function. See the example sorters in the code of this module.

=back

=cut

sub xbuses {
	my $optsort = shift || 'SORT_CONNECTOR';
	my @xbuses;

	-d "$proc_base" or return ();
	my @lines;
	local $/ = "\n";
	open(F, "$proc_base/xbuses") ||
		die "$0: Failed to open $proc_base/xbuses: $!\n";
	@lines = <F>;
	close F;
	foreach my $line (@lines) {
		chomp $line;
		my ($name, @attr) = split(/\s+/, $line);
		$name =~ s/://;
		$name =~ /XBUS-(\d\d)/ or die "Bad XBUS number: $name";
		my $num = $1;
		@attr = map { split(/=/); } @attr;
		my $xbus = Zaptel::Xpp::Xbus->new(NAME => $name, NUM => $num, @attr);
		push(@xbuses, $xbus);
	}
	my $sorter;
	if($optsort eq "SORT_CONNECTOR") {
		$sorter = \&by_connector;
	} elsif($optsort eq "SORT_NAME") {
		$sorter = \&by_name;
	} elsif($optsort eq "SORT_LABEL") {
		$sorter = \&by_label;
	} elsif(ref($optsort) eq 'CODE') {
		$sorter = $optsort;
	} else {
		die "Unknown optional sorter '$optsort'";
	}
	@xbuses = sort $sorter @xbuses;
	return @xbuses;
}

sub xpd_of_span($) {
	my $span = shift or die "Missing span parameter";
	return undef unless defined $span;
	foreach my $xbus (Zaptel::Xpp::xbuses('SORT_CONNECTOR')) {
		foreach my $xpd ($xbus->xpds()) {
			return $xpd if $xpd->fqn eq $span->name;
		}
	}
	return undef;
}

=head1 sync([new_sync_source])

Gets (and optionally sets) the internal Astribanks synchronization
source. When used to set sync source, returns the original sync source.

A synchronization source is a value valid writing into /proc/xpp/sync .
For more information read that file and see README.Astribank .

=cut

sub sync {
	my $newsync = shift;
	my $result;
	my $newapi = 0;

	my $file = "$proc_base/sync";
	return '' unless -f $file;
	# First query
	open(F, "$file") or die "Failed to open $file for reading: $!";
	while(<F>) {
		chomp;
		/SYNC=/ and $newapi = 1;
		s/#.*//;
		if(/\S/) {	# First non-comment line
			s/^SYNC=\D*// if $newapi;
			$result = $_;
			last;
		}
	}
	close F;
	if(defined($newsync)) {		# Now change
		$newsync =~ s/.*/\U$&/;
		if($newsync =~ /^(\d+)$/) {
			$newsync = ($newapi)? "SYNC=$1" : "$1 0";
		} elsif($newsync ne 'ZAPTEL') {
			die "Bad sync parameter '$newsync'";
		}
		open(F, ">$file") or die "Failed to open $file for writing: $!";
		print F $newsync;
		close(F) or die "Failed in closing $file: $!";
	}
	return $result;
}

=head1 SEE ALSO

=over

=item L<Zaptel::Xpp::Xbus>

Xbus (Astribank) object.

=item L<Zaptel::Xpp::Xpd>

XPD (the rough equivalent of a Zaptel span) object.

=item L<Zaptel::Xpp::Line>

Object for a line: an analog port or a time-slot in a adapter. 
Equivalent of a channel in Zaptel.

=item L<Zaptel>

General documentation in the master package.

=back

=cut

1;
