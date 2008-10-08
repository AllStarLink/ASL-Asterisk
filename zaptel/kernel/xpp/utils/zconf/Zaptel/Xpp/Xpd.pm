package Zaptel::Xpp::Xpd;
#
# Written by Oron Peled <oron@actcom.co.il>
# Copyright (C) 2007, Xorcom
# This program is free software; you can redistribute and/or
# modify it under the same terms as Perl itself.
#
# $Id: Xpd.pm 4266 2008-05-13 21:08:09Z tzafrir $
#
use strict;
use Zaptel::Utils;
use Zaptel::Xpp;
use Zaptel::Xpp::Line;

my $proc_base = "/proc/xpp";

sub blink($$) {
	my $self = shift;
	my $on = shift;
	my $result;

	my $file = "$proc_base/" . $self->fqn . "/blink";
	die "$file is missing" unless -f $file;
	# First query
	open(F, "$file") or die "Failed to open $file for reading: $!";
	$result = <F>;
	chomp $result;
	close F;
	if(defined($on) and $on ne $result) {		# Now change
		open(F, ">$file") or die "Failed to open $file for writing: $!";
		print F ($on)?"0xFFFF":"0";
		if(!close(F)) {
			if($! == 17) {	# EEXISTS
				# good
			} else {
				undef $result;
			}
		}
	}
	return $result;
}

sub zt_registration($$) {
	my $self = shift;
	my $on = shift;
	my $result;

	my $file = "$proc_base/" . $self->fqn . "/zt_registration";
	die "$file is missing" unless -f $file;
	# First query
	open(F, "$file") or die "Failed to open $file for reading: $!";
	$result = <F>;
	chomp $result;
	close F;
	if(defined($on) and $on ne $result) {		# Now change
		open(F, ">$file") or die "Failed to open $file for writing: $!";
		print F ($on)?"1":"0";
		if(!close(F)) {
			if($! == 17) {	# EEXISTS
				# good
			} else {
				undef $result;
			}
		}
	}
	return $result;
}

sub xpds_by_spanno() {
	my @xbuses = Zaptel::Xpp::xbuses("SORT_CONNECTOR");
	my @xpds = map { $_->xpds } @xbuses;
	@xpds = grep { $_->spanno } @xpds;
	@xpds = sort { $a->spanno <=> $b->spanno } @xpds;
	my @spanno = map { $_->spanno } @xpds;
	my @idx;
	@idx[@spanno] = @xpds;	# The spanno is the index now
	return @idx;
}

sub new($$) {
	my $pack = shift or die "Wasn't called as a class method\n";
	my $xbus = shift || die;
	my $procdir = shift || die;
	my $self = {};
	bless $self, $pack;
	$self->{XBUS} = $xbus;
	$self->{DIR} = $procdir;
	local $/ = "\n";
	open(F, "$procdir/summary") || die "Missing summary file in $procdir";
	my $head = <F>;
	chomp $head;	# "XPD-00 (BRI_TE ,card present, span 3)"
	# The driver does not export the number of channels...
	# Let's find it indirectly
	while(<F>) {
		chomp;
		if(s/^\s*offhook\s*:\s*//) {
			my @offhook = split;
			@offhook || die "No channels in '$procdir/summary'";
			$self->{CHANNELS} = @offhook;
			last;
		}
	}
	close F;
	$head =~ s/^(XPD-(\d\d))\s+// || die;
	$self->{ID} = $2;
	$self->{FQN} = $xbus->name . "/" . $1;
	$head =~ s/^.*\(// || die;
	$head =~ s/\) */, / || die;
	$head =~ s/\s*,\s*/,/g || die;
	my ($type,$present,$span,$rest) = split(/,/, $head);
	#warn "Garbage in '$procdir/summary': rest='$rest'\n" if $rest;
	if($span =~ s/span\s+(\d+)//) {	# since changeset:5119
		$self->{SPANNO} = $1;
	}
	$self->{TYPE} = $type;
	$self->{IS_BRI} = ($type =~ /BRI_(NT|TE)/);
	$self->{IS_PRI} = ($type =~ /[ETJ]1_(NT|TE)/);
	$self->{IS_DIGITAL} = ( $self->{IS_BRI} || $self->{IS_PRI} );
	Zaptel::Xpp::Line->create_all($self, $procdir);
	return $self;
}

1;
