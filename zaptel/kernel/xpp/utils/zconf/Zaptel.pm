package Zaptel;
#
# Written by Oron Peled <oron@actcom.co.il>
# Copyright (C) 2007, Xorcom
# This program is free software; you can redistribute and/or
# modify it under the same terms as Perl itself.
#
# $Id: Zaptel.pm 4534 2008-09-09 18:24:17Z tzafrir $
#
use strict;
use Zaptel::Span;

=head1 NAME

Zaptel - Perl interface to Zaptel information

This package allows access from Perl to information about Zaptel
hardware and loaded Zaptel devices.

=head1 SYNOPSIS

  # Listing channels in analog spans:
  use Zaptel;
  # scans system:
  my @spans = Zaptel::spans();
  for my $span (@spans) {
    next if ($span->is_digital);
    $span->num. " - [". $span->type ."] ". $span->name. "\n";
    for my $chan ($span->chans) {
      print " - ".$chan->num . " - [". $chan->type. "] ". $chan->fqn". \n";
    }
  }
=cut

my $proc_base = "/proc/zaptel";

=head1 spans()

Returns a list of span objects, ordered by span number.

=cut

sub spans() {
	my @spans;

	-d $proc_base or return ();
	foreach my $zfile (glob "$proc_base/*") {
		$zfile =~ s:$proc_base/::;
		my $span = Zaptel::Span->new($zfile);
		push(@spans, $span);
	}
	@spans = sort { $a->num <=> $b->num } @spans;
	return @spans;
}

=head1 SEE ALSO

Span objects: L<Zaptel::Span>.

Zaptel channels objects: L<Zaptel::Chan>.

Zaptel hardware devices information: L<Zaptel::Hardware>.

Xorcom Astribank -specific information: L<Zaptel::Xpp>.

=cut

1;
