/*
 * Copyright (C) 2006 Voop as
 * Thorsten Lockert <tholo@voop.as>
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*! \file
 *
 * \brief SNMP Agent / SubAgent support for Asterisk
 *
 * \author Thorsten Lockert <tholo@voop.as>
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 174148 $")

#include <net-snmp/net-snmp-config.h>
#include <net-snmp/net-snmp-includes.h>
#include <net-snmp/agent/net-snmp-agent-includes.h>

#include "asterisk/channel.h"
#include "asterisk/logger.h"
#include "asterisk/options.h"
#include "asterisk/indications.h"
#include "asterisk/version.h"
#include "asterisk/pbx.h"

/* Colission between Net-SNMP and Asterisk */
#define unload_module ast_unload_module
#include "asterisk/module.h"
#undef unload_module

#include "agent.h"

/* Helper functions in Net-SNMP, header file not installed by default */
int header_generic(struct variable *, oid *, size_t *, int, size_t *, WriteMethod **);
int header_simple_table(struct variable *, oid *, size_t *, int, size_t *, WriteMethod **, int);
int register_sysORTable(oid *, size_t, const char *);
int unregister_sysORTable(oid *, size_t);

/* Not defined in header files */
extern char ast_config_AST_SOCKET[];

/* Forward declaration */
static void init_asterisk_mib(void);

/*
 * Anchor for all the Asterisk MIB values
 */
static oid asterisk_oid[] = { 1, 3, 6, 1, 4, 1, 22736, 1 };

/*
 * MIB values -- these correspond to values in the Asterisk MIB,
 * and MUST be kept in sync with the MIB for things to work as
 * expected.
 */
#define ASTVERSION				1
#define		ASTVERSTRING			1
#define		ASTVERTAG				2

#define	ASTCONFIGURATION		2
#define		ASTCONFUPTIME			1
#define		ASTCONFRELOADTIME		2
#define		ASTCONFPID				3
#define		ASTCONFSOCKET			4

#define	ASTMODULES				3
#define		ASTMODCOUNT				1

#define	ASTINDICATIONS			4
#define		ASTINDCOUNT				1
#define		ASTINDCURRENT			2

#define		ASTINDTABLE				3
#define			ASTINDINDEX				1
#define			ASTINDCOUNTRY			2
#define			ASTINDALIAS				3
#define			ASTINDDESCRIPTION		4

#define	ASTCHANNELS				5
#define		ASTCHANCOUNT			1

#define		ASTCHANTABLE			2
#define			ASTCHANINDEX			1
#define			ASTCHANNAME				2
#define			ASTCHANLANGUAGE			3
#define			ASTCHANTYPE				4
#define			ASTCHANMUSICCLASS		5
#define			ASTCHANBRIDGE			6
#define			ASTCHANMASQ				7
#define			ASTCHANMASQR			8
#define			ASTCHANWHENHANGUP		9
#define			ASTCHANAPP				10
#define			ASTCHANDATA				11
#define			ASTCHANCONTEXT			12
#define			ASTCHANMACROCONTEXT		13
#define			ASTCHANMACROEXTEN		14
#define			ASTCHANMACROPRI			15
#define			ASTCHANEXTEN			16
#define			ASTCHANPRI				17
#define			ASTCHANACCOUNTCODE		18
#define			ASTCHANFORWARDTO		19
#define			ASTCHANUNIQUEID			20
#define			ASTCHANCALLGROUP		21
#define			ASTCHANPICKUPGROUP		22
#define			ASTCHANSTATE			23
#define			ASTCHANMUTED			24
#define			ASTCHANRINGS			25
#define			ASTCHANCIDDNID			26
#define			ASTCHANCIDNUM			27
#define			ASTCHANCIDNAME			28
#define			ASTCHANCIDANI			29
#define			ASTCHANCIDRDNIS			30
#define			ASTCHANCIDPRES			31
#define			ASTCHANCIDANI2			32
#define			ASTCHANCIDTON			33
#define			ASTCHANCIDTNS			34
#define			ASTCHANAMAFLAGS			35
#define			ASTCHANADSI				36
#define			ASTCHANTONEZONE			37
#define			ASTCHANHANGUPCAUSE		38
#define			ASTCHANVARIABLES		39
#define			ASTCHANFLAGS			40
#define			ASTCHANTRANSFERCAP		41

