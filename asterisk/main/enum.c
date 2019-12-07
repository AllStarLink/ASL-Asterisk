/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 *
 * Funding provided by nic.at
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
 *
 * \brief ENUM Support for Asterisk
 *
 * \author Mark Spencer <markster@digium.com>
 *
 * \arg Funding provided by nic.at
 *
 * \par Enum standards
 *
 * - NAPTR records: http://ietf.nri.reston.va.us/rfc/rfc2915.txt
 * - DNS SRV records: http://www.ietf.org/rfc/rfc2782.txt
 * - ENUM http://www.ietf.org/rfc/rfc3761.txt
 * - ENUM for H.323: http://www.ietf.org/rfc/rfc3762.txt
 * - ENUM SIP: http://www.ietf.org/rfc/rfc3764.txt
 * - IANA ENUM Services: http://www.iana.org/assignments/enum-services
 *
 * \par Possible improvement
 * \todo Implement a caching mechanism for multile enum lookups
 * - See http://bugs.digium.com/view.php?id=6739
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 180532 $")

#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/nameser.h>
#if __APPLE_CC__ >= 1495
#include <arpa/nameser_compat.h>
#endif
#include <resolv.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <regex.h>
#include <unistd.h>
#include <errno.h>

#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/enum.h"
#include "asterisk/dns.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/utils.h"

#ifdef __APPLE__
#undef T_NAPTR
#define T_NAPTR 35
#endif

#ifdef __APPLE__
#undef T_TXT
#define T_TXT 16
#endif

#define TOPLEV "e164.arpa."	/*!< The IETF Enum standard root, managed by the ITU */

/* Linked list from config file */
static struct enum_search {
	char toplev[512];
	struct enum_search *next;
} *toplevs;

static int enumver;

AST_MUTEX_DEFINE_STATIC(enumlock);

struct naptr {
	unsigned short order;
	unsigned short pref;
} __attribute__((__packed__));

/*! \brief Parse NAPTR record information elements */
static unsigned int parse_ie(char *data, unsigned int maxdatalen, unsigned char *src, unsigned int srclen)
{
	unsigned int len, olen;

	len = olen = (unsigned int) src[0];
	src++;
	srclen--;

	if (len > srclen) {
		ast_log(LOG_WARNING, "ENUM parsing failed: Wanted %d characters, got %d\n", len, srclen);
		return -1;
	}

	if (len > maxdatalen)
		len = maxdatalen;
	memcpy(data, src, len);

	return olen + 1;
}

