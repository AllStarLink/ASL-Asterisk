/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * UDPTL support for T.38
 * 
 * Copyright (C) 2005, Steve Underwood, partly based on RTP code which is
 * Copyright (C) 1999-2006, Digium, Inc.
 *
 * Steve Underwood <steveu@coppice.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 209131 $")

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/time.h>
#include <sys/socket.h>
#include <arpa/inet.h>
#include <fcntl.h>

#include "asterisk/udptl.h"
#include "asterisk/frame.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/channel.h"
#include "asterisk/acl.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/lock.h"
#include "asterisk/utils.h"
#include "asterisk/cli.h"
#include "asterisk/unaligned.h"
#include "asterisk/utils.h"

#define UDPTL_MTU		1200

#if !defined(FALSE)
#define FALSE 0
#endif
#if !defined(TRUE)
#define TRUE (!FALSE)
#endif

static int udptlstart;
static int udptlend;
static int udptldebug;	                  /* Are we debugging? */
static struct sockaddr_in udptldebugaddr;   /* Debug packets to/from this host */
#ifdef SO_NO_CHECK
static int nochecksums;
#endif
static int udptlfectype;
static int udptlfecentries;
static int udptlfecspan;
static int udptlmaxdatagram;
static int use_even_ports;

#define LOCAL_FAX_MAX_DATAGRAM      1400
#define MAX_FEC_ENTRIES             5
#define MAX_FEC_SPAN                5

#define UDPTL_BUF_MASK              15

typedef struct {
	int buf_len;
	uint8_t buf[LOCAL_FAX_MAX_DATAGRAM];
} udptl_fec_tx_buffer_t;

typedef struct {
	int buf_len;
	uint8_t buf[LOCAL_FAX_MAX_DATAGRAM];
	int fec_len[MAX_FEC_ENTRIES];
	uint8_t fec[MAX_FEC_ENTRIES][LOCAL_FAX_MAX_DATAGRAM];
	int fec_span;
	int fec_entries;
} udptl_fec_rx_buffer_t;

struct ast_udptl {
	int fd;
	char resp;
	struct ast_frame f[16];
	unsigned char rawdata[8192 + AST_FRIENDLY_OFFSET];
	unsigned int lasteventseqn;
	int nat;
	int flags;
	struct sockaddr_in us;
	struct sockaddr_in them;
	int *ioid;
	struct sched_context *sched;
	struct io_context *io;
	void *data;
	ast_udptl_callback callback;
	int udptl_offered_from_local;

	/*! This option indicates the error correction scheme used in transmitted UDPTL
	    packets. */
	int error_correction_scheme;

	/*! This option indicates the number of error correction entries transmitted in
	    UDPTL packets. */
	int error_correction_entries;

	/*! This option indicates the span of the error correction entries in transmitted
	    UDPTL packets (FEC only). */
	int error_correction_span;

	/*! This option indicates the maximum size of a UDPTL packet that can be accepted by
	    the remote device. */
	int far_max_datagram_size;

	/*! This option indicates the maximum size of a UDPTL packet that we are prepared to
	    accept. */
	int local_max_datagram_size;

	int verbose;

	struct sockaddr_in far;

	int tx_seq_no;
	int rx_seq_no;
	int rx_expected_seq_no;

	udptl_fec_tx_buffer_t tx[UDPTL_BUF_MASK + 1];
	udptl_fec_rx_buffer_t rx[UDPTL_BUF_MASK + 1];
};

static struct ast_udptl_protocol *protos;

static int udptl_rx_packet(struct ast_udptl *s, uint8_t *buf, int len);
static int udptl_build_packet(struct ast_udptl *s, uint8_t *buf, int buflen, uint8_t *ifp, int ifp_len);

static inline int udptl_debug_test_addr(struct sockaddr_in *addr)
{
	if (udptldebug == 0)
		return 0;
	if (udptldebugaddr.sin_addr.s_addr) {
		if (((ntohs(udptldebugaddr.sin_port) != 0)
			&& (udptldebugaddr.sin_port != addr->sin_port))
			|| (udptldebugaddr.sin_addr.s_addr != addr->sin_addr.s_addr))
			return 0;
	}
	return 1;
}

static int decode_length(uint8_t *buf, int limit, int *len, int *pvalue)
{
	if (*len >= limit)
		return -1;
	if ((buf[*len] & 0x80) == 0) {
		*pvalue = buf[*len];
		(*len)++;
		return 0;
	}
	if ((buf[*len] & 0x40) == 0) {
		if (*len == limit - 1)
			return -1;
		*pvalue = (buf[*len] & 0x3F) << 8;
		(*len)++;
		*pvalue |= buf[*len];
		(*len)++;
		return 0;
	}
	*pvalue = (buf[*len] & 0x3F) << 14;
	(*len)++;
	/* Indicate we have a fragment */
	return 1;
}
/*- End of function --------------------------------------------------------*/

static int decode_open_type(uint8_t *buf, int limit, int *len, const uint8_t **p_object, int *p_num_octets)
{
	int octet_cnt;
	int octet_idx;
	int stat;
	int i;
	const uint8_t **pbuf;

	for (octet_idx = 0, *p_num_octets = 0; ; octet_idx += octet_cnt) {
		if ((stat = decode_length(buf, limit, len, &octet_cnt)) < 0)
			return -1;
		if (octet_cnt > 0) {
			*p_num_octets += octet_cnt;

			pbuf = &p_object[octet_idx];
			i = 0;
			/* Make sure the buffer contains at least the number of bits requested */
			if ((*len + octet_cnt) > limit)
				return -1;

			*pbuf = &buf[*len];
			*len += octet_cnt;
		}
		if (stat == 0)
			break;
	}
	return 0;
}
/*- End of function --------------------------------------------------------*/

