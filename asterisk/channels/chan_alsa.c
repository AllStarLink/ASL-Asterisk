/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2005, Digium, Inc.
 *
 * By Matthew Fredrickson <creslin@digium.com>
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
 * \brief ALSA sound card channel driver 
 *
 * \author Matthew Fredrickson <creslin@digium.com>
 *
 * \par See also
 * \arg Config_alsa
 *
 * \ingroup channel_drivers
 */

/*** MODULEINFO
	<depend>asound</depend>
 ***/

#include "asterisk.h"

ASTERISK_FILE_VERSION(__FILE__, "$Revision: 182810 $")

#include <unistd.h>
#include <fcntl.h>
#include <errno.h>
#include <sys/ioctl.h>
#include <sys/time.h>
#include <string.h>
#include <stdlib.h>
#include <stdio.h>

#define ALSA_PCM_NEW_HW_PARAMS_API
#define ALSA_PCM_NEW_SW_PARAMS_API
#include <alsa/asoundlib.h>

#include "asterisk/frame.h"
#include "asterisk/logger.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/options.h"
#include "asterisk/pbx.h"
#include "asterisk/config.h"
#include "asterisk/cli.h"
#include "asterisk/utils.h"
#include "asterisk/causes.h"
#include "asterisk/endian.h"
#include "asterisk/stringfields.h"
#include "asterisk/abstract_jb.h"
#include "asterisk/musiconhold.h"
#include "asterisk/poll-compat.h"

#include "busy_tone.h"
#include "ring_tone.h"
#include "ring10.h"
#include "answer.h"

#ifdef ALSA_MONITOR
#include "alsa-monitor.h"
#endif

/*! Global jitterbuffer configuration - by default, jb is disabled */
static struct ast_jb_conf default_jbconf = {
	.flags = 0,
	.max_size = -1,
	.resync_threshold = -1,
	.impl = ""
};
static struct ast_jb_conf global_jbconf;

#define DEBUG 0
/* Which device to use */
#define ALSA_INDEV "default"
#define ALSA_OUTDEV "default"
#define DESIRED_RATE 8000

/* Lets use 160 sample frames, just like GSM.  */
#define FRAME_SIZE 160
#define PERIOD_FRAMES 80		/* 80 Frames, at 2 bytes each */

/* When you set the frame size, you have to come up with
   the right buffer format as well. */
/* 5 64-byte frames = one frame */
#define BUFFER_FMT ((buffersize * 10) << 16) | (0x0006);

/* Don't switch between read/write modes faster than every 300 ms */
#define MIN_SWITCH_TIME 600

#if __BYTE_ORDER == __LITTLE_ENDIAN
static snd_pcm_format_t format = SND_PCM_FORMAT_S16_LE;
#else
static snd_pcm_format_t format = SND_PCM_FORMAT_S16_BE;
#endif

static char indevname[50] = ALSA_INDEV;
static char outdevname[50] = ALSA_OUTDEV;

#if 0
static struct timeval lasttime;
#endif

static int silencesuppression = 0;
static int silencethreshold = 1000;

AST_MUTEX_DEFINE_STATIC(alsalock);

static const char tdesc[] = "ALSA Console Channel Driver";
static const char config[] = "alsa.conf";

static char context[AST_MAX_CONTEXT] = "default";
static char language[MAX_LANGUAGE] = "";
static char exten[AST_MAX_EXTENSION] = "s";
static char mohinterpret[MAX_MUSICCLASS];

static int hookstate = 0;

static short silence[FRAME_SIZE] = { 0, };

struct sound {
	int ind;
	short *data;
	int datalen;
	int samplen;
	int silencelen;
	int repeat;
};

static struct sound sounds[] = {
	{AST_CONTROL_RINGING, ringtone, sizeof(ringtone) / 2, 16000, 32000, 1},
	{AST_CONTROL_BUSY, busy, sizeof(busy) / 2, 4000, 4000, 1},
	{AST_CONTROL_CONGESTION, busy, sizeof(busy) / 2, 2000, 2000, 1},
	{AST_CONTROL_RING, ring10, sizeof(ring10) / 2, 16000, 32000, 1},
	{AST_CONTROL_ANSWER, answer, sizeof(answer) / 2, 2200, 0, 0},
};

/* Sound command pipe */
static int sndcmd[2];

static struct chan_alsa_pvt {
	/* We only have one ALSA structure -- near sighted perhaps, but it
	   keeps this driver as simple as possible -- as it should be. */
	struct ast_channel *owner;
	char exten[AST_MAX_EXTENSION];
	char context[AST_MAX_CONTEXT];
#if 0
	snd_pcm_t *card;
#endif
	snd_pcm_t *icard, *ocard;

} alsa;

/* Number of buffers...  Each is FRAMESIZE/8 ms long.  For example
   with 160 sample frames, and a buffer size of 3, we have a 60ms buffer, 
   usually plenty. */

pthread_t sthread;

#define MAX_BUFFER_SIZE 100

/* File descriptors for sound device */
static int readdev = -1;
static int writedev = -1;

static int autoanswer = 1;

static int cursound = -1;
static int sampsent = 0;
static int silencelen = 0;
static int offset = 0;
static int nosound = 0;

/* ZZ */
static struct ast_channel *alsa_request(const char *type, int format, void *data, int *cause);
static int alsa_digit(struct ast_channel *c, char digit, unsigned int duration);
static int alsa_text(struct ast_channel *c, const char *text);
static int alsa_hangup(struct ast_channel *c);
static int alsa_answer(struct ast_channel *c);
static struct ast_frame *alsa_read(struct ast_channel *chan);
static int alsa_call(struct ast_channel *c, char *dest, int timeout);
static int alsa_write(struct ast_channel *chan, struct ast_frame *f);
static int alsa_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen);
static int alsa_fixup(struct ast_channel *oldchan, struct ast_channel *newchan);

static const struct ast_channel_tech alsa_tech = {
	.type = "Console",
	.description = tdesc,
	.capabilities = AST_FORMAT_SLINEAR,
	.requester = alsa_request,
	.send_digit_end = alsa_digit,
	.send_text = alsa_text,
	.hangup = alsa_hangup,
	.answer = alsa_answer,
	.read = alsa_read,
	.call = alsa_call,
	.write = alsa_write,
	.indicate = alsa_indicate,
	.fixup = alsa_fixup,
};

static int send_sound(void)
{
	short myframe[FRAME_SIZE];
	int total = FRAME_SIZE;
	short *frame = NULL;
	int amt = 0, res, myoff;
	snd_pcm_state_t state;

	if (cursound == -1)
		return 0;
	
	res = total;
	if (sampsent < sounds[cursound].samplen) {
		myoff = 0;
		while (total) {
			amt = total;
			if (amt > (sounds[cursound].datalen - offset))
				amt = sounds[cursound].datalen - offset;
			memcpy(myframe + myoff, sounds[cursound].data + offset, amt * 2);
			total -= amt;
			offset += amt;
			sampsent += amt;
			myoff += amt;
			if (offset >= sounds[cursound].datalen)
				offset = 0;
		}
		/* Set it up for silence */
		if (sampsent >= sounds[cursound].samplen)
			silencelen = sounds[cursound].silencelen;
		frame = myframe;
	} else {
		if (silencelen > 0) {
			frame = silence;
			silencelen -= res;
		} else {
			if (sounds[cursound].repeat) {
				/* Start over */
				sampsent = 0;
				offset = 0;
			} else {
				cursound = -1;
				nosound = 0;
			}
			return 0;
		}
	}
	
	if (res == 0 || !frame)
		return 0;

#ifdef ALSA_MONITOR
	alsa_monitor_write((char *) frame, res * 2);
#endif
	state = snd_pcm_state(alsa.ocard);
	if (state == SND_PCM_STATE_XRUN)
		snd_pcm_prepare(alsa.ocard);
	while ((res = snd_pcm_writei(alsa.ocard, frame, res)) == -EAGAIN) {
		usleep(1);
	}
	if (res > 0)
		return 0;
	return 0;
}

static void *sound_thread(void *unused)
{
	fd_set rfds;
	fd_set wfds;
	int max, res;

	for (;;) {
		FD_ZERO(&rfds);
		FD_ZERO(&wfds);
		max = sndcmd[0];
		FD_SET(sndcmd[0], &rfds);
		if (cursound > -1) {
			FD_SET(writedev, &wfds);
			if (writedev > max)
				max = writedev;
		}
#ifdef ALSA_MONITOR
		if (!alsa.owner) {
			FD_SET(readdev, &rfds);
			if (readdev > max)
				max = readdev;
		}
#endif
		res = ast_select(max + 1, &rfds, &wfds, NULL, NULL);
		if (res < 1) {
			ast_log(LOG_WARNING, "select failed: %s\n", strerror(errno));
			continue;
		}
#ifdef ALSA_MONITOR
		if (FD_ISSET(readdev, &rfds)) {
			/* Keep the pipe going with read audio */
			snd_pcm_state_t state;
			short buf[FRAME_SIZE];
			int r;

			state = snd_pcm_state(alsa.ocard);
			if (state == SND_PCM_STATE_XRUN) {
				snd_pcm_prepare(alsa.ocard);
			}
			r = snd_pcm_readi(alsa.icard, buf, FRAME_SIZE);
			if (r == -EPIPE) {
#if DEBUG
				ast_log(LOG_ERROR, "XRUN read\n");
#endif
				snd_pcm_prepare(alsa.icard);
			} else if (r == -ESTRPIPE) {
				ast_log(LOG_ERROR, "-ESTRPIPE\n");
				snd_pcm_prepare(alsa.icard);
			} else if (r < 0) {
				ast_log(LOG_ERROR, "Read error: %s\n", snd_strerror(r));
			} else
				alsa_monitor_read((char *) buf, r * 2);
		}
#endif
		if (FD_ISSET(sndcmd[0], &rfds)) {
			if (read(sndcmd[0], &cursound, sizeof(cursound)) < 0) {
				ast_log(LOG_WARNING, "read() failed: %s\n", strerror(errno));
			}
			silencelen = 0;
			offset = 0;
			sampsent = 0;
		}
		if (FD_ISSET(writedev, &wfds))
			if (send_sound())
				ast_log(LOG_WARNING, "Failed to write sound\n");
	}
	/* Never reached */
	return NULL;
}

static snd_pcm_t *alsa_card_init(char *dev, snd_pcm_stream_t stream)
{
	int err;
	int direction;
	snd_pcm_t *handle = NULL;
	snd_pcm_hw_params_t *hwparams = NULL;
	snd_pcm_sw_params_t *swparams = NULL;
	struct pollfd pfd;
	snd_pcm_uframes_t period_size = PERIOD_FRAMES * 4;
	/* int period_bytes = 0; */
	snd_pcm_uframes_t buffer_size = 0;

	unsigned int rate = DESIRED_RATE;
#if 0
	unsigned int per_min = 1;
#endif
	/* unsigned int per_max = 8; */
	snd_pcm_uframes_t start_threshold, stop_threshold;

	err = snd_pcm_open(&handle, dev, stream, SND_PCM_NONBLOCK);
	if (err < 0) {
		ast_log(LOG_ERROR, "snd_pcm_open failed: %s\n", snd_strerror(err));
		return NULL;
	} else
		ast_log(LOG_DEBUG, "Opening device %s in %s mode\n", dev, (stream == SND_PCM_STREAM_CAPTURE) ? "read" : "write");

	hwparams = alloca(snd_pcm_hw_params_sizeof());
	memset(hwparams, 0, snd_pcm_hw_params_sizeof());
	snd_pcm_hw_params_any(handle, hwparams);

	err = snd_pcm_hw_params_set_access(handle, hwparams, SND_PCM_ACCESS_RW_INTERLEAVED);
	if (err < 0)
		ast_log(LOG_ERROR, "set_access failed: %s\n", snd_strerror(err));

	err = snd_pcm_hw_params_set_format(handle, hwparams, format);
	if (err < 0)
		ast_log(LOG_ERROR, "set_format failed: %s\n", snd_strerror(err));

	err = snd_pcm_hw_params_set_channels(handle, hwparams, 1);
	if (err < 0)
		ast_log(LOG_ERROR, "set_channels failed: %s\n", snd_strerror(err));

	direction = 0;
	err = snd_pcm_hw_params_set_rate_near(handle, hwparams, &rate, &direction);
	if (rate != DESIRED_RATE)
		ast_log(LOG_WARNING, "Rate not correct, requested %d, got %d\n", DESIRED_RATE, rate);

	direction = 0;
	err = snd_pcm_hw_params_set_period_size_near(handle, hwparams, &period_size, &direction);
	if (err < 0)
		ast_log(LOG_ERROR, "period_size(%ld frames) is bad: %s\n", period_size, snd_strerror(err));
	else
		ast_log(LOG_DEBUG, "Period size is %d\n", err);

	buffer_size = 4096 * 2;		/* period_size * 16; */
	err = snd_pcm_hw_params_set_buffer_size_near(handle, hwparams, &buffer_size);
	if (err < 0)
		ast_log(LOG_WARNING, "Problem setting buffer size of %ld: %s\n", buffer_size, snd_strerror(err));
	else
		ast_log(LOG_DEBUG, "Buffer size is set to %d frames\n", err);

#if 0
	direction = 0;
	err = snd_pcm_hw_params_set_periods_min(handle, hwparams, &per_min, &direction);
	if (err < 0)
		ast_log(LOG_ERROR, "periods_min: %s\n", snd_strerror(err));

	err = snd_pcm_hw_params_set_periods_max(handle, hwparams, &per_max, 0);
	if (err < 0)
		ast_log(LOG_ERROR, "periods_max: %s\n", snd_strerror(err));
#endif

	err = snd_pcm_hw_params(handle, hwparams);
	if (err < 0)
		ast_log(LOG_ERROR, "Couldn't set the new hw params: %s\n", snd_strerror(err));

	swparams = alloca(snd_pcm_sw_params_sizeof());
	memset(swparams, 0, snd_pcm_sw_params_sizeof());
	snd_pcm_sw_params_current(handle, swparams);

#if 1
	if (stream == SND_PCM_STREAM_PLAYBACK)
		start_threshold = period_size;
	else
		start_threshold = 1;

	err = snd_pcm_sw_params_set_start_threshold(handle, swparams, start_threshold);
	if (err < 0)
		ast_log(LOG_ERROR, "start threshold: %s\n", snd_strerror(err));
#endif

#if 1
	if (stream == SND_PCM_STREAM_PLAYBACK)
		stop_threshold = buffer_size;
	else
		stop_threshold = buffer_size;

	err = snd_pcm_sw_params_set_stop_threshold(handle, swparams, stop_threshold);
	if (err < 0)
		ast_log(LOG_ERROR, "stop threshold: %s\n", snd_strerror(err));
#endif
#if 0
	err = snd_pcm_sw_params_set_xfer_align(handle, swparams, PERIOD_FRAMES);
	if (err < 0)
		ast_log(LOG_ERROR, "Unable to set xfer alignment: %s\n", snd_strerror(err));
#endif

#if 0
	err = snd_pcm_sw_params_set_silence_threshold(handle, swparams, silencethreshold);
	if (err < 0)
		ast_log(LOG_ERROR, "Unable to set silence threshold: %s\n", snd_strerror(err));
#endif
	err = snd_pcm_sw_params(handle, swparams);
	if (err < 0)
		ast_log(LOG_ERROR, "sw_params: %s\n", snd_strerror(err));

	err = snd_pcm_poll_descriptors_count(handle);
	if (err <= 0)
		ast_log(LOG_ERROR, "Unable to get a poll descriptors count, error is %s\n", snd_strerror(err));
	if (err != 1)
		ast_log(LOG_DEBUG, "Can't handle more than one device\n");

	snd_pcm_poll_descriptors(handle, &pfd, err);
	ast_log(LOG_DEBUG, "Acquired fd %d from the poll descriptor\n", pfd.fd);

	if (stream == SND_PCM_STREAM_CAPTURE)
		readdev = pfd.fd;
	else
		writedev = pfd.fd;

	return handle;
}

static int soundcard_init(void)
{
	alsa.icard = alsa_card_init(indevname, SND_PCM_STREAM_CAPTURE);
	alsa.ocard = alsa_card_init(outdevname, SND_PCM_STREAM_PLAYBACK);

	if (!alsa.icard || !alsa.ocard) {
		ast_log(LOG_ERROR, "Problem opening alsa I/O devices\n");
		return -1;
	}

	return readdev;
}

static int alsa_digit(struct ast_channel *c, char digit, unsigned int duration)
{
	ast_mutex_lock(&alsalock);
	ast_verbose(" << Console Received digit %c of duration %u ms >> \n", 
		digit, duration);
	ast_mutex_unlock(&alsalock);
	return 0;
}

static int alsa_text(struct ast_channel *c, const char *text)
{
	ast_mutex_lock(&alsalock);
	ast_verbose(" << Console Received text %s >> \n", text);
	ast_mutex_unlock(&alsalock);
	return 0;
}

static void grab_owner(void)
{
	while (alsa.owner && ast_mutex_trylock(&alsa.owner->lock)) {
		DEADLOCK_AVOIDANCE(&alsalock);
	}
}

static int alsa_call(struct ast_channel *c, char *dest, int timeout)
{
	int res = 3;
	struct ast_frame f = { AST_FRAME_CONTROL };
	ast_mutex_lock(&alsalock);
	ast_verbose(" << Call placed to '%s' on console >> \n", dest);
	if (autoanswer) {
		ast_verbose(" << Auto-answered >> \n");
		grab_owner();
		if (alsa.owner) {
			f.subclass = AST_CONTROL_ANSWER;
			ast_queue_frame(alsa.owner, &f);
			ast_mutex_unlock(&alsa.owner->lock);
		}
	} else {
		ast_verbose(" << Type 'answer' to answer, or use 'autoanswer' for future calls >> \n");
		grab_owner();
		if (alsa.owner) {
			f.subclass = AST_CONTROL_RINGING;
			ast_queue_frame(alsa.owner, &f);
			ast_mutex_unlock(&alsa.owner->lock);
		}
		if (write(sndcmd[1], &res, sizeof(res)) < 0) {
			ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
		}
	}
	snd_pcm_prepare(alsa.icard);
	snd_pcm_start(alsa.icard);
	ast_mutex_unlock(&alsalock);
	return 0;
}

static void answer_sound(void)
{
	int res;

	nosound = 1;
	res = 4;
	if (write(sndcmd[1], &res, sizeof(res)) < 0) {
		ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
	}
}

static int alsa_answer(struct ast_channel *c)
{
	ast_mutex_lock(&alsalock);
	ast_verbose(" << Console call has been answered >> \n");
	answer_sound();
	ast_setstate(c, AST_STATE_UP);
	cursound = -1;
	snd_pcm_prepare(alsa.icard);
	snd_pcm_start(alsa.icard);
	ast_mutex_unlock(&alsalock);
	return 0;
}

static int alsa_hangup(struct ast_channel *c)
{
	int res;
	ast_mutex_lock(&alsalock);
	cursound = -1;
	c->tech_pvt = NULL;
	alsa.owner = NULL;
	ast_verbose(" << Hangup on console >> \n");
	ast_module_unref(ast_module_info->self);
	if (hookstate) {
		hookstate = 0;
		if (!autoanswer) {
			/* Congestion noise */
			res = 2;
			if (write(sndcmd[1], &res, sizeof(res)) < 0) {
				ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
			}
		}
	}
	snd_pcm_drop(alsa.icard);
	ast_mutex_unlock(&alsalock);
	return 0;
}

static int alsa_write(struct ast_channel *chan, struct ast_frame *f)
{
	static char sizbuf[8000];
	static int sizpos = 0;
	int len = sizpos;
	int pos;
	int res = 0;
	/* size_t frames = 0; */
	snd_pcm_state_t state;

	/* Immediately return if no sound is enabled */
	if (nosound)
		return 0;

	ast_mutex_lock(&alsalock);
	/* Stop any currently playing sound */
	if (cursound != -1) {
		snd_pcm_drop(alsa.ocard);
		snd_pcm_prepare(alsa.ocard);
		cursound = -1;
	}


	/* We have to digest the frame in 160-byte portions */
	if (f->datalen > sizeof(sizbuf) - sizpos) {
		ast_log(LOG_WARNING, "Frame too large\n");
		res = -1;
	} else {
		memcpy(sizbuf + sizpos, f->data, f->datalen);
		len += f->datalen;
		pos = 0;
#ifdef ALSA_MONITOR
		alsa_monitor_write(sizbuf, len);
#endif
		state = snd_pcm_state(alsa.ocard);
		if (state == SND_PCM_STATE_XRUN)
			snd_pcm_prepare(alsa.ocard);
		while ((res = snd_pcm_writei(alsa.ocard, sizbuf, len / 2)) == -EAGAIN) {
			usleep(1);
		}
		if (res == -EPIPE) {
#if DEBUG
			ast_log(LOG_DEBUG, "XRUN write\n");
#endif
			snd_pcm_prepare(alsa.ocard);
			while ((res = snd_pcm_writei(alsa.ocard, sizbuf, len / 2)) == -EAGAIN) {
				usleep(1);
			}
			if (res != len / 2) {
				ast_log(LOG_ERROR, "Write error: %s\n", snd_strerror(res));
				res = -1;
			} else if (res < 0) {
				ast_log(LOG_ERROR, "Write error %s\n", snd_strerror(res));
				res = -1;
			}
		} else {
			if (res == -ESTRPIPE)
				ast_log(LOG_ERROR, "You've got some big problems\n");
			else if (res < 0)
				ast_log(LOG_NOTICE, "Error %d on write\n", res);
		}
	}
	ast_mutex_unlock(&alsalock);
	if (res > 0)
		res = 0;
	return res;
}


static struct ast_frame *alsa_read(struct ast_channel *chan)
{
	static struct ast_frame f;
	static short __buf[FRAME_SIZE + AST_FRIENDLY_OFFSET / 2];
	short *buf;
	static int readpos = 0;
	static int left = FRAME_SIZE;
	snd_pcm_state_t state;
	int r = 0;
	int off = 0;

