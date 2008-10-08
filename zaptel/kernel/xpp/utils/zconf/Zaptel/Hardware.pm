package Zaptel::Hardware;
#
# Written by Oron Peled <oron@actcom.co.il>
# Copyright (C) 2007, Xorcom
# This program is free software; you can redistribute and/or
# modify it under the same terms as Perl itself.
#
# $Id: Hardware.pm 4039 2008-03-21 01:51:39Z tzafrir $
#
use strict;
use Zaptel::Hardware::USB;
use Zaptel::Hardware::PCI;

=head1 NAME

Zaptel::Hardware - Perl interface to a Zaptel devices listing


  use Zaptel::Hardware;
  
  my $hardware = Zaptel::Hardware->scan; 
  
  # mini zaptel_hardware:
  foreach my $device ($hardware->device_list) {
    print "Vendor: device->{VENDOR}, Product: $device->{PRODUCT}\n"
  }

  # let's see if there are devices without loaded drivers, and sugggest
  # drivers to load:
  my @to_load = ();
  foreach my $device ($hardware->device_list) {
    if (! $device->{LOADED} ) {
      push @to_load, ($device->${DRIVER});
    }
  }
  if (@to_load) {
    print "To support the extra devices you probably need to run:\n"
    print "  modprobe ". (join ' ', @to_load). "\n";
  }


This module provides information about available Zaptel devices on the
system. It identifies devices by (USB/PCI) bus IDs.


=head1 Device Attributes
As usual, object attributes can be used in either upp-case or
lower-case, or lower-case functions.

=head2 bus_type

'PCI' or 'USB'.


=head2 description

A one-line description of the device.


=head2 driver

Name of a Zaptel device driver that should handle this device. This is
based on a pre-made list.


=head2 vendor, product, subvendor, subproduct

The PCI and USB vendor ID, product ID, sub-vendor ID and sub-product ID.
(The standard short lspci and lsusb listings show only vendor and
product IDs).


=head2 loaded

If the device is handled by a module - the name of the module. Else -
undef.


=head2 priv_device_name

A string that shows the "location" of that device on the bus.


=head2 is_astribank

True if the device is a Xorcom Astribank (which may provide some extra
attributes).

=head2 serial

(Astribank-specific attrribute) - the serial number string of the
Astribank.

=cut

sub device_detected($$) {
	my $dev = shift || die;
	my $name =  shift || die;
	die unless defined $dev->{'BUS_TYPE'};
	$dev->{IS_ASTRIBANK} = 0 unless defined $dev->{'IS_ASTRIBANK'};
	$dev->{'HARDWARE_NAME'} = $name;
}

sub device_removed($) {
	my $dev = shift || die;
	my $name = $dev->hardware_name;
	die "Missing zaptel device hardware name" unless $name;
}


=head1 device_list()

Returns a list of the hardware devices on the system.

You must run scan() first for this function to run meaningful output.

=cut

sub device_list($) {
	my $self = shift || die;
	my @types = @_;
	my @list;

	@types = qw(USB PCI) unless @types;
	foreach my $t (@types) {
		@list = ( @list, @{$self->{$t}} );
	}
	return @list;
}


=head1 drivers()

Returns a list of drivers (currently sorted by name) that are used by
the devices in the current system (regardless to whether or not they are
loaded.

=cut

sub drivers($) {
	my $self = shift || die;
	my @devs = $self->device_list;
	my @drvs = map { $_->{DRIVER} } @devs;
	# Make unique
	my %drivers;
	@drivers{@drvs} = 1;
	return sort keys %drivers;
}


=head1 scan()

Scan the system for Zaptel devices (PCI and USB). Returns nothing but
must be run to initialize the module.

=cut

sub scan($) {
	my $pack = shift || die;
	my $self = {};
	bless $self, $pack;

	$self->{USB} = [ Zaptel::Hardware::USB->devices ];
	$self->{PCI} = [ Zaptel::Hardware::PCI->scan_devices ];
	return $self;
}

1;