static int encode_length(uint8_t *buf, int *len, int value)
{
	int multiplier;

	if (value < 0x80) {
		/* 1 octet */
		buf[*len] = value;
		(*len)++;
		return value;
	}
	if (value < 0x4000) {
		/* 2 octets */
		/* Set the first bit of the first octet */
		buf[*len] = ((0x8000 | value) >> 8) & 0xFF;
		(*len)++;
		buf[*len] = value & 0xFF;
		(*len)++;
		return value;
	}
	/* Fragmentation */
	multiplier = (value < 0x10000) ? (value >> 14) : 4;
	/* Set the first 2 bits of the octet */
	buf[*len] = 0xC0 | multiplier;
	(*len)++;
	return multiplier << 14;
}
/*- End of function --------------------------------------------------------*/

static int encode_open_type(uint8_t *buf, int buflen, int *len, const uint8_t *data, int num_octets)
{
	int enclen;
	int octet_idx;
	uint8_t zero_byte;

	/* If open type is of zero length, add a single zero byte (10.1) */
	if (num_octets == 0) {
		zero_byte = 0;
		data = &zero_byte;
		num_octets = 1;
	}
	/* Encode the open type */
	for (octet_idx = 0; ; num_octets -= enclen, octet_idx += enclen) {
		if ((enclen = encode_length(buf, len, num_octets)) < 0)
			return -1;
		if (enclen + *len > buflen) {
			ast_log(LOG_ERROR, "Buffer overflow detected (%d + %d > %d)\n", enclen, *len, buflen);
			return -1;
		}
		if (enclen > 0) {
			memcpy(&buf[*len], &data[octet_idx], enclen);
			*len += enclen;
		}
		if (enclen >= num_octets)
			break;
	}

	return 0;
}
/*- End of function --------------------------------------------------------*/

