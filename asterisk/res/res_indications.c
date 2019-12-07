/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2002, Pauline Middelink
 *
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

/*! \file res_indications.c 
 *
 * \brief Load the indications
 * 
 * \author Pauline Middelink <middelink@polyware.nl>
 *
 * Load the country specific dialtones into the asterisk PBX.
 */
 
#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 182808 $")

#include <unistd.h>
#include <string.h>
#include <ctype.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "asterisk/lock.h"
#include "asterisk/file.h"
#include "asterisk/cli.h"
#include "asterisk/logger.h"
#include "asterisk/config.h"
#include "asterisk/channel.h"
#include "asterisk/pbx.h"
#include "asterisk/module.h"
#include "asterisk/translate.h"
#include "asterisk/indications.h"
#include "asterisk/utils.h"

/* Globals */
static const char config[] = "indications.conf";

/*
 * Help for commands provided by this module ...
 */
static char help_add_indication[] =
"Usage: indication add <country> <indication> \"<tonelist>\"\n"
"       Add the given indication to the country.\n";

static char help_remove_indication[] =
"Usage: indication remove <country> <indication>\n"
"       Remove the given indication from the country.\n";

static char help_show_indications[] =
"Usage: indication show [<country> ...]\n"
"       Display either a condensed for of all country/indications, or the\n"
"       indications for the specified countries.\n";

char *playtones_desc=
"PlayTones(arg): Plays a tone list. Execution will continue with the next step immediately,\n"
"while the tones continue to play.\n"
"Arg is either the tone name defined in the indications.conf configuration file, or a directly\n"
"specified list of frequencies and durations.\n"
"See the sample indications.conf for a description of the specification of a tonelist.\n\n"
"Use the StopPlayTones application to stop the tones playing. \n";

/*
 * Implementation of functions provided by this module
 */

/*
 * ADD INDICATION command stuff
 */
static int handle_add_indication(int fd, int argc, char *argv[])
{
	struct tone_zone *tz;
	int created_country = 0;
	if (argc != 5) return RESULT_SHOWUSAGE;

	tz = ast_get_indication_zone(argv[2]);
	if (!tz) {
		/* country does not exist, create it */
		ast_log(LOG_NOTICE, "Country '%s' does not exist, creating it.\n",argv[2]);
		
		if (!(tz = ast_calloc(1, sizeof(*tz)))) {
			return -1;
		}
		ast_copy_string(tz->country,argv[2],sizeof(tz->country));
		if (ast_register_indication_country(tz)) {
			ast_log(LOG_WARNING, "Unable to register new country\n");
			free(tz);
			return -1;
		}
		created_country = 1;
	}
	if (ast_register_indication(tz,argv[3],argv[4])) {
		ast_log(LOG_WARNING, "Unable to register indication %s/%s\n",argv[2],argv[3]);
		if (created_country)
			ast_unregister_indication_country(argv[2]);
		return -1;
	}
	return 0;
}

/*
 * REMOVE INDICATION command stuff
 */
static int handle_remove_indication(int fd, int argc, char *argv[])
{
	struct tone_zone *tz;
	if (argc != 3 && argc != 4) return RESULT_SHOWUSAGE;

	if (argc == 3) {
		/* remove entiry country */
		if (ast_unregister_indication_country(argv[2])) {
			ast_log(LOG_WARNING, "Unable to unregister indication country %s\n",argv[2]);
			return -1;
		}
		return 0;
	}

	tz = ast_get_indication_zone(argv[2]);
	if (!tz) {
		ast_log(LOG_WARNING, "Unable to unregister indication %s/%s, country does not exists\n",argv[2],argv[3]);
		return -1;
	}
	if (ast_unregister_indication(tz,argv[3])) {
		ast_log(LOG_WARNING, "Unable to unregister indication %s/%s\n",argv[2],argv[3]);
		return -1;
	}
	return 0;
}

/*
 * SHOW INDICATIONS command stuff
 */