#define		ASTCHANTYPECOUNT		3

#define		ASTCHANTYPETABLE		4
#define			ASTCHANTYPEINDEX		1
#define			ASTCHANTYPENAME			2
#define			ASTCHANTYPEDESC			3
#define			ASTCHANTYPEDEVSTATE		4
#define			ASTCHANTYPEINDICATIONS	5
#define			ASTCHANTYPETRANSFER		6
#define			ASTCHANTYPECHANNELS		7

void *agent_thread(void *arg)
{
    ast_verbose(VERBOSE_PREFIX_2 "Starting %sAgent\n", res_snmp_agentx_subagent ? "Sub" : "");

    snmp_enable_stderrlog();

    if (res_snmp_agentx_subagent)
		netsnmp_ds_set_boolean(NETSNMP_DS_APPLICATION_ID,
							   NETSNMP_DS_AGENT_ROLE,
							   1);

    init_agent("asterisk");

    init_asterisk_mib();

    init_snmp("asterisk");

    if (!res_snmp_agentx_subagent)
		init_master_agent();

    while (res_snmp_dont_stop)
		agent_check_and_process(1);

    snmp_shutdown("asterisk");

    ast_verbose(VERBOSE_PREFIX_2 "Terminating %sAgent\n",
				res_snmp_agentx_subagent ? "Sub" : "");

    return NULL;
}

static u_char *
ast_var_channels(struct variable *vp, oid *name, size_t *length,
				 int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;

    if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

    switch (vp->magic) {
	case ASTCHANCOUNT:
		long_ret = ast_active_channels();
		return (u_char *)&long_ret;
	default:
		break;
    }
    return NULL;
}

