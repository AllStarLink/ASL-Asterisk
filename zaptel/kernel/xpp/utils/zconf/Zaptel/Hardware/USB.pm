package Zaptel::Hardware::USB;
#
# Written by Oron Peled <oron@actcom.co.il>
# Copyright (C) 2007, Xorcom
# This program is free software; you can redistribute and/or
# modify it under the same terms as Perl itself.
#
# $Id: USB.pm 3793 2008-02-04 23:00:48Z tzafrir $
#
use strict;
use Zaptel::Utils;
use Zaptel::Hardware;
use Zaptel::Xpp;
use Zaptel::Xpp::Xbus;

our @ISA = qw(Zaptel::Hardware);

my %usb_ids = (
	# from wcusb
	'06e6:831c'	=> { DRIVER => 'wcusb', DESCRIPTION => 'Wildcard S100U USB FXS Interface' },
	'06e6:831e'	=> { DRIVER => 'wcusb2', DESCRIPTION => 'Wildcard S110U USB FXS Interface' },
	'06e6:b210'	=> { DRIVER => 'wc_usb_phone', DESCRIPTION => 'Wildcard Phone Test driver' },

	# from xpp_usb
	'e4e4:1130'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-8/16 no-firmware' },
	'e4e4:1131'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-8/16 USB-firmware' },
	'e4e4:1132'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-8/16 FPGA-firmware' },
	'e4e4:1140'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-BRI no-firmware' },
	'e4e4:1141'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-BRI USB-firmware' },
	'e4e4:1142'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-BRI FPGA-firmware' },
	'e4e4:1150'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-multi no-firmware' },
	'e4e4:1151'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-multi USB-firmware' },
	'e4e4:1152'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-multi FPGA-firmware' },
	'e4e4:1160'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-modular no-firmware' },
	'e4e4:1161'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-modular USB-firmware' },
	'e4e4:1162'	=> { DRIVER => 'xpp_usb', DESCRIPTION => 'Astribank-modular FPGA-firmware' },
	);


$ENV{PATH} .= ":/usr/sbin:/sbin:/usr/bin:/bin";

my @xbuses = Zaptel::Xpp::xbuses('SORT_CONNECTOR');

sub usb_sorter() {
	return $a->hardware_name cmp $b->hardware_name;
}

sub xbus_of_usb($) {
	my $priv_device_name = shift;
	my $dev = shift;

	my ($wanted) = grep {
			defined($_->usb_devname) &&
			$priv_device_name eq $_->usb_devname
		} @xbuses;
	return $wanted;
}

sub new($$) {
	my $pack = shift or die "Wasn't called as a class method\n";
	my $self = { @_ };
	bless $self, $pack;
	my $xbus = xbus_of_usb($self->priv_device_name);
	if(defined $xbus) {
		$self->{XBUS} = $xbus;
		$self->{LOADED} = 'xpp_usb';
	} else {
		$self->{XBUS} = undef;
		$self->{LOADED} = undef;
	}
	Zaptel::Hardware::device_detected($self,
		sprintf("usb:%s", $self->{PRIV_DEVICE_NAME}));
	return $self;
}

sub devices($) {
	my $pack = shift || die;
	my $usb_device_list = "/proc/bus/usb/devices";
	return unless (-r $usb_device_list);

	my @devices;
	open(F, $usb_device_list) || die "Failed to open $usb_device_list: $!";
	local $/ = '';
	while(<F>) {
		my @lines = split(/\n/);
		my ($tline) = grep(/^T/, @lines);
		my ($pline) = grep(/^P/, @lines);
		my ($sline) = grep(/^S:.*SerialNumber=/, @lines);
		my ($busnum,$devnum) = ($tline =~ /Bus=(\w+)\W.*Dev#=\s*(\w+)\W/);
		my $devname = sprintf("%03d/%03d", $busnum, $devnum);
		my ($vendor,$product) = ($pline =~ /Vendor=(\w+)\W.*ProdID=(\w+)\W/);
		my $serial;
		if(defined $sline) {
			$sline =~ /SerialNumber=(.*)/;
			$serial = $1;
			#$serial =~ s/[[:^print:]]/_/g;
		}
		my $model = $usb_ids{"$vendor:$product"};
		next unless defined $model;
		my $d = Zaptel::Hardware::USB->new(
			IS_ASTRIBANK		=> ($model->{DRIVER} eq 'xpp_usb')?1:0,
			BUS_TYPE		=> 'USB',
			PRIV_DEVICE_NAME	=> $devname,
			VENDOR			=> $vendor,
			PRODUCT			=> $product,
			SERIAL			=> $serial,
			DESCRIPTION		=> $model->{DESCRIPTION},
			DRIVER			=> $model->{DRIVER},
			);
		push(@devices, $d);
	}
	close F;
	@devices = sort usb_sorter @devices;
}

1;