static int handle_show_indications(int fd, int argc, char *argv[])
{
	struct tone_zone *tz = NULL;
	char buf[256];
	int found_country = 0;

	if (argc == 2) {
		/* no arguments, show a list of countries */
		ast_cli(fd,"Country Alias   Description\n"
			   "===========================\n");
		while ( (tz = ast_walk_indications(tz) ) )
			ast_cli(fd,"%-7.7s %-7.7s %s\n", tz->country, tz->alias, tz->description);
		return 0;
	}
	/* there was a request for specific country(ies), lets humor them */
	while ( (tz = ast_walk_indications(tz) ) ) {
		int i,j;
		for (i=2; i<argc; i++) {
			if (strcasecmp(tz->country,argv[i])==0 &&
			    !tz->alias[0]) {
				struct tone_zone_sound* ts;
				if (!found_country) {
					found_country = 1;
					ast_cli(fd,"Country Indication      PlayList\n"
						   "=====================================\n");
				}
				j = snprintf(buf,sizeof(buf),"%-7.7s %-15.15s ",tz->country,"<ringcadence>");
				for (i=0; i<tz->nrringcadence; i++) {
					j += snprintf(buf+j,sizeof(buf)-j,"%d,",tz->ringcadence[i]);
				}
				if (tz->nrringcadence)
					j--;
				ast_copy_string(buf+j,"\n",sizeof(buf)-j);
				ast_cli(fd, "%s", buf);
				for (ts=tz->tones; ts; ts=ts->next)
					ast_cli(fd,"%-7.7s %-15.15s %s\n",tz->country,ts->name,ts->data);
				break;
			}
		}
	}
	if (!found_country)
		ast_cli(fd,"No countries matched your criteria.\n");
	return -1;
}

/*
 * Playtones command stuff
 */
static int handle_playtones(struct ast_channel *chan, void *data)
{
	struct tone_zone_sound *ts;
	int res;

	if (!data || !((char*)data)[0]) {
		ast_log(LOG_NOTICE,"Nothing to play\n");
		return -1;
	}
	ts = ast_get_indication_tone(chan->zone, (const char*)data);
	if (ts && ts->data[0])
		res = ast_playtones_start(chan, 0, ts->data, 0);
	else
		res = ast_playtones_start(chan, 0, (const char*)data, 0);
	if (res)
		ast_log(LOG_NOTICE,"Unable to start playtones\n");
	return res;
}

/*
 * StopPlaylist command stuff
 */
static int handle_stopplaytones(struct ast_channel *chan, void *data)
{
	ast_playtones_stop(chan);
	return 0;
}

/*
 * Load module stuff
 */