	ast_mutex_lock(&alsalock);
	/* Acknowledge any pending cmd */
	f.frametype = AST_FRAME_NULL;
	f.subclass = 0;
	f.samples = 0;
	f.datalen = 0;
	f.data = NULL;
	f.offset = 0;
	f.src = "Console";
	f.mallocd = 0;
	f.delivery.tv_sec = 0;
	f.delivery.tv_usec = 0;

	state = snd_pcm_state(alsa.icard);
	if ((state != SND_PCM_STATE_PREPARED) && (state != SND_PCM_STATE_RUNNING)) {
		snd_pcm_prepare(alsa.icard);
	}

	buf = __buf + AST_FRIENDLY_OFFSET / 2;

	r = snd_pcm_readi(alsa.icard, buf + readpos, left);
	if (r == -EPIPE) {
#if DEBUG
		ast_log(LOG_ERROR, "XRUN read\n");
#endif
		snd_pcm_prepare(alsa.icard);
	} else if (r == -ESTRPIPE) {
		ast_log(LOG_ERROR, "-ESTRPIPE\n");
		snd_pcm_prepare(alsa.icard);
	} else if (r < 0) {
		ast_log(LOG_ERROR, "Read error: %s\n", snd_strerror(r));
	} else if (r >= 0) {
		off -= r;
	}
	/* Update positions */
	readpos += r;
	left -= r;

	if (readpos >= FRAME_SIZE) {
		/* A real frame */
		readpos = 0;
		left = FRAME_SIZE;
		if (chan->_state != AST_STATE_UP) {
			/* Don't transmit unless it's up */
			ast_mutex_unlock(&alsalock);
			return &f;
		}
		f.frametype = AST_FRAME_VOICE;
		f.subclass = AST_FORMAT_SLINEAR;
		f.samples = FRAME_SIZE;
		f.datalen = FRAME_SIZE * 2;
		f.data = buf;
		f.offset = AST_FRIENDLY_OFFSET;
		f.src = "Console";
		f.mallocd = 0;
#ifdef ALSA_MONITOR
		alsa_monitor_read((char *) buf, FRAME_SIZE * 2);
#endif

	}
	ast_mutex_unlock(&alsalock);
	return &f;
}

static int alsa_fixup(struct ast_channel *oldchan, struct ast_channel *newchan)
{
	struct chan_alsa_pvt *p = newchan->tech_pvt;
	ast_mutex_lock(&alsalock);
	p->owner = newchan;
	ast_mutex_unlock(&alsalock);
	return 0;
}

