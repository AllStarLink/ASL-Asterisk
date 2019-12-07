/*
 * Asterisk -- A telephony toolkit for Linux.
 *
 * UDPTL support for T.38
 * 
 * Copyright (C) 2005, Steve Underwood, partly based on RTP code which is
 * Copyright (C) 1999-2004, Digium, Inc.
 *
 * Steve Underwood <steveu@coppice.org>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License
 *
 * A license has been granted to Digium (via disclaimer) for the use of
 * this code.
 */

#ifndef _ASTERISK_UDPTL_H
#define _ASTERISK_UDPTL_H

#include "asterisk/frame.h"
#include "asterisk/io.h"
#include "asterisk/sched.h"
#include "asterisk/channel.h"

#include <netinet/in.h>

enum
{
    UDPTL_ERROR_CORRECTION_NONE,
    UDPTL_ERROR_CORRECTION_FEC,
    UDPTL_ERROR_CORRECTION_REDUNDANCY
};

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

struct ast_udptl_protocol {
	/* Get UDPTL struct, or NULL if unwilling to transfer */
	struct ast_udptl *(*get_udptl_info)(struct ast_channel *chan);
	/* Set UDPTL peer */
	int (* const set_udptl_peer)(struct ast_channel *chan, struct ast_udptl *peer);
	const char * const type;
	struct ast_udptl_protocol *next;
};

struct ast_udptl;

typedef int (*ast_udptl_callback)(struct ast_udptl *udptl, struct ast_frame *f, void *data);

struct ast_udptl *ast_udptl_new(struct sched_context *sched, struct io_context *io, int callbackmode);

struct ast_udptl *ast_udptl_new_with_bindaddr(struct sched_context *sched, struct io_context *io, int callbackmode, struct in_addr in);

void ast_udptl_set_peer(struct ast_udptl *udptl, struct sockaddr_in *them);

void ast_udptl_get_peer(struct ast_udptl *udptl, struct sockaddr_in *them);

void ast_udptl_get_us(struct ast_udptl *udptl, struct sockaddr_in *us);

void ast_udptl_destroy(struct ast_udptl *udptl);

void ast_udptl_reset(struct ast_udptl *udptl);

void ast_udptl_set_callback(struct ast_udptl *udptl, ast_udptl_callback callback);

void ast_udptl_set_data(struct ast_udptl *udptl, void *data);

int ast_udptl_write(struct ast_udptl *udptl, struct ast_frame *f);

struct ast_frame *ast_udptl_read(struct ast_udptl *udptl);

int ast_udptl_fd(struct ast_udptl *udptl);

int ast_udptl_settos(struct ast_udptl *udptl, int tos);

void ast_udptl_set_m_type(struct ast_udptl* udptl, int pt);

void ast_udptl_set_udptlmap_type(struct ast_udptl* udptl, int pt,
			 char* mimeType, char* mimeSubtype);

int ast_udptl_lookup_code(struct ast_udptl* udptl, int isAstFormat, int code);

void ast_udptl_offered_from_local(struct ast_udptl* udptl, int local);

int ast_udptl_get_error_correction_scheme(struct ast_udptl* udptl);

void ast_udptl_set_error_correction_scheme(struct ast_udptl* udptl, int ec);

int ast_udptl_get_local_max_datagram(struct ast_udptl* udptl);

void ast_udptl_set_local_max_datagram(struct ast_udptl* udptl, int max_datagram);

int ast_udptl_get_far_max_datagram(struct ast_udptl* udptl);

void ast_udptl_set_far_max_datagram(struct ast_udptl* udptl, int max_datagram);

void ast_udptl_get_current_formats(struct ast_udptl* udptl,
			     int* astFormats, int* nonAstFormats);

void ast_udptl_setnat(struct ast_udptl *udptl, int nat);

int ast_udptl_bridge(struct ast_channel *c0, struct ast_channel *c1, int flags, struct ast_frame **fo, struct ast_channel **rc);

int ast_udptl_proto_register(struct ast_udptl_protocol *proto);

void ast_udptl_proto_unregister(struct ast_udptl_protocol *proto);

void ast_udptl_stop(struct ast_udptl *udptl);

void ast_udptl_init(void);

void ast_udptl_reload(void);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif
