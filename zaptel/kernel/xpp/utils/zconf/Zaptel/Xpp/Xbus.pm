package Zaptel::Xpp::Xbus;
#
# Written by Oron Peled <oron@actcom.co.il>
# Copyright (C) 2007, Xorcom
# This program is free software; you can redistribute and/or
# modify it under the same terms as Perl itself.
#
# $Id: Xbus.pm 4266 2008-05-13 21:08:09Z tzafrir $
#
use strict;
use Zaptel::Utils;
use Zaptel::Xpp::Xpd;

my $proc_base = "/proc/xpp";

sub xpds($) {
	my $xbus = shift;
	return @{$xbus->{XPDS}};
}

sub by_number($) {
	my $busnumber = shift;
	die "Missing xbus number parameter" unless defined $busnumber;
	my @xbuses = Zaptel::Xpp::xbuses();

	my ($xbus) = grep { $_->num == $busnumber } @xbuses;
	return $xbus;
}

sub by_label($) {
	my $label = shift;
	die "Missing xbus label parameter" unless defined $label;
	my @xbuses = Zaptel::Xpp::xbuses();

	my ($xbus) = grep { $_->label eq $label } @xbuses;
	return $xbus;
}

sub get_xpd_by_number($$) {
	my $xbus = shift;
	my $xpdid = shift;
	die "Missing XPD id parameter" unless defined $xpdid;
	my @xpds = $xbus->xpds;
	my ($wanted) = grep { $_->id eq $xpdid } @xpds;
	return $wanted;
}

sub new($$) {
	my $pack = shift or die "Wasn't called as a class method\n";
	my $self = {};
	bless $self, $pack;
	while(@_) {
		my ($k, $v) = @_;
		shift; shift;
		# Keys in all caps
		$k = uc($k);
		# Some values are in all caps as well
		if($k =~ /^(STATUS)$/) {
			$v = uc($v);
		}
		$self->{$k} = $v;
	}
	# backward compat for drivers without labels.
	if(!defined $self->{LABEL}) {
		$self->{LABEL} = '[]';
	}
	$self->{LABEL} =~ s/^\[(.*)\]$/$1/ or die "$self->{NAME}: Bad label";
	# Fix badly burned labels.
	$self->{LABEL} =~ s/[[:^print:]]/_/g;
	$self->{NAME} or die "Missing xbus name";
	my $prefix = "$proc_base/" . $self->{NAME};
	my $usbfile = "$prefix/xpp_usb";
	if(open(F, "$usbfile")) {
		my $head = <F>;
		chomp $head;
		close F;
		$head =~ s/^device: +([^, ]+)/$1/i or die;
		$self->{USB_DEVNAME} = $head;
	}
	@{$self->{XPDS}} = ();
	foreach my $dir (glob "$prefix/XPD-??") {
		my $xpd = Zaptel::Xpp::Xpd->new($self, $dir);
		push(@{$self->{XPDS}}, $xpd);
	}
	@{$self->{XPDS}} = sort { $a->id <=> $b->id } @{$self->{XPDS}};
	return $self;
}

sub pretty_xpds($) {
		my $xbus = shift;
		my @xpds = sort { $a->id <=> $b->id } $xbus->xpds();
		my @xpd_types = map { $_->type } @xpds;
		my $last_type = '';
		my $mult = 0;
		my $xpdstr = '';
		foreach my $curr (@xpd_types) {
			if(!$last_type || ($curr eq $last_type)) {
				$mult++;
			} else {
				if($mult == 1) {
					$xpdstr .= "$last_type ";
				} elsif($mult) {
					$xpdstr .= "$last_type*$mult ";
				}
				$mult = 1;
			}
			$last_type = $curr;
		}
		if($mult == 1) {
			$xpdstr .= "$last_type ";
		} elsif($mult) {
			$xpdstr .= "$last_type*$mult ";
		}
		$xpdstr =~ s/\s*$//;	# trim trailing space
		return $xpdstr;
}

1;