static int alsa_indicate(struct ast_channel *chan, int cond, const void *data, size_t datalen)
{
	int res = 0;

	ast_mutex_lock(&alsalock);

	switch (cond) {
	case AST_CONTROL_BUSY:
		res = 1;
		break;
	case AST_CONTROL_CONGESTION:
		res = 2;
		break;
	case AST_CONTROL_RINGING:
	case AST_CONTROL_PROGRESS:
		break;
	case -1:
		res = -1;
		break;
	case AST_CONTROL_VIDUPDATE:
		res = -1;
		break;
	case AST_CONTROL_HOLD:
		ast_verbose(" << Console Has Been Placed on Hold >> \n");
		ast_moh_start(chan, data, mohinterpret);
		break;
	case AST_CONTROL_UNHOLD:
		ast_verbose(" << Console Has Been Retrieved from Hold >> \n");
		ast_moh_stop(chan);
		break;
	case AST_CONTROL_SRCUPDATE:
		break;
	default:
		ast_log(LOG_WARNING, "Don't know how to display condition %d on %s\n", cond, chan->name);
		res = -1;
	}

	if (res > -1) {
		if (write(sndcmd[1], &res, sizeof(res)) < 0) {
			ast_log(LOG_WARNING, "write() failed: %s\n", strerror(errno));
		}
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static struct ast_channel *alsa_new(struct chan_alsa_pvt *p, int state)
{
	struct ast_channel *tmp = NULL;
	
	if (!(tmp = ast_channel_alloc(1, state, 0, 0, "", p->exten, p->context, 0, "ALSA/%s", indevname)))
		return NULL;

	tmp->tech = &alsa_tech;
	tmp->fds[0] = readdev;
	tmp->nativeformats = AST_FORMAT_SLINEAR;
	tmp->readformat = AST_FORMAT_SLINEAR;
	tmp->writeformat = AST_FORMAT_SLINEAR;
	tmp->tech_pvt = p;
	if (!ast_strlen_zero(p->context))
		ast_copy_string(tmp->context, p->context, sizeof(tmp->context));
	if (!ast_strlen_zero(p->exten))
		ast_copy_string(tmp->exten, p->exten, sizeof(tmp->exten));
	if (!ast_strlen_zero(language))
		ast_string_field_set(tmp, language, language);
	p->owner = tmp;
	ast_module_ref(ast_module_info->self);
	ast_jb_configure(tmp, &global_jbconf);
	if (state != AST_STATE_DOWN) {
		if (ast_pbx_start(tmp)) {
			ast_log(LOG_WARNING, "Unable to start PBX on %s\n", tmp->name);
			ast_hangup(tmp);
			tmp = NULL;
		}
	}

	return tmp;
}

static struct ast_channel *alsa_request(const char *type, int format, void *data, int *cause)
{
	int oldformat = format;
	struct ast_channel *tmp = NULL;

	format &= AST_FORMAT_SLINEAR;
	if (!format) {
		ast_log(LOG_NOTICE, "Asked to get a channel of format '%d'\n", oldformat);
		return NULL;
	}

	ast_mutex_lock(&alsalock);

	if (alsa.owner) {
		ast_log(LOG_NOTICE, "Already have a call on the ALSA channel\n");
		*cause = AST_CAUSE_BUSY;
	} else if (!(tmp = alsa_new(&alsa, AST_STATE_DOWN)))
		ast_log(LOG_WARNING, "Unable to create new ALSA channel\n");

	ast_mutex_unlock(&alsalock);

	return tmp;
}

static int console_autoanswer_deprecated(int fd, int argc, char *argv[])
{
	int res = RESULT_SUCCESS;

	if ((argc != 1) && (argc != 2))
		return RESULT_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (argc == 1) {
		ast_cli(fd, "Auto answer is %s.\n", autoanswer ? "on" : "off");
	} else {
		if (!strcasecmp(argv[1], "on"))
			autoanswer = -1;
		else if (!strcasecmp(argv[1], "off"))
			autoanswer = 0;
		else
			res = RESULT_SHOWUSAGE;
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static int console_autoanswer(int fd, int argc, char *argv[])
{
	int res = RESULT_SUCCESS;;
	if ((argc != 2) && (argc != 3))
		return RESULT_SHOWUSAGE;
	ast_mutex_lock(&alsalock);
	if (argc == 2) {
		ast_cli(fd, "Auto answer is %s.\n", autoanswer ? "on" : "off");
	} else {
		if (!strcasecmp(argv[2], "on"))
			autoanswer = -1;
		else if (!strcasecmp(argv[2], "off"))
			autoanswer = 0;
		else
			res = RESULT_SHOWUSAGE;
	}
	ast_mutex_unlock(&alsalock);
	return res;
}

static char *autoanswer_complete(const char *line, const char *word, int pos, int state)
{
#ifndef MIN
#define MIN(a,b) ((a) < (b) ? (a) : (b))
#endif
	switch (state) {
		case 0:
			if (!ast_strlen_zero(word) && !strncasecmp(word, "on", MIN(strlen(word), 2)))
				return ast_strdup("on");
		case 1:
			if (!ast_strlen_zero(word) && !strncasecmp(word, "off", MIN(strlen(word), 3)))
				return ast_strdup("off");
		default:
			return NULL;
	}
	return NULL;
}

static const char autoanswer_usage[] =
	"Usage: console autoanswer [on|off]\n"
	"       Enables or disables autoanswer feature.  If used without\n"
	"       argument, displays the current on/off status of autoanswer.\n"
	"       The default value of autoanswer is in 'alsa.conf'.\n";

static int console_answer_deprecated(int fd, int argc, char *argv[])
{
	int res = RESULT_SUCCESS;

	if (argc != 1)
		return RESULT_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (!alsa.owner) {
		ast_cli(fd, "No one is calling us\n");
		res = RESULT_FAILURE;
	} else {
		hookstate = 1;
		cursound = -1;
		grab_owner();
		if (alsa.owner) {
			struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_ANSWER };
			ast_queue_frame(alsa.owner, &f);
			ast_mutex_unlock(&alsa.owner->lock);
		}
		answer_sound();
	}

	snd_pcm_prepare(alsa.icard);
	snd_pcm_start(alsa.icard);

	ast_mutex_unlock(&alsalock);

	return RESULT_SUCCESS;
}

static int console_answer(int fd, int argc, char *argv[])
{
	int res = RESULT_SUCCESS;

	if (argc != 2)
		return RESULT_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (!alsa.owner) {
		ast_cli(fd, "No one is calling us\n");
		res = RESULT_FAILURE;
	} else {
		hookstate = 1;
		cursound = -1;
		grab_owner();
		if (alsa.owner) {
			struct ast_frame f = { AST_FRAME_CONTROL, AST_CONTROL_ANSWER };
			ast_queue_frame(alsa.owner, &f);
			ast_mutex_unlock(&alsa.owner->lock);
		}
		answer_sound();
	}

	snd_pcm_prepare(alsa.icard);
	snd_pcm_start(alsa.icard);

	ast_mutex_unlock(&alsalock);

	return RESULT_SUCCESS;
}

static char sendtext_usage[] =
	"Usage: console send text <message>\n"
	"       Sends a text message for display on the remote terminal.\n";

static int console_sendtext_deprecated(int fd, int argc, char *argv[])
{
	int tmparg = 2;
	int res = RESULT_SUCCESS;

	if (argc < 2)
		return RESULT_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (!alsa.owner) {
		ast_cli(fd, "No one is calling us\n");
		res = RESULT_FAILURE;
	} else {
		struct ast_frame f = { AST_FRAME_TEXT, 0 };
		char text2send[256] = "";
		text2send[0] = '\0';
		while (tmparg < argc) {
			strncat(text2send, argv[tmparg++], sizeof(text2send) - strlen(text2send) - 1);
			strncat(text2send, " ", sizeof(text2send) - strlen(text2send) - 1);
		}
		text2send[strlen(text2send) - 1] = '\n';
		f.data = text2send;
		f.datalen = strlen(text2send) + 1;
		grab_owner();
		if (alsa.owner) {
			ast_queue_frame(alsa.owner, &f);
			f.frametype = AST_FRAME_CONTROL;
			f.subclass = AST_CONTROL_ANSWER;
			f.data = NULL;
			f.datalen = 0;
			ast_queue_frame(alsa.owner, &f);
			ast_mutex_unlock(&alsa.owner->lock);
		}
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static int console_sendtext(int fd, int argc, char *argv[])
{
	int tmparg = 3;
	int res = RESULT_SUCCESS;

	if (argc < 3)
		return RESULT_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (!alsa.owner) {
		ast_cli(fd, "No one is calling us\n");
		res = RESULT_FAILURE;
	} else {
		struct ast_frame f = { AST_FRAME_TEXT, 0 };
		char text2send[256] = "";
		text2send[0] = '\0';
		while (tmparg < argc) {
			strncat(text2send, argv[tmparg++], sizeof(text2send) - strlen(text2send) - 1);
			strncat(text2send, " ", sizeof(text2send) - strlen(text2send) - 1);
		}
		text2send[strlen(text2send) - 1] = '\n';
		f.data = text2send;
		f.datalen = strlen(text2send) + 1;
		grab_owner();
		if (alsa.owner) {
			ast_queue_frame(alsa.owner, &f);
			f.frametype = AST_FRAME_CONTROL;
			f.subclass = AST_CONTROL_ANSWER;
			f.data = NULL;
			f.datalen = 0;
			ast_queue_frame(alsa.owner, &f);
			ast_mutex_unlock(&alsa.owner->lock);
		}
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static char answer_usage[] =
	"Usage: console answer\n"
	"       Answers an incoming call on the console (ALSA) channel.\n";

static int console_hangup_deprecated(int fd, int argc, char *argv[])
{
	int res = RESULT_SUCCESS;

	if (argc != 1)
		return RESULT_SHOWUSAGE;

	cursound = -1;

	ast_mutex_lock(&alsalock);

	if (!alsa.owner && !hookstate) {
		ast_cli(fd, "No call to hangup up\n");
		res = RESULT_FAILURE;
	} else {
		hookstate = 0;
		grab_owner();
		if (alsa.owner) {
			ast_queue_hangup(alsa.owner);
			ast_mutex_unlock(&alsa.owner->lock);
		}
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static int console_hangup(int fd, int argc, char *argv[])
{
	int res = RESULT_SUCCESS;

	if (argc != 2)
		return RESULT_SHOWUSAGE;

	cursound = -1;

	ast_mutex_lock(&alsalock);

	if (!alsa.owner && !hookstate) {
		ast_cli(fd, "No call to hangup up\n");
		res = RESULT_FAILURE;
	} else {
		hookstate = 0;
		grab_owner();
		if (alsa.owner) {
			ast_queue_hangup(alsa.owner);
			ast_mutex_unlock(&alsa.owner->lock);
		}
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static char hangup_usage[] =
	"Usage: console hangup\n"
	"       Hangs up any call currently placed on the console.\n";

static int console_dial_deprecated(int fd, int argc, char *argv[])
{
	char tmp[256], *tmp2;
	char *mye, *myc;
	char *d;
	int res = RESULT_SUCCESS;

	if ((argc != 1) && (argc != 2))
		return RESULT_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (alsa.owner) {
		if (argc == 2) {
			d = argv[1];
			grab_owner();
			if (alsa.owner) {
				struct ast_frame f = { AST_FRAME_DTMF };
				while (*d) {
					f.subclass = *d;
					ast_queue_frame(alsa.owner, &f);
					d++;
				}
				ast_mutex_unlock(&alsa.owner->lock);
			}
		} else {
			ast_cli(fd, "You're already in a call.  You can use this only to dial digits until you hangup\n");
			res = RESULT_FAILURE;
		}
	} else {
		mye = exten;
		myc = context;
		if (argc == 2) {
			char *stringp = NULL;
			ast_copy_string(tmp, argv[1], sizeof(tmp));
			stringp = tmp;
			strsep(&stringp, "@");
			tmp2 = strsep(&stringp, "@");
			if (!ast_strlen_zero(tmp))
				mye = tmp;
			if (!ast_strlen_zero(tmp2))
				myc = tmp2;
		}
		if (ast_exists_extension(NULL, myc, mye, 1, NULL)) {
			ast_copy_string(alsa.exten, mye, sizeof(alsa.exten));
			ast_copy_string(alsa.context, myc, sizeof(alsa.context));
			hookstate = 1;
			alsa_new(&alsa, AST_STATE_RINGING);
		} else
			ast_cli(fd, "No such extension '%s' in context '%s'\n", mye, myc);
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static int console_dial(int fd, int argc, char *argv[])
{
	char tmp[256], *tmp2;
	char *mye, *myc;
	char *d;
	int res = RESULT_SUCCESS;

	if ((argc != 2) && (argc != 3))
		return RESULT_SHOWUSAGE;

	ast_mutex_lock(&alsalock);

	if (alsa.owner) {
		if (argc == 3) {
			d = argv[2];
			grab_owner();
			if (alsa.owner) {
				struct ast_frame f = { AST_FRAME_DTMF };
				while (*d) {
					f.subclass = *d;
					ast_queue_frame(alsa.owner, &f);
					d++;
				}
				ast_mutex_unlock(&alsa.owner->lock);
			}
		} else {
			ast_cli(fd, "You're already in a call.  You can use this only to dial digits until you hangup\n");
			res = RESULT_FAILURE;
		}
	} else {
		mye = exten;
		myc = context;
		if (argc == 3) {
			char *stringp = NULL;
			ast_copy_string(tmp, argv[2], sizeof(tmp));
			stringp = tmp;
			strsep(&stringp, "@");
			tmp2 = strsep(&stringp, "@");
			if (!ast_strlen_zero(tmp))
				mye = tmp;
			if (!ast_strlen_zero(tmp2))
				myc = tmp2;
		}
		if (ast_exists_extension(NULL, myc, mye, 1, NULL)) {
			ast_copy_string(alsa.exten, mye, sizeof(alsa.exten));
			ast_copy_string(alsa.context, myc, sizeof(alsa.context));
			hookstate = 1;
			alsa_new(&alsa, AST_STATE_RINGING);
		} else
			ast_cli(fd, "No such extension '%s' in context '%s'\n", mye, myc);
	}

	ast_mutex_unlock(&alsalock);

	return res;
}

static char dial_usage[] =
	"Usage: console dial [extension[@context]]\n"
	"       Dials a given extension (and context if specified)\n";

static struct ast_cli_entry cli_alsa_answer_deprecated = {
	{ "answer", NULL },
	console_answer_deprecated, NULL,
	NULL };

static struct ast_cli_entry cli_alsa_hangup_deprecated = {
	{ "hangup", NULL },
	console_hangup_deprecated, NULL,
	NULL };

static struct ast_cli_entry cli_alsa_dial_deprecated = {
	{ "dial", NULL },
	console_dial_deprecated, NULL,
	NULL };

static struct ast_cli_entry cli_alsa_send_text_deprecated = {
	{ "send", "text", NULL },
	console_sendtext_deprecated, NULL,
	NULL };

static struct ast_cli_entry cli_alsa_autoanswer_deprecated = {
	{ "autoanswer", NULL },
	console_autoanswer_deprecated, NULL,
	NULL, autoanswer_complete };

static struct ast_cli_entry cli_alsa[] = {
	{ { "console", "answer", NULL },
	console_answer, "Answer an incoming console call",
	answer_usage, NULL, &cli_alsa_answer_deprecated },

	{ { "console", "hangup", NULL },
	console_hangup, "Hangup a call on the console",
	hangup_usage, NULL, &cli_alsa_hangup_deprecated },

	{ { "console", "dial", NULL },
	console_dial, "Dial an extension on the console",
	dial_usage, NULL, &cli_alsa_dial_deprecated },

	{ { "console", "send", "text", NULL },
	console_sendtext, "Send text to the remote device",
	sendtext_usage, NULL, &cli_alsa_send_text_deprecated },

	{ { "console", "autoanswer", NULL },
	console_autoanswer, "Sets/displays autoanswer",
	autoanswer_usage, autoanswer_complete, &cli_alsa_autoanswer_deprecated },
};

static int load_module(void)
{
	int res;
	struct ast_config *cfg;
	struct ast_variable *v;

	/* Copy the default jb config over global_jbconf */
	memcpy(&global_jbconf, &default_jbconf, sizeof(struct ast_jb_conf));

	strcpy(mohinterpret, "default");

	if ((cfg = ast_config_load(config))) {
		v = ast_variable_browse(cfg, "general");
		for (; v; v = v->next) {
			/* handle jb conf */
			if (!ast_jb_read_conf(&global_jbconf, v->name, v->value))
				continue;

			if (!strcasecmp(v->name, "autoanswer"))
				autoanswer = ast_true(v->value);
			else if (!strcasecmp(v->name, "silencesuppression"))
				silencesuppression = ast_true(v->value);
			else if (!strcasecmp(v->name, "silencethreshold"))
				silencethreshold = atoi(v->value);
			else if (!strcasecmp(v->name, "context"))
				ast_copy_string(context, v->value, sizeof(context));
			else if (!strcasecmp(v->name, "language"))
				ast_copy_string(language, v->value, sizeof(language));
			else if (!strcasecmp(v->name, "extension"))
				ast_copy_string(exten, v->value, sizeof(exten));
			else if (!strcasecmp(v->name, "input_device"))
				ast_copy_string(indevname, v->value, sizeof(indevname));
			else if (!strcasecmp(v->name, "output_device"))
				ast_copy_string(outdevname, v->value, sizeof(outdevname));
			else if (!strcasecmp(v->name, "mohinterpret"))
				ast_copy_string(mohinterpret, v->value, sizeof(mohinterpret));
		}
		ast_config_destroy(cfg);
	}
	res = pipe(sndcmd);
	if (res) {
		ast_log(LOG_ERROR, "Unable to create pipe\n");
		return -1;
	}
	res = soundcard_init();
	if (res < 0) {
		if (option_verbose > 1) {
			ast_verbose(VERBOSE_PREFIX_2 "No sound card detected -- console channel will be unavailable\n");
			ast_verbose(VERBOSE_PREFIX_2 "Turn off ALSA support by adding 'noload=chan_alsa.so' in /etc/asterisk/modules.conf\n");
		}
		return 0;
	}

	res = ast_channel_register(&alsa_tech);
	if (res < 0) {
		ast_log(LOG_ERROR, "Unable to register channel class 'Console'\n");
		return -1;
	}
	ast_cli_register_multiple(cli_alsa, sizeof(cli_alsa) / sizeof(struct ast_cli_entry));

	ast_pthread_create_background(&sthread, NULL, sound_thread, NULL);
#ifdef ALSA_MONITOR
	if (alsa_monitor_start())
		ast_log(LOG_ERROR, "Problem starting Monitoring\n");
#endif
	return 0;
}

static int unload_module(void)
{
	ast_channel_unregister(&alsa_tech);
	ast_cli_unregister_multiple(cli_alsa, sizeof(cli_alsa) / sizeof(struct ast_cli_entry));

	if (alsa.icard)
		snd_pcm_close(alsa.icard);
	if (alsa.ocard)
		snd_pcm_close(alsa.ocard);
	if (sndcmd[0] > 0) {
		close(sndcmd[0]);
		close(sndcmd[1]);
	}
	if (alsa.owner)
		ast_softhangup(alsa.owner, AST_SOFTHANGUP_APPUNLOAD);
	if (alsa.owner)
		return -1;
	return 0;
}

AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "ALSA Console Channel Driver");
