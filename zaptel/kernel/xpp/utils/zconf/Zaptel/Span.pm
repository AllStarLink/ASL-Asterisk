package Zaptel::Span;
#
# Written by Oron Peled <oron@actcom.co.il>
# Copyright (C) 2007, Xorcom
# This program is free software; you can redistribute and/or
# modify it under the same terms as Perl itself.
#
# $Id: Span.pm 4309 2008-05-20 14:54:58Z tzafrir $
#
use strict;
use Zaptel::Utils;
use Zaptel::Chans;
use Zaptel::Xpp::Xpd;

=head1 NAME

Zaptel::Spans - Perl interface to a Zaptel span information

This package allows access from perl to information about a Zaptel
channel. It is part of the Zaptel Perl package.

A span is a logical unit of Zaptel channels. Normally a port in a
digital card or a whole analog card.

See documentation of module L<Zaptel> for usage example. Specifically
C<Zaptel::spans()> must be run initially.

=head1 by_number()

Get a span by its Zaptel span number.

=head1 Span Properties

=head2 num()

The span number.

=head2 name()

The name field of a Zaptel span. E.g.:

  TE2/0/1

=head2 description()

The description field of the span. e.g:

  "T2XXP (PCI) Card 0 Span 1" HDB3/CCS/CRC4 RED

=head2 chans()

The list of the channels (L<Zaptel::Chan> objects) of this span.
In a scalar context returns the number of channels this span has.

=head2 bchans()

Likewise a list of bchannels (or a count in a scalar context).

=head2 is_sync_master()

Is this span the source of timing for Zaptel?

=head2 type()

Type of span, or "UNKNOWN" if could not be detected. Current known
types: 

BRI_TE, BRI_NT, E1_TE, E1_NT, J1_TE, J1_NT, T1_TE, T1_NT, FXS, FXO

=head2 is_pri()

Is this an E1/J1/T1 span?

=head2 is_bri()

Is this a BRI span?

=head2 is_digital()

Is this a digital (as opposed to analog) span?

=head2 termtype()

Set for digital spans. "TE" or "NT". Will probably be assumed to be "TE"
if there's no information pointing either way.

=head2 coding()

Suggested sane coding type (e.g.: "hdb3", "b8zs") for this type of span. 

=head2 framing()

Suggested sane framing type (e.g.: "ccs", "esf") for this type of span. 

=head2 yellow(), crc4()

Likewise, suggestions ofr the respective fields in the span= line in
zaptel.conf for this span.

=head2 signalling()

Suggested zapata.conf signalling for channels of this span.

=head2 switchtype()

Suggested zapata.conf switchtype for channels of this span.

=head1 Note

Most of those properties are normally used as lower-case functions, but
actually set in the module as capital-letter propeties. To look at e.g.
"signalling" is set, look for "SIGNALLING".

=cut

my $proc_base = "/proc/zaptel";

sub chans($) {
	my $span = shift;
	return @{$span->{CHANS}};
}

sub by_number($) {
	my $span_number = shift;
	die "Missing span number" unless defined $span_number;
	my @spans = Zaptel::spans();

	my ($span) = grep { $_->num == $span_number } @spans;
	return $span;
}

my @bri_strings = (
		'BRI_(NT|TE)',
		'(?:quad|octo)BRI PCI ISDN Card.* \[(NT|TE)\]\ ',
		'octoBRI \[(NT|TE)\] ',
		'HFC-S PCI A ISDN.* \[(NT|TE)\] '
		);

my @pri_strings = (
		'(E1|T1|J1)_(NT|TE)',
		'Tormenta 2 .*Quad (E1|T1)',       # tor2.
		'Digium Wildcard .100P (T1|E1)/', # wct1xxp
		'ISA Tormenta Span 1',	           # torisa
		'TE110P T1/E1',                    # wcte11xp
		'Wildcard TE120P',                 # wcte12xp
		'Wildcard TE121',                  # wcte12xp
		'Wildcard TE122',                  # wcte12xp
		'T[24]XXP \(PCI\) Card ',          # wct4xxp
		);

our $ZAPBRI_NET = 'bri_net';
our $ZAPBRI_CPE = 'bri_cpe';

our $ZAPPRI_NET = 'pri_net';
our $ZAPPRI_CPE = 'pri_cpe';

sub init_proto($$) {
	my $self = shift;
	my $proto = shift;

	$self->{PROTO} = $proto;
	if($proto eq 'E1') {
		$self->{DCHAN_IDX} = 15;
		$self->{BCHAN_LIST} = [ 0 .. 14, 16 .. 30 ];
	} elsif($proto eq 'T1') {
		$self->{DCHAN_IDX} = 23;
		$self->{BCHAN_LIST} = [ 0 .. 22 ];
	}
	$self->{TYPE} = "${proto}_$self->{TERMTYPE}";
}

