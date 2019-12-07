/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * Mark Spencer <markster@digium.com>
 * Russell Bryant <russelb@clemson.edu>
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
 * \brief Applications to test connection and produce report in text file
 *
 * \author Mark Spencer <markster@digium.com>
 * \author Russell Bryant <russelb@clemson.edu>
 * 
 * \ingroup applications
 */

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 184388 $")

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/types.h>
#include <sys/stat.h>

#include "asterisk/channel.h"
#include "asterisk/options.h"
#include "asterisk/module.h"
#include "asterisk/logger.h"
#include "asterisk/lock.h"
#include "asterisk/app.h"
#include "asterisk/pbx.h"
#include "asterisk/utils.h"

static char *tests_descrip =
	 "TestServer(): Perform test server function and write call report.\n"
	 "Results stored in /var/log/asterisk/testreports/<testid>-server.txt";
static char *tests_app = "TestServer";
static char *tests_synopsis = "Execute Interface Test Server";

static char *testc_descrip =
	 "TestClient(testid): Executes test client with given testid.\n"
	 "Results stored in /var/log/asterisk/testreports/<testid>-client.txt";

static char *testc_app = "TestClient";
static char *testc_synopsis = "Execute Interface Test Client";

static int measurenoise(struct ast_channel *chan, int ms, char *who)
{
	int res=0;
	int mssofar;
	int noise=0;
	int samples=0;
	int x;
	short *foo;
	struct timeval start;
	struct ast_frame *f;
	int rformat;
	rformat = chan->readformat;
	if (ast_set_read_format(chan, AST_FORMAT_SLINEAR)) {
		ast_log(LOG_NOTICE, "Unable to set to linear mode!\n");
		return -1;
	}
	start = ast_tvnow();
	for(;;) {
		mssofar = ast_tvdiff_ms(ast_tvnow(), start);
		if (mssofar > ms)
			break;
		res = ast_waitfor(chan, ms - mssofar);
		if (res < 1)
			break;
		f = ast_read(chan);
		if (!f) {
			res = -1;
			break;
		}
		if ((f->frametype == AST_FRAME_VOICE) && (f->subclass == AST_FORMAT_SLINEAR)) {
			foo = (short *)f->data;
			for (x=0;x<f->samples;x++) {
				noise += abs(foo[x]);
				samples++;
			}
		}
		ast_frfree(f);
	}

	if (rformat) {
		if (ast_set_read_format(chan, rformat)) {
			ast_log(LOG_NOTICE, "Unable to restore original format!\n");
			return -1;
		}
	}
	if (res < 0)
		return res;
	if (!samples) {
		ast_log(LOG_NOTICE, "No samples were received from the other side!\n");
		return -1;
	}
	ast_log(LOG_DEBUG, "%s: Noise: %d, samples: %d, avg: %d\n", who, noise, samples, noise / samples);
	return (noise / samples);
}

static int sendnoise(struct ast_channel *chan, int ms)
{
	int res;
	res = ast_tonepair_start(chan, 1537, 2195, ms, 8192);
	if (!res) {
		res = ast_waitfordigit(chan, ms);
		ast_tonepair_stop(chan);
	}
	return res;
}