static u_char *ast_var_channels_table(struct variable *vp, oid *name, size_t *length,
									int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    static u_char bits_ret[2];
    static char string_ret[256];
    struct ast_channel *chan, *bridge;
    struct timeval tval;
    u_char *ret;
    int i, bit;

    if (header_simple_table(vp, name, length, exact, var_len, write_method, ast_active_channels()))
		return NULL;

    i = name[*length - 1] - 1;
    for (chan = ast_channel_walk_locked(NULL);
		 chan && i;
		 chan = ast_channel_walk_locked(chan), i--)
		ast_channel_unlock(chan);
    if (chan == NULL)
		return NULL;
	*var_len = sizeof(long_ret);

    switch (vp->magic) {
	case ASTCHANINDEX:
		long_ret = name[*length - 1];
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANNAME:
		if (!ast_strlen_zero(chan->name)) {
			strncpy(string_ret, chan->name, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANLANGUAGE:
		if (!ast_strlen_zero(chan->language)) {
			strncpy(string_ret, chan->language, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANTYPE:
		strncpy(string_ret, chan->tech->type, sizeof(string_ret));
		string_ret[sizeof(string_ret) - 1] = '\0';
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANMUSICCLASS:
		if (!ast_strlen_zero(chan->musicclass)) {
			strncpy(string_ret, chan->musicclass, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANBRIDGE:
		if ((bridge = ast_bridged_channel(chan)) != NULL) {
			strncpy(string_ret, bridge->name, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANMASQ:
		if (chan->masq && !ast_strlen_zero(chan->masq->name)) {
			strncpy(string_ret, chan->masq->name, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANMASQR:
		if (chan->masqr && !ast_strlen_zero(chan->masqr->name)) {
			strncpy(string_ret, chan->masqr->name, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANWHENHANGUP:
		if (chan->whentohangup) {
			gettimeofday(&tval, NULL);
			long_ret = difftime(chan->whentohangup, tval.tv_sec) * 100 - tval.tv_usec / 10000;
			ret= (u_char *)&long_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANAPP:
		if (chan->appl) {
			strncpy(string_ret, chan->appl, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANDATA:
		if (chan->data) {
			strncpy(string_ret, chan->data, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANCONTEXT:
		strncpy(string_ret, chan->context, sizeof(string_ret));
		string_ret[sizeof(string_ret) - 1] = '\0';
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANMACROCONTEXT:
		strncpy(string_ret, chan->macrocontext, sizeof(string_ret));
		string_ret[sizeof(string_ret) - 1] = '\0';
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANMACROEXTEN:
		strncpy(string_ret, chan->macroexten, sizeof(string_ret));
		string_ret[sizeof(string_ret) - 1] = '\0';
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANMACROPRI:
		long_ret = chan->macropriority;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANEXTEN:
		strncpy(string_ret, chan->exten, sizeof(string_ret));
		string_ret[sizeof(string_ret) - 1] = '\0';
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANPRI:
		long_ret = chan->priority;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANACCOUNTCODE:
		if (!ast_strlen_zero(chan->accountcode)) {
			strncpy(string_ret, chan->accountcode, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANFORWARDTO:
		if (!ast_strlen_zero(chan->call_forward)) {
			strncpy(string_ret, chan->call_forward, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANUNIQUEID:
		strncpy(string_ret, chan->uniqueid, sizeof(string_ret));
		string_ret[sizeof(string_ret) - 1] = '\0';
		*var_len = strlen(string_ret);
		ret = (u_char *)string_ret;
		break;
	case ASTCHANCALLGROUP:
		long_ret = chan->callgroup;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANPICKUPGROUP:
		long_ret = chan->pickupgroup;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANSTATE:
		long_ret = chan->_state & 0xffff;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANMUTED:
		long_ret = chan->_state & AST_STATE_MUTE ? 1 : 2;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANRINGS:
		long_ret = chan->rings;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANCIDDNID:
		if (chan->cid.cid_dnid) {
			strncpy(string_ret, chan->cid.cid_dnid, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANCIDNUM:
		if (chan->cid.cid_num) {
			strncpy(string_ret, chan->cid.cid_num, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANCIDNAME:
		if (chan->cid.cid_name) {
			strncpy(string_ret, chan->cid.cid_name, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANCIDANI:
		if (chan->cid.cid_ani) {
			strncpy(string_ret, chan->cid.cid_ani, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANCIDRDNIS:
		if (chan->cid.cid_rdnis) {
			strncpy(string_ret, chan->cid.cid_rdnis, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANCIDPRES:
		long_ret = chan->cid.cid_pres;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANCIDANI2:
		long_ret = chan->cid.cid_ani2;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANCIDTON:
		long_ret = chan->cid.cid_ton;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANCIDTNS:
		long_ret = chan->cid.cid_tns;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANAMAFLAGS:
		long_ret = chan->amaflags;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANADSI:
		long_ret = chan->adsicpe;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANTONEZONE:
		if (chan->zone) {
			strncpy(string_ret, chan->zone->country, sizeof(string_ret));
			string_ret[sizeof(string_ret) - 1] = '\0';
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANHANGUPCAUSE:
		long_ret = chan->hangupcause;
		ret = (u_char *)&long_ret;
		break;
	case ASTCHANVARIABLES:
		if (pbx_builtin_serialize_variables(chan, string_ret, sizeof(string_ret))) {
			*var_len = strlen(string_ret);
			ret = (u_char *)string_ret;
		}
		else
			ret = NULL;
		break;
	case ASTCHANFLAGS:
		bits_ret[0] = 0;
		for (bit = 0; bit < 8; bit++)
			bits_ret[0] |= ((chan->flags & (1 << bit)) >> bit) << (7 - bit);
		bits_ret[1] = 0;
		for (bit = 0; bit < 8; bit++)
			bits_ret[1] |= (((chan->flags >> 8) & (1 << bit)) >> bit) << (7 - bit);
		*var_len = 2;
		ret = bits_ret;
		break;
	case ASTCHANTRANSFERCAP:
		long_ret = chan->transfercapability;
		ret = (u_char *)&long_ret;
		break;
	default:
		ret = NULL;
		break;
    }
    ast_channel_unlock(chan);
    return ret;
}

static u_char *ast_var_channel_types(struct variable *vp, oid *name, size_t *length,
								   int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
	struct ast_variable *channel_types, *next;

    if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

    switch (vp->magic) {
	case ASTCHANTYPECOUNT:
		long_ret = 0;
		for (channel_types = next = ast_channeltype_list(); next; next = next->next) {
			long_ret++;
		}
		ast_variables_destroy(channel_types);
		return (u_char *)&long_ret;
	default:
		break;
    }
    return NULL;
}

static u_char *ast_var_channel_types_table(struct variable *vp, oid *name, size_t *length,
										int exact, size_t *var_len, WriteMethod **write_method)
{
	const struct ast_channel_tech *tech = NULL;
	struct ast_variable *channel_types, *next;
    static unsigned long long_ret;
    struct ast_channel *chan;
    u_long i;

    if (header_simple_table(vp, name, length, exact, var_len, write_method, -1))
		return NULL;

	channel_types = ast_channeltype_list();
	for (i = 1, next = channel_types; next && i != name[*length - 1]; next = next->next, i++)
		;
	if (next != NULL)
		tech = ast_get_channel_tech(next->name);
	ast_variables_destroy(channel_types);
    if (next == NULL || tech == NULL)
		return NULL;
    
    switch (vp->magic) {
	case ASTCHANTYPEINDEX:
		long_ret = name[*length - 1];
		return (u_char *)&long_ret;
	case ASTCHANTYPENAME:
		*var_len = strlen(tech->type);
		return (u_char *)tech->type;
	case ASTCHANTYPEDESC:
		*var_len = strlen(tech->description);
		return (u_char *)tech->description;
	case ASTCHANTYPEDEVSTATE:
		long_ret = tech->devicestate ? 1 : 2;
		return (u_char *)&long_ret;
	case ASTCHANTYPEINDICATIONS:
		long_ret = tech->indicate ? 1 : 2;
		return (u_char *)&long_ret;
	case ASTCHANTYPETRANSFER:
		long_ret = tech->transfer ? 1 : 2;
		return (u_char *)&long_ret;
	case ASTCHANTYPECHANNELS:
		long_ret = 0;
		for (chan = ast_channel_walk_locked(NULL); chan; chan = ast_channel_walk_locked(chan)) {
			if (chan->tech == tech)
				long_ret++;
			ast_channel_unlock(chan);
		}
		return (u_char *)&long_ret;
	default:
		break;
    }
    return NULL;
}

static u_char *ast_var_Config(struct variable *vp, oid *name, size_t *length,
							 int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct timeval tval;

    if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

    switch (vp->magic) {
	case ASTCONFUPTIME:
		gettimeofday(&tval, NULL);
		long_ret = difftime(tval.tv_sec, ast_startuptime) * 100 + tval.tv_usec / 10000;
		return (u_char *)&long_ret;
	case ASTCONFRELOADTIME:
		gettimeofday(&tval, NULL);
		if (ast_lastreloadtime)
			long_ret = difftime(tval.tv_sec, ast_lastreloadtime) * 100 + tval.tv_usec / 10000;
		else
			long_ret = difftime(tval.tv_sec, ast_startuptime) * 100 + tval.tv_usec / 10000;
		return (u_char *)&long_ret;
	case ASTCONFPID:
		long_ret = getpid();
		return (u_char *)&long_ret;
	case ASTCONFSOCKET:
		*var_len = strlen(ast_config_AST_SOCKET);
		return (u_char *)ast_config_AST_SOCKET;
	default:
		break;
    }
    return NULL;
}

static u_char *ast_var_indications(struct variable *vp, oid *name, size_t *length,
								  int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct tone_zone *tz = NULL;

    if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

    switch (vp->magic) {
	case ASTINDCOUNT:
		long_ret = 0;
		while ( (tz = ast_walk_indications(tz)) )
			long_ret++;

		return (u_char *)&long_ret;
	case ASTINDCURRENT:
		tz = ast_get_indication_zone(NULL);
		if (tz) {
			*var_len = strlen(tz->country);
			return (u_char *)tz->country;
		}
		*var_len = 0;
		return NULL;
	default:
		break;
    }
    return NULL;
}

static u_char *ast_var_indications_table(struct variable *vp, oid *name, size_t *length,
									   int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;
    struct tone_zone *tz = NULL;
    int i;

    if (header_simple_table(vp, name, length, exact, var_len, write_method, -1))
		return NULL;

    i = name[*length - 1] - 1;
    while ( (tz = ast_walk_indications(tz)) && i )
	i--;
    if (tz == NULL)
		return NULL;

    switch (vp->magic) {
	case ASTINDINDEX:
		long_ret = name[*length - 1];
		return (u_char *)&long_ret;
	case ASTINDCOUNTRY:
		*var_len = strlen(tz->country);
		return (u_char *)tz->country;
	case ASTINDALIAS:
		if (tz->alias) {
			*var_len = strlen(tz->alias);
			return (u_char *)tz->alias;
		}
		return NULL;
	case ASTINDDESCRIPTION:
		*var_len = strlen(tz->description);
		return (u_char *)tz->description;
	default:
		break;
    }
    return NULL;
}

static int countmodule(const char *mod, const char *desc, int use, const char *like)
{
    return 1;
}

static u_char *ast_var_Modules(struct variable *vp, oid *name, size_t *length,
							  int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;

    if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

    switch (vp->magic) {
	case ASTMODCOUNT:
		long_ret = ast_update_module_list(countmodule, NULL);
		return (u_char *)&long_ret;
	default:
		break;
    }
    return NULL;
}

static u_char *ast_var_Version(struct variable *vp, oid *name, size_t *length,
							  int exact, size_t *var_len, WriteMethod **write_method)
{
    static unsigned long long_ret;

    if (header_generic(vp, name, length, exact, var_len, write_method))
		return NULL;

    switch (vp->magic) {
	case ASTVERSTRING:
		*var_len = strlen(ASTERISK_VERSION);
		return (u_char *)ASTERISK_VERSION;
	case ASTVERTAG:
		long_ret = ASTERISK_VERSION_NUM;
		return (u_char *)&long_ret;
	default:
		break;
    }
    return NULL;
}

static int term_asterisk_mib(int majorID, int minorID, void *serverarg, void *clientarg)
{
    unregister_sysORTable(asterisk_oid, OID_LENGTH(asterisk_oid));
    return 0;
}

static void init_asterisk_mib(void)
{
    static struct variable4 asterisk_vars[] = {
		{ASTVERSTRING,           ASN_OCTET_STR, RONLY, ast_var_Version,             2, {ASTVERSION, ASTVERSTRING}},
		{ASTVERTAG,              ASN_UNSIGNED,  RONLY, ast_var_Version,             2, {ASTVERSION, ASTVERTAG}},
		{ASTCONFUPTIME,          ASN_TIMETICKS, RONLY, ast_var_Config,              2, {ASTCONFIGURATION, ASTCONFUPTIME}},
		{ASTCONFRELOADTIME,      ASN_TIMETICKS, RONLY, ast_var_Config,              2, {ASTCONFIGURATION, ASTCONFRELOADTIME}},
		{ASTCONFPID,             ASN_INTEGER,   RONLY, ast_var_Config,              2, {ASTCONFIGURATION, ASTCONFPID}},
		{ASTCONFSOCKET,          ASN_OCTET_STR, RONLY, ast_var_Config,              2, {ASTCONFIGURATION, ASTCONFSOCKET}},
		{ASTMODCOUNT,            ASN_INTEGER,   RONLY, ast_var_Modules ,            2, {ASTMODULES, ASTMODCOUNT}},
		{ASTINDCOUNT,            ASN_INTEGER,   RONLY, ast_var_indications,         2, {ASTINDICATIONS, ASTINDCOUNT}},
		{ASTINDCURRENT,          ASN_OCTET_STR, RONLY, ast_var_indications,         2, {ASTINDICATIONS, ASTINDCURRENT}},
		{ASTINDINDEX,            ASN_INTEGER,   RONLY, ast_var_indications_table,   4, {ASTINDICATIONS, ASTINDTABLE, 1, ASTINDINDEX}},
		{ASTINDCOUNTRY,          ASN_OCTET_STR, RONLY, ast_var_indications_table,   4, {ASTINDICATIONS, ASTINDTABLE, 1, ASTINDCOUNTRY}},
		{ASTINDALIAS,            ASN_OCTET_STR, RONLY, ast_var_indications_table,   4, {ASTINDICATIONS, ASTINDTABLE, 1, ASTINDALIAS}},
		{ASTINDDESCRIPTION,      ASN_OCTET_STR, RONLY, ast_var_indications_table,   4, {ASTINDICATIONS, ASTINDTABLE, 1, ASTINDDESCRIPTION}},
		{ASTCHANCOUNT,           ASN_GAUGE,     RONLY, ast_var_channels,            2, {ASTCHANNELS, ASTCHANCOUNT}},
		{ASTCHANINDEX,           ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANINDEX}},
		{ASTCHANNAME,            ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANNAME}},
		{ASTCHANLANGUAGE,        ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANLANGUAGE}},
		{ASTCHANTYPE,            ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANTYPE}},
		{ASTCHANMUSICCLASS,      ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMUSICCLASS}},
		{ASTCHANBRIDGE,          ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANBRIDGE}},
		{ASTCHANMASQ,            ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMASQ}},
		{ASTCHANMASQR,           ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMASQR}},
		{ASTCHANWHENHANGUP,      ASN_TIMETICKS, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANWHENHANGUP}},
		{ASTCHANAPP,             ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANAPP}},
		{ASTCHANDATA,            ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANDATA}},
		{ASTCHANCONTEXT,         ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCONTEXT}},
		{ASTCHANMACROCONTEXT,    ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMACROCONTEXT}},
		{ASTCHANMACROEXTEN,      ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMACROEXTEN}},
		{ASTCHANMACROPRI,        ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMACROPRI}},
		{ASTCHANEXTEN,           ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANEXTEN}},
		{ASTCHANPRI,             ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANPRI}},
		{ASTCHANACCOUNTCODE,     ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANACCOUNTCODE}},
		{ASTCHANFORWARDTO,       ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANFORWARDTO}},
		{ASTCHANUNIQUEID,        ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANUNIQUEID}},
		{ASTCHANCALLGROUP,       ASN_UNSIGNED,  RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCALLGROUP}},
		{ASTCHANPICKUPGROUP,     ASN_UNSIGNED,  RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANPICKUPGROUP}},
		{ASTCHANSTATE,           ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANSTATE}},
		{ASTCHANMUTED,           ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANMUTED}},
		{ASTCHANRINGS,           ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANRINGS}},
		{ASTCHANCIDDNID,         ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDDNID}},
		{ASTCHANCIDNUM,          ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDNUM}},
		{ASTCHANCIDNAME,         ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDNAME}},
		{ASTCHANCIDANI,          ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDANI}},
		{ASTCHANCIDRDNIS,        ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDRDNIS}},
		{ASTCHANCIDPRES,         ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDPRES}},
		{ASTCHANCIDANI2,         ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDANI2}},
		{ASTCHANCIDTON,          ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDTON}},
		{ASTCHANCIDTNS,          ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANCIDTNS}},
		{ASTCHANAMAFLAGS,        ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANAMAFLAGS}},
		{ASTCHANADSI,            ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANADSI}},
		{ASTCHANTONEZONE,        ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANTONEZONE}},
		{ASTCHANHANGUPCAUSE,     ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANHANGUPCAUSE}},
		{ASTCHANVARIABLES,       ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANVARIABLES}},
		{ASTCHANFLAGS,           ASN_OCTET_STR, RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANFLAGS}},
		{ASTCHANTRANSFERCAP,     ASN_INTEGER,   RONLY, ast_var_channels_table,      4, {ASTCHANNELS, ASTCHANTABLE, 1, ASTCHANTRANSFERCAP}},
		{ASTCHANTYPECOUNT,       ASN_INTEGER,   RONLY, ast_var_channel_types,       2, {ASTCHANNELS, ASTCHANTYPECOUNT}},
		{ASTCHANTYPEINDEX,       ASN_INTEGER,   RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPEINDEX}},
		{ASTCHANTYPENAME,        ASN_OCTET_STR, RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPENAME}},
		{ASTCHANTYPEDESC,        ASN_OCTET_STR, RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPEDESC}},
		{ASTCHANTYPEDEVSTATE,    ASN_INTEGER,   RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPEDEVSTATE}},
		{ASTCHANTYPEINDICATIONS, ASN_INTEGER,   RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPEINDICATIONS}},
		{ASTCHANTYPETRANSFER,    ASN_INTEGER,   RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPETRANSFER}},
		{ASTCHANTYPECHANNELS,    ASN_GAUGE,     RONLY, ast_var_channel_types_table, 4, {ASTCHANNELS, ASTCHANTYPETABLE, 1, ASTCHANTYPECHANNELS}},
    };

    register_sysORTable(asterisk_oid, OID_LENGTH(asterisk_oid),
			"ASTERISK-MIB implementation for Asterisk.");

    REGISTER_MIB("res_snmp", asterisk_vars, variable4, asterisk_oid);

    snmp_register_callback(SNMP_CALLBACK_LIBRARY,
			   SNMP_CALLBACK_SHUTDOWN,
			   term_asterisk_mib, NULL);
}

/*
 * Local Variables:
 * c-basic-offset: 4
 * c-file-offsets: ((case-label . 0))
 * tab-width: 4
 * indent-tabs-mode: t
 * End:
 */
