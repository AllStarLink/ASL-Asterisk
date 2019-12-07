/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 * \brief Access Control of various sorts
 */

#ifndef _ASTERISK_ACL_H
#define _ASTERISK_ACL_H


#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <netinet/in.h>
#include "asterisk/io.h"

#define AST_SENSE_DENY                  0
#define AST_SENSE_ALLOW                 1

/* Host based access control */
struct ast_ha {
	/* Host access rule */
	struct in_addr netaddr;
	struct in_addr netmask;
	int sense;
	struct ast_ha *next;
};

void ast_free_ha(struct ast_ha *ha);
void ast_copy_ha(const struct ast_ha *from, struct ast_ha *to);
struct ast_ha *ast_append_ha(char *sense, const char *stuff, struct ast_ha *path);
int ast_apply_ha(struct ast_ha *ha, struct sockaddr_in *sin);
int ast_get_ip(struct sockaddr_in *sin, const char *value);
int ast_get_ip_or_srv(struct sockaddr_in *sin, const char *value, const char *service);
int ast_ouraddrfor(struct in_addr *them, struct in_addr *us);
int ast_lookup_iface(char *iface, struct in_addr *address);
struct ast_ha *ast_duplicate_ha_list(struct ast_ha *original);
int ast_find_ourip(struct in_addr *ourip, struct sockaddr_in bindaddr);
int ast_str2tos(const char *value, unsigned int *tos);
const char *ast_tos2str(unsigned int tos);

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_ACL_H */