/*! \brief Parse DNS NAPTR record used in ENUM ---*/
static int parse_naptr(char *dst, int dstsize, char *tech, int techsize, unsigned char *answer, int len, char *naptrinput)
{
	char tech_return[80];
	unsigned char *oanswer = answer;
	char flags[512] = "";
	char services[512] = "";
	char *p;
	char regexp[512] = "";
	char repl[512] = "";
	char tempdst[512] = "";
	char errbuff[512] = "";
	char delim;
	char *delim2;
	char *pattern, *subst, *d;
	int res;
	int regexp_len, rc;
	int size, matchindex; /* size is the size of the backreference sub. */
	int d_len = sizeof(tempdst) - 1;
	static const int max_bt = 10; /* max num of regexp backreference allowed, must remain 10 to guarantee a valid backreference index */
	regex_t preg;
	regmatch_t pmatch[max_bt];

	tech_return[0] = '\0';

	dst[0] = '\0';

	if (len < sizeof(struct naptr)) {
		ast_log(LOG_WARNING, "NAPTR record length too short\n");
		return -1;
	}
	answer += sizeof(struct naptr);
	len -= sizeof(struct naptr);
	if ((res = parse_ie(flags, sizeof(flags) - 1, answer, len)) < 0) {
		ast_log(LOG_WARNING, "Failed to get flags from NAPTR record\n");
		return -1;
	} else {
		answer += res;
		len -= res;
	}
	if ((res = parse_ie(services, sizeof(services) - 1, answer, len)) < 0) {
		ast_log(LOG_WARNING, "Failed to get services from NAPTR record\n");
		return -1;
	} else {
		answer += res;
		len -= res;
	}
	if ((res = parse_ie(regexp, sizeof(regexp) - 1, answer, len)) < 0) {
		ast_log(LOG_WARNING, "Failed to get regexp from NAPTR record\n");
		return -1;
	} else {
		answer += res;
		len -= res;
	}

	if ((res = dn_expand(oanswer, answer + len, answer, repl, sizeof(repl) - 1)) < 0) {
		ast_log(LOG_WARNING, "Failed to expand hostname\n");
		return -1;
	}

	if (option_debug > 2)	/* Advanced NAPTR debugging */
		ast_log(LOG_DEBUG, "NAPTR input='%s', flags='%s', services='%s', regexp='%s', repl='%s'\n",
			naptrinput, flags, services, regexp, repl);

	if (tolower(flags[0]) != 'u') {
		ast_log(LOG_WARNING, "NAPTR Flag must be 'U' or 'u'.\n");
		return -1;
	}

	p = strstr(services, "e2u+");
	if (p == NULL)
		p = strstr(services, "E2U+");
	if (p){
		p = p + 4;
		if (strchr(p, ':')){
			p = strchr(p, ':') + 1;
		}
		ast_copy_string(tech_return, p, sizeof(tech_return));
	} else {

		p = strstr(services, "+e2u");
		if (p == NULL)
			p = strstr(services, "+E2U");
		if (p) {
			*p = 0;
			p = strchr(services, ':');
			if (p)
				*p = 0;
			ast_copy_string(tech_return, services, sizeof(tech_return));
		}
	}

	regexp_len = strlen(regexp);
	if (regexp_len < 7) {
		ast_log(LOG_WARNING, "Regex too short to be meaningful.\n");
		return -1;
	}

	/* this takes the first character of the regexp (which is a delimiter) 
	 * and uses that character to find the index of the second delimiter */
	delim = regexp[0];
	delim2 = strchr(regexp + 1, delim);
	if ((delim2 == NULL) || (regexp[regexp_len - 1] != delim)) {  /* is the second delimiter found, and is the end of the regexp a delimiter */
		ast_log(LOG_WARNING, "Regex delimiter error (on \"%s\").\n", regexp);
		return -1;
	} else if (strchr((delim2 + 1), delim) == NULL) { /* if the second delimiter is found, make sure there is a third instance.  this could be the end one instead of the middle */
		ast_log(LOG_WARNING, "Regex delimiter error (on \"%s\").\n", regexp);
		return -1;
	}
	pattern = regexp + 1;   /* pattern is the regex without the begining and ending delimiter */
	*delim2 = 0;    /* zero out the middle delimiter */
	subst   = delim2 + 1; /* dst substring is everything after the second delimiter. */
	regexp[regexp_len - 1] = 0; /* zero out the last delimiter */

/*
 * now do the regex wizardry.
 */

	if (regcomp(&preg, pattern, REG_EXTENDED | REG_NEWLINE)) {
		ast_log(LOG_WARNING, "NAPTR Regex compilation error (regex = \"%s\").\n", regexp);
		return -1;
	}

	if (preg.re_nsub > ARRAY_LEN(pmatch)) {
		ast_log(LOG_WARNING, "NAPTR Regex compilation error: too many subs.\n");
		regfree(&preg);
		return -1;
	}
	/* pmatch is an array containing the substring indexes for the regex backreference sub.
	 * max_bt is the maximum number of backreferences allowed to be stored in pmatch */
	if ((rc = regexec(&preg, (char *) naptrinput, max_bt, pmatch, 0))) {
		regerror(rc, &preg, errbuff, sizeof(errbuff));
		ast_log(LOG_WARNING, "NAPTR Regex match failed. Reason: %s\n", errbuff);
		regfree(&preg);
		return -1;
	}
	regfree(&preg);

	d = tempdst;
	d_len--;

	/* perform the backreference sub. Search the subst for backreferences,
	 * when a backreference is found, retrieve the backreferences number.
	 * use the backreference number as an index for pmatch to retrieve the
	 * beginning and ending indexes of the substring to insert as the backreference.
	 * if no backreference is found, continue copying the subst into tempdst */
	while (*subst && (d_len > 0)) {
		if ((subst[0] == '\\') && isdigit(subst[1])) { /* is this character the beginning of a backreference */
			matchindex = (int) (subst[1] - '0');
			if (matchindex >= ARRAY_LEN(pmatch)) {
				ast_log(LOG_WARNING, "Error during regex substitution. Invalid pmatch index.\n");
				return -1;
			}
			/* pmatch len is 10. we are garanteed a single char 0-9 is a valid index */
			size = pmatch[matchindex].rm_eo - pmatch[matchindex].rm_so;
			if (size > d_len) {
				ast_log(LOG_WARNING, "Not enough space during NAPTR regex substitution.\n");
				return -1;
			}
			/* are the pmatch indexes valid for the input length */
			if ((strlen((char *) naptrinput) >= pmatch[matchindex].rm_eo) && (pmatch[matchindex].rm_so <= pmatch[matchindex].rm_eo)) {
				memcpy(d, (naptrinput + (int) pmatch[matchindex].rm_so), size);  /* copy input substring into backreference marker */
				d_len -= size;
				subst += 2;  /* skip over backreference characters to next valid character */
				d += size;
			} else {
				ast_log(LOG_WARNING, "Error during regex substitution. Invalid backreference index.\n");
				return -1;
			}
		} else if (isprint(*subst)) {
			*d++ = *subst++;
			d_len--;
		} else {
			ast_log(LOG_WARNING, "Error during regex substitution.\n");
			return -1;
		}
	}
	*d = 0;
	ast_copy_string((char *) dst, tempdst, dstsize);
	dst[dstsize - 1] = '\0';
	if (*tech != '\0'){ /* check if it is requested NAPTR */
		if (!strncasecmp(tech, "ALL", techsize)){
			return 1; /* return or count any RR */
		}
		if (!strncasecmp(tech_return, tech, sizeof(tech_return)<techsize?sizeof(tech_return):techsize)){
			ast_copy_string(tech, tech_return, techsize);
			return 1; /* we got out RR */
		} else { /* go to the next RR in the DNS answer */
			return 0;
		}
	}

	/* tech was not specified, return first parsed RR */
	ast_copy_string(tech, tech_return, techsize);
	return 1;
}