static int udptl_rx_packet(struct ast_udptl *s, uint8_t *buf, int len)
{
	int stat;
	int stat2;
	int i;
	int j;
	int k;
	int l;
	int m;
	int x;
	int limit;
	int which;
	int ptr;
	int count;
	int total_count;
	int seq_no;
	const uint8_t *ifp;
	const uint8_t *data;
	int ifp_len;
	int repaired[16];
	const uint8_t *bufs[16];
	int lengths[16];
	int span;
	int entries;
	int ifp_no;

	ptr = 0;
	ifp_no = 0;
	memset(&s->f[0], 0, sizeof(s->f[0]));

	/* Decode seq_number */
	if (ptr + 2 > len)
		return -1;
	seq_no = (buf[0] << 8) | buf[1];
	ptr += 2;

	/* Break out the primary packet */
	if ((stat = decode_open_type(buf, len, &ptr, &ifp, &ifp_len)) != 0)
		return -1;
	/* Decode error_recovery */
	if (ptr + 1 > len)
		return -1;
	if ((buf[ptr++] & 0x80) == 0) {
		/* Secondary packet mode for error recovery */
		if (seq_no > s->rx_seq_no) {
			/* We received a later packet than we expected, so we need to check if we can fill in the gap from the
			   secondary packets. */
			total_count = 0;
			do {
				if ((stat2 = decode_length(buf, len, &ptr, &count)) < 0)
					return -1;
				for (i = 0; i < count; i++) {
					if ((stat = decode_open_type(buf, len, &ptr, &bufs[total_count + i], &lengths[total_count + i])) != 0)
						return -1;
				}
				total_count += count;
			}
			while (stat2 > 0);
			/* Step through in reverse order, so we go oldest to newest */
			for (i = total_count; i > 0; i--) {
				if (seq_no - i >= s->rx_seq_no) {
					/* This one wasn't seen before */
					/* Decode the secondary IFP packet */
					//fprintf(stderr, "Secondary %d, len %d\n", seq_no - i, lengths[i - 1]);
					s->f[ifp_no].frametype = AST_FRAME_MODEM;
					s->f[ifp_no].subclass = AST_MODEM_T38;

					s->f[ifp_no].mallocd = 0;
					s->f[ifp_no].seqno = seq_no - i;
					s->f[ifp_no].datalen = lengths[i - 1];
					s->f[ifp_no].data = (uint8_t *) bufs[i - 1];
					s->f[ifp_no].offset = 0;
					s->f[ifp_no].src = "UDPTL";
					if (ifp_no > 0)
						AST_LIST_NEXT(&s->f[ifp_no - 1], frame_list) = &s->f[ifp_no];
					AST_LIST_NEXT(&s->f[ifp_no], frame_list) = NULL;
					ifp_no++;
				}
			}
		}
	}
	else
	{
		/* FEC mode for error recovery */
		/* Our buffers cannot tolerate overlength IFP packets in FEC mode */
		if (ifp_len > LOCAL_FAX_MAX_DATAGRAM)
			return -1;
		/* Update any missed slots in the buffer */
		for ( ; seq_no > s->rx_seq_no; s->rx_seq_no++) {
			x = s->rx_seq_no & UDPTL_BUF_MASK;
			s->rx[x].buf_len = -1;
			s->rx[x].fec_len[0] = 0;
			s->rx[x].fec_span = 0;
			s->rx[x].fec_entries = 0;
		}

		x = seq_no & UDPTL_BUF_MASK;

		memset(repaired, 0, sizeof(repaired));

		/* Save the new IFP packet */
		memcpy(s->rx[x].buf, ifp, ifp_len);
		s->rx[x].buf_len = ifp_len;
		repaired[x] = TRUE;

		/* Decode the FEC packets */
		/* The span is defined as an unconstrained integer, but will never be more
		   than a small value. */
		if (ptr + 2 > len)
			return -1;
		if (buf[ptr++] != 1)
			return -1;
		span = buf[ptr++];
		s->rx[x].fec_span = span;

		/* The number of entries is defined as a length, but will only ever be a small
		   value. Treat it as such. */
		if (ptr + 1 > len)
			return -1;
		entries = buf[ptr++];
		s->rx[x].fec_entries = entries;

		/* Decode the elements */
		for (i = 0; i < entries; i++) {
			if ((stat = decode_open_type(buf, len, &ptr, &data, &s->rx[x].fec_len[i])) != 0)
				return -1;
			if (s->rx[x].fec_len[i] > LOCAL_FAX_MAX_DATAGRAM)
				return -1;

			/* Save the new FEC data */
			memcpy(s->rx[x].fec[i], data, s->rx[x].fec_len[i]);
#if 0
			fprintf(stderr, "FEC: ");
			for (j = 0; j < s->rx[x].fec_len[i]; j++)
				fprintf(stderr, "%02X ", data[j]);
			fprintf(stderr, "\n");
#endif
		}

		/* See if we can reconstruct anything which is missing */
		/* TODO: this does not comprehensively hunt back and repair everything that is possible */
		for (l = x; l != ((x - (16 - span*entries)) & UDPTL_BUF_MASK); l = (l - 1) & UDPTL_BUF_MASK) {
			if (s->rx[l].fec_len[0] <= 0)
				continue;
			for (m = 0; m < s->rx[l].fec_entries; m++) {
				limit = (l + m) & UDPTL_BUF_MASK;
				for (which = -1, k = (limit - s->rx[l].fec_span * s->rx[l].fec_entries) & UDPTL_BUF_MASK; k != limit; k = (k + s->rx[l].fec_entries) & UDPTL_BUF_MASK) {
					if (s->rx[k].buf_len <= 0)
						which = (which == -1) ? k : -2;
				}
				if (which >= 0) {
					/* Repairable */
					for (j = 0; j < s->rx[l].fec_len[m]; j++) {
						s->rx[which].buf[j] = s->rx[l].fec[m][j];
						for (k = (limit - s->rx[l].fec_span * s->rx[l].fec_entries) & UDPTL_BUF_MASK; k != limit; k = (k + s->rx[l].fec_entries) & UDPTL_BUF_MASK)
							s->rx[which].buf[j] ^= (s->rx[k].buf_len > j) ? s->rx[k].buf[j] : 0;
					}
					s->rx[which].buf_len = s->rx[l].fec_len[m];
					repaired[which] = TRUE;
				}
			}
		}
		/* Now play any new packets forwards in time */
		for (l = (x + 1) & UDPTL_BUF_MASK, j = seq_no - UDPTL_BUF_MASK; l != x; l = (l + 1) & UDPTL_BUF_MASK, j++) {
			if (repaired[l]) {
				//fprintf(stderr, "Fixed packet %d, len %d\n", j, l);
				s->f[ifp_no].frametype = AST_FRAME_MODEM;
				s->f[ifp_no].subclass = AST_MODEM_T38;
			
				s->f[ifp_no].mallocd = 0;
				s->f[ifp_no].seqno = j;
				s->f[ifp_no].datalen = s->rx[l].buf_len;
				s->f[ifp_no].data = s->rx[l].buf;
				s->f[ifp_no].offset = 0;
				s->f[ifp_no].src = "UDPTL";
				if (ifp_no > 0)
					AST_LIST_NEXT(&s->f[ifp_no - 1], frame_list) = &s->f[ifp_no];
				AST_LIST_NEXT(&s->f[ifp_no], frame_list) = NULL;
				ifp_no++;
			}
		}
	}

	/* If packets are received out of sequence, we may have already processed this packet from the error
	   recovery information in a packet already received. */
	if (seq_no >= s->rx_seq_no) {
		/* Decode the primary IFP packet */
		s->f[ifp_no].frametype = AST_FRAME_MODEM;
		s->f[ifp_no].subclass = AST_MODEM_T38;
		
		s->f[ifp_no].mallocd = 0;
		s->f[ifp_no].seqno = seq_no;
		s->f[ifp_no].datalen = ifp_len;
		s->f[ifp_no].data = (uint8_t *) ifp;
		s->f[ifp_no].offset = 0;
		s->f[ifp_no].src = "UDPTL";
		if (ifp_no > 0)
			AST_LIST_NEXT(&s->f[ifp_no - 1], frame_list) = &s->f[ifp_no];
		AST_LIST_NEXT(&s->f[ifp_no], frame_list) = NULL;

		ifp_no++;
	}

	s->rx_seq_no = seq_no + 1;
	return ifp_no;
}
/*- End of function --------------------------------------------------------*/