static int ind_load_module(void)
{
	struct ast_config *cfg;
	struct ast_variable *v;
	char *cxt;
	char *c;
	struct tone_zone *tones;
	const char *country = NULL;

	/* that the following cast is needed, is yuk! */
	/* yup, checked it out. It is NOT written to. */
	cfg = ast_config_load((char *)config);
	if (!cfg)
		return -1;

	/* Use existing config to populate the Indication table */
	cxt = ast_category_browse(cfg, NULL);
	while(cxt) {
		/* All categories but "general" are considered countries */
		if (!strcasecmp(cxt, "general")) {
			cxt = ast_category_browse(cfg, cxt);
			continue;
		}		
		if (!(tones = ast_calloc(1, sizeof(*tones)))) {
			ast_config_destroy(cfg);
			return -1;
		}
		ast_copy_string(tones->country,cxt,sizeof(tones->country));

		v = ast_variable_browse(cfg, cxt);
		while(v) {
			if (!strcasecmp(v->name, "description")) {
				ast_copy_string(tones->description, v->value, sizeof(tones->description));
			} else if ((!strcasecmp(v->name,"ringcadence"))||(!strcasecmp(v->name,"ringcadance"))) {
				char *ring,*rings = ast_strdupa(v->value);
				c = rings;
				ring = strsep(&c,",");
				while (ring) {
					int *tmp, val;
					if (!isdigit(ring[0]) || (val=atoi(ring))==-1) {
						ast_log(LOG_WARNING,"Invalid ringcadence given '%s' at line %d.\n",ring,v->lineno);
						ring = strsep(&c,",");
						continue;
					}					
					if (!(tmp = ast_realloc(tones->ringcadence, (tones->nrringcadence + 1) * sizeof(int)))) {
						ast_config_destroy(cfg);
						return -1;
					}
					tones->ringcadence = tmp;
					tmp[tones->nrringcadence] = val;
					tones->nrringcadence++;
					/* next item */
					ring = strsep(&c,",");
				}
			} else if (!strcasecmp(v->name,"alias")) {
				char *countries = ast_strdupa(v->value);
				c = countries;
				country = strsep(&c,",");
				while (country) {
					struct tone_zone* azone;
					if (!(azone = ast_calloc(1, sizeof(*azone)))) {
						ast_config_destroy(cfg);
						return -1;
					}
					ast_copy_string(azone->country, country, sizeof(azone->country));
					ast_copy_string(azone->alias, cxt, sizeof(azone->alias));
					if (ast_register_indication_country(azone)) {
						ast_log(LOG_WARNING, "Unable to register indication alias at line %d.\n",v->lineno);
						free(tones);
					}
					/* next item */
					country = strsep(&c,",");
				}
			} else {
				/* add tone to country */
				struct tone_zone_sound *ps,*ts;
				for (ps=NULL,ts=tones->tones; ts; ps=ts, ts=ts->next) {
					if (strcasecmp(v->name,ts->name)==0) {
						/* already there */
						ast_log(LOG_NOTICE,"Duplicate entry '%s', skipped.\n",v->name);
						goto out;
					}
				}
				/* not there, add it to the back */				
				if (!(ts = ast_malloc(sizeof(*ts)))) {
					ast_config_destroy(cfg);
					return -1;
				}
				ts->next = NULL;
				ts->name = strdup(v->name);
				ts->data = strdup(v->value);
				if (ps)
					ps->next = ts;
				else
					tones->tones = ts;
			}
out:			v = v->next;
		}
		if (tones->description[0] || tones->alias[0] || tones->tones) {
			if (ast_register_indication_country(tones)) {
				ast_log(LOG_WARNING, "Unable to register indication at line %d.\n",v->lineno);
				free(tones);
			}
		} else free(tones);

		cxt = ast_category_browse(cfg, cxt);
	}

	/* determine which country is the default */
	country = ast_variable_retrieve(cfg,"general","country");
	if (!country || !*country || ast_set_indication_country(country))
		ast_log(LOG_WARNING,"Unable to set the default country (for indication tones)\n");

	ast_config_destroy(cfg);
	return 0;
}

/*
 * CLI entries for commands provided by this module
 */
static struct ast_cli_entry cli_show_indications_deprecated = {
	{ "show", "indications", NULL },
	handle_show_indications, NULL,
	NULL };

static struct ast_cli_entry cli_indications[] = {
	{ { "indication", "add", NULL },
	handle_add_indication, "Add the given indication to the country",
	help_add_indication, NULL },

	{ { "indication", "remove", NULL },
	handle_remove_indication, "Remove the given indication from the country",
	help_remove_indication, NULL },

	{ { "indication", "show", NULL },
	handle_show_indications, "Display a list of all countries/indications",
	help_show_indications, NULL, &cli_show_indications_deprecated },
};

/*
 * Standard module functions ...
 */
static int unload_module(void)
{
	/* remove the registed indications... */
	ast_unregister_indication_country(NULL);

	/* and the functions */
	ast_cli_unregister_multiple(cli_indications, sizeof(cli_indications) / sizeof(struct ast_cli_entry));
	ast_unregister_application("PlayTones");
	ast_unregister_application("StopPlayTones");
	return 0;
}


static int load_module(void)
{
	if (ind_load_module())
		return AST_MODULE_LOAD_DECLINE; 
	ast_cli_register_multiple(cli_indications, sizeof(cli_indications) / sizeof(struct ast_cli_entry));
	ast_register_application("PlayTones", handle_playtones, "Play a tone list", playtones_desc);
	ast_register_application("StopPlayTones", handle_stopplaytones, "Stop playing a tone list","Stop playing a tone list");

	return 0;
}

static int reload(void)
{
	/* remove the registed indications... */
	ast_unregister_indication_country(NULL);

	return ind_load_module();
}

AST_MODULE_INFO(ASTERISK_GPL_KEY, AST_MODFLAG_DEFAULT, "Indications Resource",
		.load = load_module,
		.unload = unload_module,
		.reload = reload,
	       );