/* do not return requested value, just count RRs and return thei number in dst */
#define ENUMLOOKUP_OPTIONS_COUNT       1

struct enum_naptr_rr {
	struct naptr naptr; /* order and preference of RR */
	char *result; /* result of naptr parsing,e.g.: tel:+5553 */
	char *tech; /* Technology (from URL scheme) */
	int sort_pos; /* sort position */
};

struct enum_context {
	char *dst;	/* Destination part of URL from ENUM */
	int dstlen;	/* Length */
	char *tech;	/* Technology (from URL scheme) */
	int techlen;	/* Length */
	char *txt;	/* TXT record in TXT lookup */
	int txtlen;	/* Length */
	char *naptrinput;	/* The number to lookup */
	int position; /* used as counter for RRs or specifies position of required RR */
	int options; /* options , see ENUMLOOKUP_OPTIONS_* defined above */
	struct enum_naptr_rr *naptr_rrs; /* array of parsed NAPTR RRs */
	int naptr_rrs_count; /* Size of array naptr_rrs */
};

/*! \brief Callback for TXT record lookup */
static int txt_callback(void *context, unsigned char *answer, int len, unsigned char *fullanswer)
{
	struct enum_context *c = (struct enum_context *)context;

	if (answer == NULL) {
		c->txt = NULL;
		c->txtlen = 0;
		return 0;
	}

	/* skip over first byte, as for some reason it's a vertical tab character */
	answer += 1;
	len -= 1;

	/* answer is not null-terminated, but should be */
	/* this is safe to do, as answer has extra bytes on the end we can
	 * safely overwrite with a null */
	answer[len] = '\0';
	/* now increment len so that len includes the null, so that we can
	 * compare apples to apples */
	len +=1;

	/* finally, copy the answer into c->txt */
	ast_copy_string(c->txt, (const char *) answer, len < c->txtlen ? len : (c->txtlen));

	/* just to be safe, let's make sure c->txt is null terminated */
	c->txt[(c->txtlen)-1] = '\0';

	return 1;
}