static int udptl_build_packet(struct ast_udptl *s, uint8_t *buf, int buflen, uint8_t *ifp, int ifp_len)
{
	uint8_t fec[LOCAL_FAX_MAX_DATAGRAM * 2];
	int i;
	int j;
	int seq;
	int entry;
	int entries;
	int span;
	int m;
	int len;
	int limit;
	int high_tide;

	seq = s->tx_seq_no & 0xFFFF;

	/* Map the sequence number to an entry in the circular buffer */
	entry = seq & UDPTL_BUF_MASK;

	/* We save the message in a circular buffer, for generating FEC or
	   redundancy sets later on. */
	s->tx[entry].buf_len = ifp_len;
	memcpy(s->tx[entry].buf, ifp, ifp_len);
	
	/* Build the UDPTLPacket */

	len = 0;
	/* Encode the sequence number */
	buf[len++] = (seq >> 8) & 0xFF;
	buf[len++] = seq & 0xFF;

	/* Encode the primary IFP packet */
	if (encode_open_type(buf, buflen, &len, ifp, ifp_len) < 0)
		return -1;

	/* Encode the appropriate type of error recovery information */
	switch (s->error_correction_scheme)
	{
	case UDPTL_ERROR_CORRECTION_NONE:
		/* Encode the error recovery type */
		buf[len++] = 0x00;
		/* The number of entries will always be zero, so it is pointless allowing
		   for the fragmented case here. */
		if (encode_length(buf, &len, 0) < 0)
			return -1;
		break;
	case UDPTL_ERROR_CORRECTION_REDUNDANCY:
		/* Encode the error recovery type */
		buf[len++] = 0x00;
		if (s->tx_seq_no > s->error_correction_entries)
			entries = s->error_correction_entries;
		else
			entries = s->tx_seq_no;
		/* The number of entries will always be small, so it is pointless allowing
		   for the fragmented case here. */
		if (encode_length(buf, &len, entries) < 0)
			return -1;
		/* Encode the elements */
		for (i = 0; i < entries; i++) {
			j = (entry - i - 1) & UDPTL_BUF_MASK;
			if (encode_open_type(buf, buflen, &len, s->tx[j].buf, s->tx[j].buf_len) < 0) {
				if (option_debug) {
					ast_log(LOG_DEBUG, "Encoding failed at i=%d, j=%d\n", i, j);
				}
				return -1;
			}
		}
		break;
	case UDPTL_ERROR_CORRECTION_FEC:
		span = s->error_correction_span;
		entries = s->error_correction_entries;
		if (seq < s->error_correction_span*s->error_correction_entries) {
			/* In the initial stages, wind up the FEC smoothly */
			entries = seq/s->error_correction_span;
			if (seq < s->error_correction_span)
				span = 0;
		}
		/* Encode the error recovery type */
		buf[len++] = 0x80;
		/* Span is defined as an inconstrained integer, which it dumb. It will only
		   ever be a small value. Treat it as such. */
		buf[len++] = 1;
		buf[len++] = span;
		/* The number of entries is defined as a length, but will only ever be a small
		   value. Treat it as such. */
		buf[len++] = entries;
		for (m = 0; m < entries; m++) {
			/* Make an XOR'ed entry the maximum length */
			limit = (entry + m) & UDPTL_BUF_MASK;
			high_tide = 0;
			for (i = (limit - span*entries) & UDPTL_BUF_MASK; i != limit; i = (i + entries) & UDPTL_BUF_MASK) {
				if (high_tide < s->tx[i].buf_len) {
					for (j = 0; j < high_tide; j++)
						fec[j] ^= s->tx[i].buf[j];
					for ( ; j < s->tx[i].buf_len; j++)
						fec[j] = s->tx[i].buf[j];
					high_tide = s->tx[i].buf_len;
				} else {
					for (j = 0; j < s->tx[i].buf_len; j++)
						fec[j] ^= s->tx[i].buf[j];
				}
			}
			if (encode_open_type(buf, buflen, &len, fec, high_tide) < 0)
				return -1;
		}
		break;
	}

	if (s->verbose)
		fprintf(stderr, "\n");

	s->tx_seq_no++;
	return len;
}

int ast_udptl_fd(struct ast_udptl *udptl)
{
	return udptl->fd;
}

void ast_udptl_set_data(struct ast_udptl *udptl, void *data)
{
	udptl->data = data;
}

void ast_udptl_set_callback(struct ast_udptl *udptl, ast_udptl_callback callback)
{
	udptl->callback = callback;
}

void ast_udptl_setnat(struct ast_udptl *udptl, int nat)
{
	udptl->nat = nat;
}

static int udptlread(int *id, int fd, short events, void *cbdata)
{
	struct ast_udptl *udptl = cbdata;
	struct ast_frame *f;

	if ((f = ast_udptl_read(udptl))) {
		if (udptl->callback)
			udptl->callback(udptl, f, udptl->data);
	}
	return 1;
}

struct ast_frame *ast_udptl_read(struct ast_udptl *udptl)
{
	int res;
	struct sockaddr_in sin;
	socklen_t len;
	uint16_t seqno = 0;
	uint16_t *udptlheader;

	len = sizeof(sin);
	
	/* Cache where the header will go */
	res = recvfrom(udptl->fd,
			udptl->rawdata + AST_FRIENDLY_OFFSET,
			sizeof(udptl->rawdata) - AST_FRIENDLY_OFFSET,
			0,
			(struct sockaddr *) &sin,
			&len);
	udptlheader = (uint16_t *)(udptl->rawdata + AST_FRIENDLY_OFFSET);
	if (res < 0) {
		if (errno != EAGAIN)
			ast_log(LOG_WARNING, "UDPTL read error: %s\n", strerror(errno));
		ast_assert(errno != EBADF);
		return &ast_null_frame;
	}

	/* Ignore if the other side hasn't been given an address yet. */
	if (!udptl->them.sin_addr.s_addr || !udptl->them.sin_port)
		return &ast_null_frame;

	if (udptl->nat) {
		/* Send to whoever sent to us */
		if ((udptl->them.sin_addr.s_addr != sin.sin_addr.s_addr) ||
			(udptl->them.sin_port != sin.sin_port)) {
			memcpy(&udptl->them, &sin, sizeof(udptl->them));
			ast_log(LOG_DEBUG, "UDPTL NAT: Using address %s:%d\n", ast_inet_ntoa(udptl->them.sin_addr), ntohs(udptl->them.sin_port));
		}
	}

	if (udptl_debug_test_addr(&sin)) {
		ast_verbose("Got UDPTL packet from %s:%d (type %d, seq %d, len %d)\n",
			ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), 0, seqno, res);
	}
#if 0
	printf("Got UDPTL packet from %s:%d (seq %d, len = %d)\n", ast_inet_ntoa(sin.sin_addr), ntohs(sin.sin_port), seqno, res);
#endif
	if (udptl_rx_packet(udptl, udptl->rawdata + AST_FRIENDLY_OFFSET, res) < 1)
		return &ast_null_frame;

	return &udptl->f[0];
}