static int testclient_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	int res = 0;
	char *testid=data;
	char fn[80];
	char serverver[80];
	FILE *f;

	/* Check for test id */
	if (ast_strlen_zero(testid)) {
		ast_log(LOG_WARNING, "TestClient requires an argument - the test id\n");
		return -1;
	}

	u = ast_module_user_add(chan);

	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);

	/* Wait a few just to be sure things get started */
	res = ast_safe_sleep(chan, 3000);
	/* Transmit client version */
	if (!res)
		res = ast_dtmf_stream(chan, NULL, "8378*1#", 0);
	if (option_debug)
		ast_log(LOG_DEBUG, "Transmit client version\n");

	/* Read server version */
	if (option_debug)
		ast_log(LOG_DEBUG, "Read server version\n");
	if (!res)
		res = ast_app_getdata(chan, NULL, serverver, sizeof(serverver) - 1, 0);
	if (res > 0)
		res = 0;
	if (option_debug)
		ast_log(LOG_DEBUG, "server version: %s\n", serverver);
	if (res > 0)
		res = 0;
	if (!res)
		res = ast_safe_sleep(chan, 1000);
	/* Send test id */
	if (!res)
		res = ast_dtmf_stream(chan, NULL, testid, 0);
	if (!res)
		res = ast_dtmf_stream(chan, NULL, "#", 0);
	if (option_debug)
		ast_log(LOG_DEBUG, "send test identifier: %s\n", testid);

	if ((res >=0) && (!ast_strlen_zero(testid))) {
		/* Make the directory to hold the test results in case it's not there */
		snprintf(fn, sizeof(fn), "%s/testresults", ast_config_AST_LOG_DIR);
		mkdir(fn, 0777);
		snprintf(fn, sizeof(fn), "%s/testresults/%s-client.txt", ast_config_AST_LOG_DIR, testid);
		if ((f = fopen(fn, "w+"))) {
			setlinebuf(f);
			fprintf(f, "CLIENTCHAN:    %s\n", chan->name);
			fprintf(f, "CLIENTTEST ID: %s\n", testid);
			fprintf(f, "ANSWER:        PASS\n");
			res = 0;

			if (!res) {
				/* Step 1: Wait for "1" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 2.  Wait DTMF 1\n");
				res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 1:   %s\n", (res != '1') ? "FAIL" : "PASS");
				if (res == '1')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				res = ast_safe_sleep(chan, 1000);
			}
			if (!res) {
				/* Step 2: Send "2" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 2.  Send DTMF 2\n");
				res = ast_dtmf_stream(chan, NULL, "2", 0);
				fprintf(f, "SEND DTMF 2:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 3: Wait one second */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 3.  Wait one second\n");
				res = ast_safe_sleep(chan, 1000);
				fprintf(f, "WAIT 1 SEC:    %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 4: Measure noise */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 4.  Measure noise\n");
				res = measurenoise(chan, 5000, "TestClient");
				fprintf(f, "MEASURENOISE:  %s (%d)\n", (res < 0) ? "FAIL" : "PASS", res);
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 5: Wait for "4" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 5.  Wait DTMF 4\n");
				res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 4:   %s\n", (res != '4') ? "FAIL" : "PASS");
				if (res == '4')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				/* Step 6: Transmit tone noise */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 6.  Transmit tone\n");
				res = sendnoise(chan, 6000);
				fprintf(f, "SENDTONE:      %s\n", (res < 0) ? "FAIL" : "PASS");
			}
			if (!res || (res == '5')) {
				/* Step 7: Wait for "5" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 7.  Wait DTMF 5\n");
				if (!res)
					res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 5:   %s\n", (res != '5') ? "FAIL" : "PASS");
				if (res == '5')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				/* Step 8: Wait one second */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 8.  Wait one second\n");
				res = ast_safe_sleep(chan, 1000);
				fprintf(f, "WAIT 1 SEC:    %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 9: Measure noise */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 9.  Measure tone\n");
				res = measurenoise(chan, 4000, "TestClient");
				fprintf(f, "MEASURETONE:   %s (%d)\n", (res < 0) ? "FAIL" : "PASS", res);
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 10: Send "7" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 10.  Send DTMF 7\n");
				res = ast_dtmf_stream(chan, NULL, "7", 0);
				fprintf(f, "SEND DTMF 7:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res =0;
			}
			if (!res) {
				/* Step 11: Wait for "8" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestClient: 11.  Wait DTMF 8\n");
				res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 8:   %s\n", (res != '8') ? "FAIL" : "PASS");
				if (res == '8')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				res = ast_safe_sleep(chan, 1000);
			}
			if (option_debug && !res ) {
				/* Step 12: Hangup! */
				ast_log(LOG_DEBUG, "TestClient: 12.  Hangup\n");
			}

			if (option_debug)
				ast_log(LOG_DEBUG, "-- TEST COMPLETE--\n");
			fprintf(f, "-- END TEST--\n");
			fclose(f);
			res = -1;
		} else
			res = -1;
	} else {
		ast_log(LOG_NOTICE, "Did not read a test ID on '%s'\n", chan->name);
		res = -1;
	}
	ast_module_user_remove(u);
	return res;
}

static int testserver_exec(struct ast_channel *chan, void *data)
{
	struct ast_module_user *u;
	int res = 0;
	char testid[80]="";
	char fn[80];
	FILE *f;
	u = ast_module_user_add(chan);
	if (chan->_state != AST_STATE_UP)
		res = ast_answer(chan);
	/* Read version */
	if (option_debug)
		ast_log(LOG_DEBUG, "Read client version\n");
	if (!res)
		res = ast_app_getdata(chan, NULL, testid, sizeof(testid) - 1, 0);
	if (res > 0)
		res = 0;
	if (option_debug) {
		ast_log(LOG_DEBUG, "client version: %s\n", testid);
		ast_log(LOG_DEBUG, "Transmit server version\n");
	}
	res = ast_safe_sleep(chan, 1000);
	if (!res)
		res = ast_dtmf_stream(chan, NULL, "8378*1#", 0);
	if (res > 0)
		res = 0;

	if (!res)
		res = ast_app_getdata(chan, NULL, testid, sizeof(testid) - 1, 0);
	if (option_debug)
		ast_log(LOG_DEBUG, "read test identifier: %s\n", testid);
	/* Check for sneakyness */
	if (strchr(testid, '/'))
		res = -1;
	if ((res >=0) && (!ast_strlen_zero(testid))) {
		/* Got a Test ID!  Whoo hoo! */
		/* Make the directory to hold the test results in case it's not there */
		snprintf(fn, sizeof(fn), "%s/testresults", ast_config_AST_LOG_DIR);
		mkdir(fn, 0777);
		snprintf(fn, sizeof(fn), "%s/testresults/%s-server.txt", ast_config_AST_LOG_DIR, testid);
		if ((f = fopen(fn, "w+"))) {
			setlinebuf(f);
			fprintf(f, "SERVERCHAN:    %s\n", chan->name);
			fprintf(f, "SERVERTEST ID: %s\n", testid);
			fprintf(f, "ANSWER:        PASS\n");
			ast_log(LOG_DEBUG, "Processing Test ID '%s'\n", testid);
			res = ast_safe_sleep(chan, 1000);
			if (!res) {
				/* Step 1: Send "1" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 1.  Send DTMF 1\n");
				res = ast_dtmf_stream(chan, NULL, "1", 0);
				fprintf(f, "SEND DTMF 1:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 2: Wait for "2" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 2.  Wait DTMF 2\n");
				res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 2:   %s\n", (res != '2') ? "FAIL" : "PASS");
				if (res == '2')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				/* Step 3: Measure noise */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 3.  Measure noise\n");
				res = measurenoise(chan, 6000, "TestServer");
				fprintf(f, "MEASURENOISE:  %s (%d)\n", (res < 0) ? "FAIL" : "PASS", res);
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 4: Send "4" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 4.  Send DTMF 4\n");
				res = ast_dtmf_stream(chan, NULL, "4", 0);
				fprintf(f, "SEND DTMF 4:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 5: Wait one second */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 5.  Wait one second\n");
				res = ast_safe_sleep(chan, 1000);
				fprintf(f, "WAIT 1 SEC:    %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 6: Measure noise */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 6.  Measure tone\n");
				res = measurenoise(chan, 4000, "TestServer");
				fprintf(f, "MEASURETONE:   %s (%d)\n", (res < 0) ? "FAIL" : "PASS", res);
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 7: Send "5" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 7.  Send DTMF 5\n");
				res = ast_dtmf_stream(chan, NULL, "5", 0);
				fprintf(f, "SEND DTMF 5:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}

			if (!res) {
				/* Step 8: Transmit tone noise */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 8.  Transmit tone\n");
				res = sendnoise(chan, 6000);
				fprintf(f, "SENDTONE:      %s\n", (res < 0) ? "FAIL" : "PASS");
			}

			if (!res || (res == '7')) {
				/* Step 9: Wait for "7" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 9.  Wait DTMF 7\n");
				if (!res)
					res = ast_waitfordigit(chan, 3000);
				fprintf(f, "WAIT DTMF 7:   %s\n", (res != '7') ? "FAIL" : "PASS");
				if (res == '7')
					res = 0;
				else
					res = -1;
			}
			if (!res) {
				res = ast_safe_sleep(chan, 1000);
			}
			if (!res) {
				/* Step 10: Send "8" */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 10.  Send DTMF 8\n");
				res = ast_dtmf_stream(chan, NULL, "8", 0);
				fprintf(f, "SEND DTMF 8:   %s\n", (res < 0) ? "FAIL" : "PASS");
				if (res > 0)
					res = 0;
			}
			if (!res) {
				/* Step 11: Wait for hangup to arrive! */
				if (option_debug)
					ast_log(LOG_DEBUG, "TestServer: 11.  Waiting for hangup\n");
				res = ast_safe_sleep(chan, 10000);
				fprintf(f, "WAIT HANGUP:   %s\n", (res < 0) ? "PASS" : "FAIL");
			}

			ast_log(LOG_NOTICE, "-- TEST COMPLETE--\n");
			fprintf(f, "-- END TEST--\n");
			fclose(f);
			res = -1;
		} else
			res = -1;
	} else {
		ast_log(LOG_NOTICE, "Did not read a test ID on '%s'\n", chan->name);
		res = -1;
	}
	ast_module_user_remove(u);
	return res;
}

static int unload_module(void)
{
	int res;

	res = ast_unregister_application(testc_app);
	res |= ast_unregister_application(tests_app);

	ast_module_user_hangup_all();

	return res;
}

static int load_module(void)
{
	int res;

	res = ast_register_application(testc_app, testclient_exec, testc_synopsis, testc_descrip);
	res |= ast_register_application(tests_app, testserver_exec, tests_synopsis, tests_descrip);

	return res;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "Interface Test Application");
