/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
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

#ifndef _ASTERISK_MANAGER_H
#define _ASTERISK_MANAGER_H

#include <stdarg.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>

#include "asterisk/lock.h"

/*!
 \file
 \brief The AMI - Asterisk Manager Interface - is a TCP protocol created to
 manage Asterisk with third-party software.

 Manager protocol packages are text fields of the form a: b.  There is
 always exactly one space after the colon.

 The first header type is the "Event" header.  Other headers vary from
 event to event.  Headers end with standard \r\n termination.
 The last line of the manager response or event is an empty line.
 (\r\n)

 ** Please try to re-use existing headers to simplify manager message parsing in clients.
    Don't re-use an existing header with a new meaning, please.
    You can find a reference of standard headers in doc/manager.txt
 */

#define DEFAULT_MANAGER_PORT 5038	/* Default port for Asterisk management via TCP */

#define EVENT_FLAG_SYSTEM 		(1 << 0) /* System events such as module load/unload */
#define EVENT_FLAG_CALL			(1 << 1) /* Call event, such as state change, etc */
#define EVENT_FLAG_LOG			(1 << 2) /* Log events */
#define EVENT_FLAG_VERBOSE		(1 << 3) /* Verbose messages */
#define EVENT_FLAG_COMMAND		(1 << 4) /* Ability to read/set commands */
#define EVENT_FLAG_AGENT		(1 << 5) /* Ability to read/set agent info */
#define EVENT_FLAG_USER                 (1 << 6) /* Ability to read/set user info */
#define EVENT_FLAG_CONFIG		(1 << 7) /* Ability to modify configurations */

/* Export manager structures */
#define AST_MAX_MANHEADERS 128

struct mansession;

struct message {
	unsigned int hdrcount;
	const char *headers[AST_MAX_MANHEADERS];
};

struct manager_action {
	/*! Name of the action */
	const char *action;
	/*! Short description of the action */
	const char *synopsis;
	/*! Detailed description of the action */
	const char *description;
	/*! Permission required for action.  EVENT_FLAG_* */
	int authority;
	/*! Function to be called */
	int (*func)(struct mansession *s, const struct message *m);
	/*! For easy linking */
	struct manager_action *next;
};

/* External routines may register/unregister manager callbacks this way */
#define ast_manager_register(a, b, c, d) ast_manager_register2(a, b, c, d, NULL)

/* Use ast_manager_register2 to register with help text for new manager commands */

/*! Register a manager command with the manager interface */
/*! 	\param action Name of the requested Action:
	\param authority Required authority for this command
	\param func Function to call for this command
	\param synopsis Help text (one line, up to 30 chars) for CLI manager show commands
	\param description Help text, several lines
*/
int ast_manager_register2(
	const char *action,
	int authority,
	int (*func)(struct mansession *s, const struct message *m),
	const char *synopsis,
	const char *description);

/*! Unregister a registered manager command */
/*!	\param action Name of registered Action:
*/
int ast_manager_unregister( char *action );

/*! 
 * \brief Verify a session's read permissions against a permission mask.  
 * \param ident session identity
 * \param perm permission mask to verify
 * \returns 1 if the session has the permission mask capabilities, otherwise 0
 */
int astman_verify_session_readpermissions(uint32_t ident, int perm);

/*!
 * \brief Verify a session's write permissions against a permission mask.  
 * \param ident session identity
 * \param perm permission mask to verify
 * \returns 1 if the session has the permission mask capabilities, otherwise 0
 */
int astman_verify_session_writepermissions(uint32_t ident, int perm);

/*! External routines may send asterisk manager events this way */
/*! 	\param category	Event category, matches manager authorization
	\param event	Event name
	\param contents	Contents of event
*/
int __attribute__((format(printf, 3,4))) manager_event(int category, const char *event, const char *contents, ...);

/*! Get header from mananger transaction */
const char *astman_get_header(const struct message *m, char *var);

/*! Get a linked list of the Variable: headers */
struct ast_variable *astman_get_variables(const struct message *m);

/*! Send error in manager transaction */
void astman_send_error(struct mansession *s, const struct message *m, char *error);
void astman_send_response(struct mansession *s, const struct message *m, char *resp, char *msg);
void astman_send_ack(struct mansession *s, const struct message *m, char *msg);

void __attribute__((format(printf, 2, 3))) astman_append(struct mansession *s, const char *fmt, ...);

/*! Called by Asterisk initialization */
int init_manager(void);
int reload_manager(void);

#endif /* _ASTERISK_MANAGER_H */