void ast_udptl_offered_from_local(struct ast_udptl* udptl, int local)
{
	if (udptl)
		udptl->udptl_offered_from_local = local;
	else
		ast_log(LOG_WARNING, "udptl structure is null\n");
}

int ast_udptl_get_error_correction_scheme(struct ast_udptl* udptl)
{
	if (udptl)
		return udptl->error_correction_scheme;
	else {
		ast_log(LOG_WARNING, "udptl structure is null\n");
		return -1;
	}
}

void ast_udptl_set_error_correction_scheme(struct ast_udptl* udptl, int ec)
{
	if (udptl) {
		switch (ec) {
		case UDPTL_ERROR_CORRECTION_FEC:
			udptl->error_correction_scheme = UDPTL_ERROR_CORRECTION_FEC;
			break;
		case UDPTL_ERROR_CORRECTION_REDUNDANCY:
			udptl->error_correction_scheme = UDPTL_ERROR_CORRECTION_REDUNDANCY;
			break;
		case UDPTL_ERROR_CORRECTION_NONE:
			udptl->error_correction_scheme = UDPTL_ERROR_CORRECTION_NONE;
			break;
		default:
			ast_log(LOG_WARNING, "error correction parameter invalid\n");
		};
	} else
		ast_log(LOG_WARNING, "udptl structure is null\n");
}

int ast_udptl_get_local_max_datagram(struct ast_udptl* udptl)
{
	if (udptl)
		return udptl->local_max_datagram_size;
	else {
		ast_log(LOG_WARNING, "udptl structure is null\n");
		return -1;
	}
}

int ast_udptl_get_far_max_datagram(struct ast_udptl* udptl)
{
	if (udptl)
		return udptl->far_max_datagram_size;
	else {
		ast_log(LOG_WARNING, "udptl structure is null\n");
		return -1;
	}
}

void ast_udptl_set_local_max_datagram(struct ast_udptl* udptl, int max_datagram)
{
	if (udptl)
		udptl->local_max_datagram_size = max_datagram;
	else
		ast_log(LOG_WARNING, "udptl structure is null\n");
}

void ast_udptl_set_far_max_datagram(struct ast_udptl* udptl, int max_datagram)
{
	if (udptl)
		udptl->far_max_datagram_size = max_datagram;
	else
		ast_log(LOG_WARNING, "udptl structure is null\n");
}

struct ast_udptl *ast_udptl_new_with_bindaddr(struct sched_context *sched, struct io_context *io, int callbackmode, struct in_addr addr)
{
	struct ast_udptl *udptl;
	int x;
	int startplace;
	int i;
	long int flags;

	if (!(udptl = ast_calloc(1, sizeof(*udptl))))
		return NULL;

	if (udptlfectype == 2)
		udptl->error_correction_scheme = UDPTL_ERROR_CORRECTION_FEC;
	else if (udptlfectype == 1)
		udptl->error_correction_scheme = UDPTL_ERROR_CORRECTION_REDUNDANCY;
	else
		udptl->error_correction_scheme = UDPTL_ERROR_CORRECTION_NONE;
	udptl->error_correction_span = udptlfecspan;
	udptl->error_correction_entries = udptlfecentries;
	
	udptl->far_max_datagram_size = udptlmaxdatagram;
	udptl->local_max_datagram_size = udptlmaxdatagram;

	memset(&udptl->rx, 0, sizeof(udptl->rx));
	memset(&udptl->tx, 0, sizeof(udptl->tx));
	for (i = 0; i <= UDPTL_BUF_MASK; i++) {
		udptl->rx[i].buf_len = -1;
		udptl->tx[i].buf_len = -1;
	}

	udptl->them.sin_family = AF_INET;
	udptl->us.sin_family = AF_INET;

	if ((udptl->fd = socket(AF_INET, SOCK_DGRAM, 0)) < 0) {
		free(udptl);
		ast_log(LOG_WARNING, "Unable to allocate socket: %s\n", strerror(errno));
		return NULL;
	}
	flags = fcntl(udptl->fd, F_GETFL);
	fcntl(udptl->fd, F_SETFL, flags | O_NONBLOCK);
#ifdef SO_NO_CHECK
	if (nochecksums)
		setsockopt(udptl->fd, SOL_SOCKET, SO_NO_CHECK, &nochecksums, sizeof(nochecksums));
#endif
	/* Find us a place */
	x = (ast_random() % (udptlend - udptlstart)) + udptlstart;
	if (use_even_ports && (x & 1)) {
		++x;
	}
	startplace = x;
	for (;;) {
		udptl->us.sin_port = htons(x);
		udptl->us.sin_addr = addr;
		if (bind(udptl->fd, (struct sockaddr *) &udptl->us, sizeof(udptl->us)) == 0)
			break;
		if (errno != EADDRINUSE) {
			ast_log(LOG_WARNING, "Unexpected bind error: %s\n", strerror(errno));
			close(udptl->fd);
			free(udptl);
			return NULL;
		}
		if (use_even_ports) {
			x += 2;
		} else {
			++x;
		}
		if (x > udptlend)
			x = udptlstart;
		if (x == startplace) {
			ast_log(LOG_WARNING, "No UDPTL ports remaining\n");
			close(udptl->fd);
			free(udptl);
			return NULL;
		}
	}
	if (io && sched && callbackmode) {
		/* Operate this one in a callback mode */
		udptl->sched = sched;
		udptl->io = io;
		udptl->ioid = ast_io_add(udptl->io, udptl->fd, udptlread, AST_IO_IN, udptl);
	}
	return udptl;
}

struct ast_udptl *ast_udptl_new(struct sched_context *sched, struct io_context *io, int callbackmode)
{
	struct in_addr ia;
	memset(&ia, 0, sizeof(ia));
	return ast_udptl_new_with_bindaddr(sched, io, callbackmode, ia);
}