/*! \brief Callback from ENUM lookup function */
static int enum_callback(void *context, unsigned char *answer, int len, unsigned char *fullanswer)
{
	struct enum_context *c = context;
	void *p = NULL;
	int res;

	res = parse_naptr(c->dst, c->dstlen, c->tech, c->techlen, answer, len, c->naptrinput);

	if (res < 0) {
		ast_log(LOG_WARNING, "Failed to parse naptr :(\n");
		return -1;
	} else if (res > 0 && !ast_strlen_zero(c->dst)){ /* ok, we got needed NAPTR */
		if (c->options & ENUMLOOKUP_OPTIONS_COUNT){ /* counting RRs */
			c->position++;
			snprintf(c->dst, c->dstlen, "%d", c->position);
		} else  {
			if ((p = ast_realloc(c->naptr_rrs, sizeof(*c->naptr_rrs) * (c->naptr_rrs_count + 1)))) {
				c->naptr_rrs = p;
				memcpy(&c->naptr_rrs[c->naptr_rrs_count].naptr, answer, sizeof(c->naptr_rrs->naptr));
				c->naptr_rrs[c->naptr_rrs_count].result = strdup(c->dst);
				c->naptr_rrs[c->naptr_rrs_count].tech = strdup(c->tech);
				c->naptr_rrs[c->naptr_rrs_count].sort_pos = c->naptr_rrs_count;
				c->naptr_rrs_count++;
			}
			c->dst[0] = 0;
		}
		return 0;
	}

	if (c->options & ENUMLOOKUP_OPTIONS_COUNT) 	{ /* counting RRs */
		snprintf(c->dst, c->dstlen, "%d", c->position);
	}

	return 0;
}

/*! \brief ENUM lookup */
int ast_get_enum(struct ast_channel *chan, const char *number, char *dst, int dstlen, char *tech, int techlen, char* suffix, char* options, unsigned int record)
{
	struct enum_context context;
	char tmp[259 + 512];
	char naptrinput[512];
	int pos = strlen(number) - 1;
	int newpos = 0;
	int ret = -1;
	struct enum_search *s = NULL;
	int version = -1;
	/* for ISN rewrite */
	char *p1 = NULL;
	char *p2 = NULL;
	int k = 0;
	int i = 0;
	int z = 0;

	ast_copy_string(naptrinput, number[0] == 'n' ? number+1 : number, sizeof(naptrinput));

	context.naptrinput = naptrinput;	/* The number */
	context.dst = dst;			/* Return string */
	context.dstlen = dstlen;
	context.tech = tech;
	context.techlen = techlen;
	context.options = 0;
	context.position = record > 0 ? record : 1;
	context.naptr_rrs = NULL;
	context.naptr_rrs_count = 0;

	if (options != NULL) {
		if (*options == 'c') {
			context.options = ENUMLOOKUP_OPTIONS_COUNT;
			context.position = 0;
		}
	}

	ast_log(LOG_DEBUG, "ast_get_enum(): n='%s', tech='%s', suffix='%s', options='%d', record='%d'\n",
			number, tech, suffix, context.options, context.position);

	if (pos > 128)
		pos = 128;

	/* ISN rewrite */
	p1 = strchr(number, '*');

	if (number[0] == 'n') { /* do not perform ISN rewrite ('n' is testing flag) */
		p1 = NULL;
		k = 1; /* strip 'n' from number */
	}

	if (p1 != NULL) {
		p2 = p1+1;
		while (p1 > number){
			p1--;
			tmp[newpos++] = *p1;
			tmp[newpos++] = '.';
		}
		if (*p2) {
			while(*p2 && newpos < 128){
				tmp[newpos++] = *p2;
				p2++;
			}
			tmp[newpos++] = '.';
		}

	} else {
		while (pos >= k) {
			if (isdigit(number[pos])) {
				tmp[newpos++] = number[pos];
				tmp[newpos++] = '.';
			}
			pos--;
		}
	}

	if (chan && ast_autoservice_start(chan) < 0)
		return -1;

	if(suffix) {
		ast_copy_string(tmp + newpos, suffix, sizeof(tmp) - newpos);
		ret = ast_search_dns(&context, tmp, C_IN, T_NAPTR, enum_callback);
		ast_log(LOG_DEBUG, "ast_get_enum: ast_search_dns(%s) returned %d\n", tmp, ret);
	} else {
		ret = -1;		/* this is actually dead code since the demise of app_enum.c */
		for (;;) {
			ast_mutex_lock(&enumlock);
			if (version != enumver) {
				/* Ooh, a reload... */
				s = toplevs;
				version = enumver;
			} else {
				s = s->next;
			}
			ast_mutex_unlock(&enumlock);

			if (!s)
				break;
	
			ast_copy_string(tmp + newpos, s->toplev, sizeof(tmp) - newpos);
			ret = ast_search_dns(&context, tmp, C_IN, T_NAPTR, enum_callback);
			ast_log(LOG_DEBUG, "ast_get_enum: ast_search_dns(%s) returned %d\n", tmp, ret);
			if (ret > 0)
				break;
		}
	}

	if (ret < 0) {
		ast_log(LOG_DEBUG, "No such number found: %s (%s)\n", tmp, strerror(errno));
		strcpy(dst, "0");
		ret = 0;
	}

	if (context.naptr_rrs_count >= context.position && ! (context.options & ENUMLOOKUP_OPTIONS_COUNT)) {
		/* sort array by NAPTR order/preference */
		for (k = 0; k < context.naptr_rrs_count; k++) {
			for (i = 0; i < context.naptr_rrs_count; i++) {
				/* use order first and then preference to compare */
				if ((ntohs(context.naptr_rrs[k].naptr.order) < ntohs(context.naptr_rrs[i].naptr.order)
						&& context.naptr_rrs[k].sort_pos > context.naptr_rrs[i].sort_pos)
					|| (ntohs(context.naptr_rrs[k].naptr.order) > ntohs(context.naptr_rrs[i].naptr.order)
						&& context.naptr_rrs[k].sort_pos < context.naptr_rrs[i].sort_pos)){
					z = context.naptr_rrs[k].sort_pos;
					context.naptr_rrs[k].sort_pos = context.naptr_rrs[i].sort_pos;
					context.naptr_rrs[i].sort_pos = z;
					continue;
				}
				if (ntohs(context.naptr_rrs[k].naptr.order) == ntohs(context.naptr_rrs[i].naptr.order)) {
					if ((ntohs(context.naptr_rrs[k].naptr.pref) < ntohs(context.naptr_rrs[i].naptr.pref)
							&& context.naptr_rrs[k].sort_pos > context.naptr_rrs[i].sort_pos)
						|| (ntohs(context.naptr_rrs[k].naptr.pref) > ntohs(context.naptr_rrs[i].naptr.pref)
							&& context.naptr_rrs[k].sort_pos < context.naptr_rrs[i].sort_pos)){
						z = context.naptr_rrs[k].sort_pos;
						context.naptr_rrs[k].sort_pos = context.naptr_rrs[i].sort_pos;
						context.naptr_rrs[i].sort_pos = z;
					}
				}
			}
		}
		for (k = 0; k < context.naptr_rrs_count; k++) {
			if (context.naptr_rrs[k].sort_pos == context.position-1) {
				ast_copy_string(context.dst, context.naptr_rrs[k].result, dstlen);
				ast_copy_string(context.tech, context.naptr_rrs[k].tech, techlen);
				break;
			}
		}
	} else if (!(context.options & ENUMLOOKUP_OPTIONS_COUNT)) {
		context.dst[0] = 0;
	}
	if (chan)
		ret |= ast_autoservice_stop(chan);

	for (k = 0; k < context.naptr_rrs_count; k++) {
		free(context.naptr_rrs[k].result);
		free(context.naptr_rrs[k].tech);
	}

	free(context.naptr_rrs);

	return ret;
}