sub new($$) {
	my $pack = shift or die "Wasn't called as a class method\n";
	my $num = shift or die "Missing a span number parameter\n";
	my $self = { NUM => $num };
	bless $self, $pack;
	$self->{TYPE} = "UNKNOWN";
	my @xpds = Zaptel::Xpp::Xpd::xpds_by_spanno;
	my $xpd = $xpds[$num];
	if(defined $xpd) {
		die "Spanno mismatch: $xpd->spanno, $num" unless $xpd->spanno == $num;
		$self->{XPD} = $xpd;
	}
	open(F, "$proc_base/$num") or die "Failed to open '$proc_base/$num\n";
	my $head = <F>;
	chomp $head;
	$self->{IS_DIGITAL} = 0;
	$self->{IS_BRI} = 0;
	$self->{IS_PRI} = 0;
	foreach my $cardtype (@bri_strings) {
		if($head =~ m/$cardtype/) {
			$self->{IS_DIGITAL} = 1;
			$self->{IS_BRI} = 1;
			$self->{TERMTYPE} = $1;
			$self->{TYPE} = "BRI_$1";
			$self->{DCHAN_IDX} = 2;
			$self->{BCHAN_LIST} = [ 0, 1 ];
			last;
		}
	}
	foreach my $cardtype (@pri_strings) {
		if($head =~ m/$cardtype/) {
			my @info;

			push(@info, $1) if defined $1;
			push(@info, $2) if defined $2;
			my ($proto) = grep(/(E1|T1|J1)/, @info);
			$proto = 'UNKNOWN' unless defined $proto;
			my ($termtype) = grep(/(NT|TE)/, @info);
			$termtype = 'TE' unless defined $termtype;

			$self->{IS_DIGITAL} = 1;
			$self->{IS_PRI} = 1;
			$self->{TERMTYPE} = $termtype;
			$self->init_proto($proto);
			last;
		}
	}
	die "$0: Unkown TERMTYPE [NT/TE]\n"
		if $self->is_digital  and !defined $self->{TERMTYPE};
	($self->{NAME}, $self->{DESCRIPTION}) = (split(/\s+/, $head, 4))[2, 3];
	$self->{IS_ZAPTEL_SYNC_MASTER} =
		($self->{DESCRIPTION} =~ /\(MASTER\)/) ? 1 : 0;
	$self->{CHANS} = [];
	my @channels;
	my $index = 0;
	while(<F>) {
		chomp;
		s/^\s*//;
		s/\s*$//;
		next unless /\S/;
		next unless /^\s*\d+/; # must be a real channel string.
		my $c = Zaptel::Chans->new($self, $index, $_);
		push(@channels, $c);
		$index++;
	}
	close F;
	if($self->is_pri()) {
		# Check for PRI with unknown type strings
		if($index == 31) {
			if($self->{PROTO} eq 'UNKNOWN') {
				$self->init_proto('E1');
			} elsif($self->{PROTO} ne 'E1')  {
				die "$index channels in a $self->{PROTO} span";
			}
		} elsif($index == 24) {
			if($self->{PROTO} eq 'UNKNOWN') {
				$self->init_proto('T1');	# FIXME: J1?
			} elsif($self->{PROTO} ne 'T1') {
				die "$index channels in a $self->{PROTO} span";
			}
		}
	}
	@channels = sort { $a->num <=> $b->num } @channels;
	$self->{CHANS} = \@channels;
	$self->{YELLOW} = undef;
	$self->{CRC4} = undef;
	if($self->is_bri()) {
		$self->{CODING} = 'ami';
		$self->{DCHAN} = ($self->chans())[$self->{DCHAN_IDX}];
		$self->{BCHANS} = [ ($self->chans())[@{$self->{BCHAN_LIST}}] ];
		# Infer some info from channel name:
		my $first_chan = ($self->chans())[0] || die "$0: No channels in span #$num\n";
		my $chan_fqn = $first_chan->fqn();
		if($chan_fqn =~ m(ZTHFC.*/|ztqoz.*/|XPP_BRI_.*/)) {		# BRI
			$self->{FRAMING} = 'ccs';
			$self->{SWITCHTYPE} = 'euroisdn';
			$self->{SIGNALLING} = ($self->{TERMTYPE} eq 'NT') ? $ZAPBRI_NET : $ZAPBRI_CPE ;
		} elsif($chan_fqn =~ m(ztgsm.*/)) {				# Junghanns's GSM cards. 
			$self->{FRAMING} = 'ccs';
			$self->{SIGNALLING} = 'gsm';
		}
	}
	if($self->is_pri()) {
		$self->{DCHAN} = ($self->chans())[$self->{DCHAN_IDX}];
		$self->{BCHANS} = [ ($self->chans())[@{$self->{BCHAN_LIST}}] ];
		if($self->{PROTO} eq 'E1') {
			$self->{CODING} = 'hdb3';
			$self->{FRAMING} = 'ccs';
			$self->{SWITCHTYPE} = 'euroisdn';
			$self->{CRC4} = 'crc4';
		} elsif($self->{PROTO} eq 'T1') {
			$self->{CODING} = 'b8zs';
			$self->{FRAMING} = 'esf';
			$self->{SWITCHTYPE} = 'national';
		} else {
			die "'$self->{PROTO}' unsupported yet";
		}
		$self->{SIGNALLING} = ($self->{TERMTYPE} eq 'NT') ? $ZAPPRI_NET : $ZAPPRI_CPE ;
	}
	return $self;
}

sub bchans($) {
	my $self = shift || die;

	return @{$self->{BCHANS}};
}

1;