int ast_udptl_settos(struct ast_udptl *udptl, int tos)
{
	int res;

	if ((res = setsockopt(udptl->fd, IPPROTO_IP, IP_TOS, &tos, sizeof(tos)))) 
		ast_log(LOG_WARNING, "UDPTL unable to set TOS to %d\n", tos);
	return res;
}

void ast_udptl_set_peer(struct ast_udptl *udptl, struct sockaddr_in *them)
{
	udptl->them.sin_port = them->sin_port;
	udptl->them.sin_addr = them->sin_addr;
}

void ast_udptl_get_peer(struct ast_udptl *udptl, struct sockaddr_in *them)
{
	memset(them, 0, sizeof(*them));
	them->sin_family = AF_INET;
	them->sin_port = udptl->them.sin_port;
	them->sin_addr = udptl->them.sin_addr;
}

void ast_udptl_get_us(struct ast_udptl *udptl, struct sockaddr_in *us)
{
	memcpy(us, &udptl->us, sizeof(udptl->us));
}

void ast_udptl_stop(struct ast_udptl *udptl)
{
	memset(&udptl->them.sin_addr, 0, sizeof(udptl->them.sin_addr));
	memset(&udptl->them.sin_port, 0, sizeof(udptl->them.sin_port));
}

void ast_udptl_destroy(struct ast_udptl *udptl)
{
	if (udptl->ioid)
		ast_io_remove(udptl->io, udptl->ioid);
	if (udptl->fd > -1)
		close(udptl->fd);
	free(udptl);
}

int ast_udptl_write(struct ast_udptl *s, struct ast_frame *f)
{
	int seq;
	int len;
	int res;
	uint8_t buf[LOCAL_FAX_MAX_DATAGRAM * 2];

	/* If we have no peer, return immediately */	
	if (s->them.sin_addr.s_addr == INADDR_ANY)
		return 0;

	/* If there is no data length, return immediately */
	if (f->datalen == 0)
		return 0;
	
	if (f->frametype != AST_FRAME_MODEM) {
		ast_log(LOG_WARNING, "UDPTL can only send T.38 data\n");
		return -1;
	}

	/* Save seq_no for debug output because udptl_build_packet increments it */
	seq = s->tx_seq_no & 0xFFFF;

	/* Cook up the UDPTL packet, with the relevant EC info. */
	len = udptl_build_packet(s, buf, sizeof(buf), f->data, f->datalen);

	if (len > 0 && s->them.sin_port && s->them.sin_addr.s_addr) {
		if ((res = sendto(s->fd, buf, len, 0, (struct sockaddr *) &s->them, sizeof(s->them))) < 0)
			ast_log(LOG_NOTICE, "UDPTL Transmission error to %s:%d: %s\n", ast_inet_ntoa(s->them.sin_addr), ntohs(s->them.sin_port), strerror(errno));
#if 0
		printf("Sent %d bytes of UDPTL data to %s:%d\n", res, ast_inet_ntoa(udptl->them.sin_addr), ntohs(udptl->them.sin_port));
#endif
		if (udptl_debug_test_addr(&s->them))
			ast_verbose("Sent UDPTL packet to %s:%d (type %d, seq %d, len %d)\n",
					ast_inet_ntoa(s->them.sin_addr),
					ntohs(s->them.sin_port), 0, seq, len);
	}
		
	return 0;
}

void ast_udptl_proto_unregister(struct ast_udptl_protocol *proto)
{
	struct ast_udptl_protocol *cur;
	struct ast_udptl_protocol *prev;

	cur = protos;
	prev = NULL;
	while (cur) {
		if (cur == proto) {
			if (prev)
				prev->next = proto->next;
			else
				protos = proto->next;
			return;
		}
		prev = cur;
		cur = cur->next;
	}
}

int ast_udptl_proto_register(struct ast_udptl_protocol *proto)
{
	struct ast_udptl_protocol *cur;

	cur = protos;
	while (cur) {
		if (cur->type == proto->type) {
			ast_log(LOG_WARNING, "Tried to register same protocol '%s' twice\n", cur->type);
			return -1;
		}
		cur = cur->next;
	}
	proto->next = protos;
	protos = proto;
	return 0;
}

static struct ast_udptl_protocol *get_proto(struct ast_channel *chan)
{
	struct ast_udptl_protocol *cur;

	cur = protos;
	while (cur) {
		if (cur->type == chan->tech->type)
			return cur;
		cur = cur->next;
	}
	return NULL;
}