/*! \brief Get TXT record from DNS.
	Really has nothing to do with enum, but anyway...
 */
int ast_get_txt(struct ast_channel *chan, const char *number, char *dst, int dstlen, char *tech, int techlen, char *txt, int txtlen)
{
	struct enum_context context;
	char tmp[259 + 512];
	char naptrinput[512] = "+";
	int pos = strlen(number) - 1;
	int newpos = 0;
	int ret = -1;
	struct enum_search *s = NULL;
	int version = -1;

	strncat(naptrinput, number, sizeof(naptrinput) - 2);

	context.naptrinput = naptrinput;
	context.dst = dst;
	context.dstlen = dstlen;
	context.tech = tech;
	context.techlen = techlen;
	context.txt = txt;
	context.txtlen = txtlen;

	if (pos > 128)
		pos = 128;
	while (pos >= 0) {
		tmp[newpos++] = number[pos--];
		tmp[newpos++] = '.';
	}

	if (chan && ast_autoservice_start(chan) < 0)
		return -1;

	for (;;) {
		ast_mutex_lock(&enumlock);
		if (version != enumver) {
			/* Ooh, a reload... */
			s = toplevs;
			version = enumver;
		} else {
			s = s->next;
		}
		if (s) {
			ast_copy_string(tmp + newpos, s->toplev, sizeof(tmp) - newpos);
		}
		ast_mutex_unlock(&enumlock);
		if (!s)
			break;

		ret = ast_search_dns(&context, tmp, C_IN, T_TXT, txt_callback);
		if (ret > 0)
			break;
	}
	if (ret < 0) {
		if (option_debug > 1)
			ast_log(LOG_DEBUG, "No such number found in ENUM: %s (%s)\n", tmp, strerror(errno));
		ret = 0;
	}
	if (chan)
		ret |= ast_autoservice_stop(chan);
	return ret;
}

/*! \brief Add enum tree to linked list */
static struct enum_search *enum_newtoplev(char *s)
{
	struct enum_search *tmp;

	if ((tmp = ast_calloc(1, sizeof(*tmp)))) {		
		ast_copy_string(tmp->toplev, s, sizeof(tmp->toplev));
	}
	return tmp;
}

/*! \brief Initialize the ENUM support subsystem */
int ast_enum_init(void)
{
	struct ast_config *cfg;
	struct enum_search *s, *sl;
	struct ast_variable *v;

	/* Destroy existing list */
	ast_mutex_lock(&enumlock);
	s = toplevs;
	while(s) {
		sl = s;
		s = s->next;
		free(sl);
	}
	toplevs = NULL;
	cfg = ast_config_load("enum.conf");
	if (cfg) {
		sl = NULL;
		v = ast_variable_browse(cfg, "general");
		while(v) {
			if (!strcasecmp(v->name, "search")) {
				s = enum_newtoplev(v->value);
				if (s) {
					if (sl)
						sl->next = s;
					else
						toplevs = s;
					sl = s;
				}
			}
			v = v->next;
		}
		ast_config_destroy(cfg);
	} else {
		toplevs = enum_newtoplev(TOPLEV);
	}
	enumver++;
	ast_mutex_unlock(&enumlock);
	return 0;
}

int ast_enum_reload(void)
{
	return ast_enum_init();
}