int ast_udptl_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc)
{
	struct ast_frame *f;
	struct ast_channel *who;
	struct ast_channel *cs[3];
	struct ast_udptl *p0;
	struct ast_udptl *p1;
	struct ast_udptl_protocol *pr0;
	struct ast_udptl_protocol *pr1;
	struct sockaddr_in ac0;
	struct sockaddr_in ac1;
	struct sockaddr_in t0;
	struct sockaddr_in t1;
	void *pvt0;
	void *pvt1;
	int to;
	
	ast_channel_lock(c0);
	while (ast_channel_trylock(c1)) {
		ast_channel_unlock(c0);
		usleep(1);
		ast_channel_lock(c0);
	}
	pr0 = get_proto(c0);
	pr1 = get_proto(c1);
	if (!pr0) {
		ast_log(LOG_WARNING, "Can't find native functions for channel '%s'\n", c0->name);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return -1;
	}
	if (!pr1) {
		ast_log(LOG_WARNING, "Can't find native functions for channel '%s'\n", c1->name);
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return -1;
	}
	pvt0 = c0->tech_pvt;
	pvt1 = c1->tech_pvt;
	p0 = pr0->get_udptl_info(c0);
	p1 = pr1->get_udptl_info(c1);
	if (!p0 || !p1) {
		/* Somebody doesn't want to play... */
		ast_channel_unlock(c0);
		ast_channel_unlock(c1);
		return -2;
	}
	if (pr0->set_udptl_peer(c0, p1)) {
		ast_log(LOG_WARNING, "Channel '%s' failed to talk to '%s'\n", c0->name, c1->name);
		memset(&ac1, 0, sizeof(ac1));
	} else {
		/* Store UDPTL peer */
		ast_udptl_get_peer(p1, &ac1);
	}
	if (pr1->set_udptl_peer(c1, p0)) {
		ast_log(LOG_WARNING, "Channel '%s' failed to talk back to '%s'\n", c1->name, c0->name);
		memset(&ac0, 0, sizeof(ac0));
	} else {
		/* Store UDPTL peer */
		ast_udptl_get_peer(p0, &ac0);
	}
	ast_channel_unlock(c0);
	ast_channel_unlock(c1);
	cs[0] = c0;
	cs[1] = c1;
	cs[2] = NULL;
	for (;;) {
		if ((c0->tech_pvt != pvt0) ||
			(c1->tech_pvt != pvt1) ||
			(c0->masq || c0->masqr || c1->masq || c1->masqr)) {
				ast_log(LOG_DEBUG, "Oooh, something is weird, backing out\n");
				/* Tell it to try again later */
				return -3;
		}
		to = -1;
		ast_udptl_get_peer(p1, &t1);
		ast_udptl_get_peer(p0, &t0);
		if (inaddrcmp(&t1, &ac1)) {
			ast_log(LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d\n", 
				c1->name, ast_inet_ntoa(t1.sin_addr), ntohs(t1.sin_port));
			ast_log(LOG_DEBUG, "Oooh, '%s' was %s:%d\n", 
				c1->name, ast_inet_ntoa(ac1.sin_addr), ntohs(ac1.sin_port));
			memcpy(&ac1, &t1, sizeof(ac1));
		}
		if (inaddrcmp(&t0, &ac0)) {
			ast_log(LOG_DEBUG, "Oooh, '%s' changed end address to %s:%d\n", 
				c0->name, ast_inet_ntoa(t0.sin_addr), ntohs(t0.sin_port));
			ast_log(LOG_DEBUG, "Oooh, '%s' was %s:%d\n", 
				c0->name, ast_inet_ntoa(ac0.sin_addr), ntohs(ac0.sin_port));
			memcpy(&ac0, &t0, sizeof(ac0));
		}
		who = ast_waitfor_n(cs, 2, &to);
		if (!who) {
			ast_log(LOG_DEBUG, "Ooh, empty read...\n");
			/* check for hangup / whentohangup */
			if (ast_check_hangup(c0) || ast_check_hangup(c1))
				break;
			continue;
		}
		f = ast_read(who);
		if (!f) {
			*fo = f;
			*rc = who;
			ast_log(LOG_DEBUG, "Oooh, got a %s\n", f ? "digit" : "hangup");
			/* That's all we needed */
			return 0;
		} else {
			if (f->frametype == AST_FRAME_MODEM) {
				/* Forward T.38 frames if they happen upon us */
				if (who == c0) {
					ast_write(c1, f);
				} else if (who == c1) {
					ast_write(c0, f);
				}
			}
			ast_frfree(f);
		}
		/* Swap priority. Not that it's a big deal at this point */
		cs[2] = cs[0];
		cs[0] = cs[1];
		cs[1] = cs[2];
	}
	return -1;
}

static int udptl_do_debug_ip(int fd, int argc, char *argv[])
{
	struct hostent *hp;
	struct ast_hostent ahp;
	int port;
	char *p;
	char *arg;

	port = 0;
	if (argc != 4)
		return RESULT_SHOWUSAGE;
	arg = argv[3];
	p = strstr(arg, ":");
	if (p) {
		*p = '\0';
		p++;
		port = atoi(p);
	}
	hp = ast_gethostbyname(arg, &ahp);
	if (hp == NULL)
		return RESULT_SHOWUSAGE;
	udptldebugaddr.sin_family = AF_INET;
	memcpy(&udptldebugaddr.sin_addr, hp->h_addr, sizeof(udptldebugaddr.sin_addr));
	udptldebugaddr.sin_port = htons(port);
	if (port == 0)
		ast_cli(fd, "UDPTL Debugging Enabled for IP: %s\n", ast_inet_ntoa(udptldebugaddr.sin_addr));
	else
		ast_cli(fd, "UDPTL Debugging Enabled for IP: %s:%d\n", ast_inet_ntoa(udptldebugaddr.sin_addr), port);
	udptldebug = 1;
	return RESULT_SUCCESS;
}

static int udptl_do_debug(int fd, int argc, char *argv[])
{
	if (argc != 2) {
		if (argc != 4)
			return RESULT_SHOWUSAGE;
		return udptl_do_debug_ip(fd, argc, argv);
	}
	udptldebug = 1;
	memset(&udptldebugaddr,0,sizeof(udptldebugaddr));
	ast_cli(fd, "UDPTL Debugging Enabled\n");
	return RESULT_SUCCESS;
}

static int udptl_nodebug(int fd, int argc, char *argv[])
{
	if (argc != 3)
		return RESULT_SHOWUSAGE;
	udptldebug = 0;
	ast_cli(fd,"UDPTL Debugging Disabled\n");
	return RESULT_SUCCESS;
}

static char debug_usage[] =
  "Usage: udptl debug [ip host[:port]]\n"
  "       Enable dumping of all UDPTL packets to and from host.\n";

static char nodebug_usage[] =
  "Usage: udptl debug off\n"
  "       Disable all UDPTL debugging\n";

static struct ast_cli_entry cli_udptl_no_debug = {
	{ "udptl", "no", "debug", NULL },
	udptl_nodebug, NULL,
	NULL };

static struct ast_cli_entry cli_udptl[] = {
	{ { "udptl", "debug", NULL },
	udptl_do_debug, "Enable UDPTL debugging",
	debug_usage },

	{ { "udptl", "debug", "ip", NULL },
	udptl_do_debug, "Enable UDPTL debugging on IP",
	debug_usage },

	{ { "udptl", "debug", "off", NULL },
	udptl_nodebug, "Disable UDPTL debugging",
	nodebug_usage, NULL, &cli_udptl_no_debug },
};

void ast_udptl_reload(void)
{
	struct ast_config *cfg;
	const char *s;

	udptlstart = 4500;
	udptlend = 4999;
	udptlfectype = 0;
	udptlfecentries = 0;
	udptlfecspan = 0;
	udptlmaxdatagram = 0;
	use_even_ports = 0;

	if ((cfg = ast_config_load("udptl.conf"))) {
		if ((s = ast_variable_retrieve(cfg, "general", "udptlstart"))) {
			udptlstart = atoi(s);
			if (udptlstart < 1024) {
				ast_log(LOG_WARNING, "Ports under 1024 are not allowed for T.38.\n");
				udptlstart = 1024;
			}
			if (udptlstart > 65535) {
				ast_log(LOG_WARNING, "Ports over 65535 are invalid.\n");
				udptlstart = 65535;
			}
		}
		if ((s = ast_variable_retrieve(cfg, "general", "udptlend"))) {
			udptlend = atoi(s);
			if (udptlend < 1024) {
				ast_log(LOG_WARNING, "Ports under 1024 are not allowed for T.38.\n");
				udptlend = 1024;
			}
			if (udptlend > 65535) {
				ast_log(LOG_WARNING, "Ports over 65535 are invalid.\n");
				udptlend = 65535;
			}
		}
		if ((s = ast_variable_retrieve(cfg, "general", "udptlchecksums"))) {
#ifdef SO_NO_CHECK
			if (ast_false(s))
				nochecksums = 1;
			else
				nochecksums = 0;
#else
			if (ast_false(s))
				ast_log(LOG_WARNING, "Disabling UDPTL checksums is not supported on this operating system!\n");
#endif
		}
		if ((s = ast_variable_retrieve(cfg, "general", "T38FaxUdpEC"))) {
			if (strcmp(s, "t38UDPFEC") == 0)
				udptlfectype = 2;
			else if (strcmp(s, "t38UDPRedundancy") == 0)
				udptlfectype = 1;
		}
		if ((s = ast_variable_retrieve(cfg, "general", "T38FaxMaxDatagram"))) {
			udptlmaxdatagram = atoi(s);
			if (udptlmaxdatagram < 100) {
				ast_log(LOG_WARNING, "Too small T38FaxMaxDatagram size.  Defaulting to 100.\n");
				udptlmaxdatagram = 100;
			}
			if (udptlmaxdatagram > LOCAL_FAX_MAX_DATAGRAM) {
				ast_log(LOG_WARNING, "Too large T38FaxMaxDatagram size.  Defaulting to %d.\n", LOCAL_FAX_MAX_DATAGRAM);
				udptlmaxdatagram = LOCAL_FAX_MAX_DATAGRAM;
			}
		}
		if ((s = ast_variable_retrieve(cfg, "general", "UDPTLFECentries"))) {
			udptlfecentries = atoi(s);
			if (udptlfecentries < 1) {
				ast_log(LOG_WARNING, "Too small UDPTLFECentries value.  Defaulting to 1.\n");
				udptlfecentries = 1;
			}
			if (udptlfecentries > MAX_FEC_ENTRIES) {
				ast_log(LOG_WARNING, "Too large UDPTLFECentries value.  Defaulting to %d.\n", MAX_FEC_ENTRIES);
				udptlfecentries = MAX_FEC_ENTRIES;
			}
		}
		if ((s = ast_variable_retrieve(cfg, "general", "UDPTLFECspan"))) {
			udptlfecspan = atoi(s);
			if (udptlfecspan < 1) {
				ast_log(LOG_WARNING, "Too small UDPTLFECspan value.  Defaulting to 1.\n");
				udptlfecspan = 1;
			}
			if (udptlfecspan > MAX_FEC_SPAN) {
				ast_log(LOG_WARNING, "Too large UDPTLFECspan value.  Defaulting to %d.\n", MAX_FEC_SPAN);
				udptlfecspan = MAX_FEC_SPAN;
			}
		}
		if ((s = ast_variable_retrieve(cfg, "general", "use_even_ports"))) {
			use_even_ports = ast_true(s);
		}
		ast_config_destroy(cfg);
	}
	if (udptlstart >= udptlend) {
		ast_log(LOG_WARNING, "Unreasonable values for UDPTL start/end\n");
		udptlstart = 4500;
		udptlend = 4999;
	}
	if (use_even_ports && (udptlstart & 1)) {
		++udptlstart;
		ast_log(LOG_NOTICE, "Odd numbered udptlstart specified but use_even_ports enabled. udptlstart is now %d\n", udptlstart);
	}
	if (use_even_ports && (udptlend & 1)) {
		--udptlend;
		ast_log(LOG_NOTICE, "Odd numbered udptlend specified but use_event_ports enabled. udptlend is now %d\n", udptlend);
	}
	if (option_verbose > 1)
		ast_verbose(VERBOSE_PREFIX_2 "UDPTL allocating from port range %d -> %d\n", udptlstart, udptlend);
}

void ast_udptl_init(void)
{
	ast_cli_register_multiple(cli_udptl, sizeof(cli_udptl) / sizeof(struct ast_cli_entry));
	ast_udptl_reload();
}
