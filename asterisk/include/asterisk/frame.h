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

/*! \file
 * \brief Asterisk internal frame definitions.
 * \arg For an explanation of frames, see \ref Def_Frame
 * \arg Frames are send of Asterisk channels, see \ref Def_Channel
 */

#ifndef _ASTERISK_FRAME_H
#define _ASTERISK_FRAME_H

#if defined(__cplusplus) || defined(c_plusplus)
extern "C" {
#endif

#include <sys/types.h>
#include <sys/time.h>

#include "asterisk/compiler.h"
#include "asterisk/endian.h"
#include "asterisk/linkedlists.h"

struct ast_codec_pref {
	char order[32];
	char framing[32];
};

/*! \page Def_Frame AST Multimedia and signalling frames
	\section Def_AstFrame What is an ast_frame ?
 	A frame of data read used to communicate between 
 	between channels and applications.
	Frames are divided into frame types and subclasses.

	\par Frame types 
	\arg \b VOICE:	Voice data, subclass is codec (AST_FORMAT_*)
	\arg \b VIDEO:	Video data, subclass is codec (AST_FORMAT_*)
	\arg \b DTMF:	A DTMF digit, subclass is the digit
	\arg \b IMAGE:	Image transport, mostly used in IAX
	\arg \b TEXT:	Text messages
	\arg \b HTML:	URL's and web pages
	\arg \b MODEM:	Modulated data encodings, such as T.38 and V.150
	\arg \b IAX:	Private frame type for the IAX protocol
	\arg \b CNG:	Comfort noice frames
	\arg \b CONTROL:	A control frame, subclass defined as AST_CONTROL_
	\arg \b NULL:	Empty, useless frame

	\par Files
	\arg frame.h	Definitions
	\arg frame.c	Function library
	\arg \ref Def_Channel Asterisk channels
	\section Def_ControlFrame Control Frames
	Control frames send signalling information between channels
	and devices. They are prefixed with AST_CONTROL_, like AST_CONTROL_FRAME_HANGUP
	\arg \b HANGUP	The other end has hungup
	\arg \b RING	Local ring
	\arg \b RINGING	The other end is ringing
	\arg \b ANSWER	The other end has answered
	\arg \b BUSY	Remote end is busy
	\arg \b TAKEOFFHOOK	Make it go off hook (what's "it" ? )
	\arg \b OFFHOOK	Line is off hook
	\arg \b CONGESTION	Congestion (circuit is busy, not available)
	\arg \b FLASH	Other end sends flash hook
	\arg \b WINK	Other end sends wink
	\arg \b OPTION	Send low-level option
	\arg \b RADIO_KEY	Key radio (see app_rpt.c)
	\arg \b RADIO_UNKEY	Un-key radio (see app_rpt.c)
	\arg \b PROGRESS	Other end indicates call progress
	\arg \b PROCEEDING	Indicates proceeding
	\arg \b HOLD	Call is placed on hold
	\arg \b UNHOLD	Call is back from hold
	\arg \b VIDUPDATE	Video update requested
	\arg \b SRCUPDATE       The source of media has changed

*/

/*!
 * \brief Frame types 
 *
 * \note It is important that the values of each frame type are never changed,
 *       because it will break backwards compatability with older versions.
 */
enum ast_frame_type {
	/*! DTMF end event, subclass is the digit */
	AST_FRAME_DTMF_END = 1,
	/*! Voice data, subclass is AST_FORMAT_* */
	AST_FRAME_VOICE,
	/*! Video frame, maybe?? :) */
	AST_FRAME_VIDEO,
	/*! A control frame, subclass is AST_CONTROL_* */
	AST_FRAME_CONTROL,
	/*! An empty, useless frame */
	AST_FRAME_NULL,
	/*! Inter Asterisk Exchange private frame type */
	AST_FRAME_IAX,
	/*! Text messages */
	AST_FRAME_TEXT,
	/*! Image Frames */
	AST_FRAME_IMAGE,
	/*! HTML Frame */
	AST_FRAME_HTML,
	/*! Comfort Noise frame (subclass is level of CNG in -dBov), 
	    body may include zero or more 8-bit quantization coefficients */
	AST_FRAME_CNG,
	/*! Modem-over-IP data streams */
	AST_FRAME_MODEM,	
	/*! DTMF begin event, subclass is the digit */
	AST_FRAME_DTMF_BEGIN,
};
#define AST_FRAME_DTMF AST_FRAME_DTMF_END

enum {
	/*! This frame contains valid timing information */
	AST_FRFLAG_HAS_TIMING_INFO = (1 << 0),
};

/*! \brief Data structure associated with a single frame of data
 */
struct ast_frame {
	/*! Kind of frame */
	enum ast_frame_type frametype;				
	/*! Subclass, frame dependent */
	int subclass;				
	/*! Length of data */
	int datalen;				
	/*! Number of samples in this frame */
	int samples;				
	/*! Was the data malloc'd?  i.e. should we free it when we discard the frame? */
	int mallocd;				
	/*! The number of bytes allocated for a malloc'd frame header */
	size_t mallocd_hdr_len;
	/*! How many bytes exist _before_ "data" that can be used if needed */
	int offset;				
	/*! Optional source of frame for debugging */
	const char *src;				
	/*! Pointer to actual data */
	void *data;		
	/*! Global delivery time */		
	struct timeval delivery;
	/*! For placing in a linked list */
	AST_LIST_ENTRY(ast_frame) frame_list;
	/*! Misc. frame flags */
	unsigned int flags;
	/*! Timestamp in milliseconds */
	long ts;
	/*! Length in milliseconds */
	long len;
	/*! Sequence number */
	int seqno;
};

/*!
 * Set the various field of a frame to point to a buffer.
 * Typically you set the base address of the buffer, the offset as
 * AST_FRIENDLY_OFFSET, and the datalen as the amount of bytes queued.
 * The remaining things (to be done manually) is set the number of
 * samples, which cannot be derived from the datalen unless you know
 * the number of bits per sample.
 */
#define	AST_FRAME_SET_BUFFER(fr, _base, _ofs, _datalen)	\
	{					\
	(fr)->data = (char *)_base + (_ofs);	\
	(fr)->offset = (_ofs);			\
	(fr)->datalen = (_datalen);		\
	}

/*! Queueing a null frame is fairly common, so we declare a global null frame object
    for this purpose instead of having to declare one on the stack */
extern struct ast_frame ast_null_frame;

#define AST_FRIENDLY_OFFSET 	64	/*! It's polite for a a new frame to
					  have this number of bytes for additional
					  headers.  */
#define AST_MIN_OFFSET 		32	/*! Make sure we keep at least this much handy */

/*! Need the header be free'd? */
#define AST_MALLOCD_HDR		(1 << 0)
/*! Need the data be free'd? */
#define AST_MALLOCD_DATA	(1 << 1)
/*! Need the source be free'd? (haha!) */
#define AST_MALLOCD_SRC		(1 << 2)

/* MODEM subclasses */
/*! T.38 Fax-over-IP */
#define AST_MODEM_T38		1
/*! V.150 Modem-over-IP */
#define AST_MODEM_V150		2

/* HTML subclasses */
/*! Sending a URL */
#define AST_HTML_URL		1
/*! Data frame */
#define AST_HTML_DATA		2
/*! Beginning frame */
#define AST_HTML_BEGIN		4
/*! End frame */
#define AST_HTML_END		8
/*! Load is complete */
#define AST_HTML_LDCOMPLETE	16
/*! Peer is unable to support HTML */
#define AST_HTML_NOSUPPORT	17
/*! Send URL, and track */
#define AST_HTML_LINKURL	18
/*! No more HTML linkage */
#define AST_HTML_UNLINK		19
/*! Reject link request */
#define AST_HTML_LINKREJECT	20

/* Data formats for capabilities and frames alike */
/*! G.723.1 compression */
#define AST_FORMAT_G723_1	(1 << 0)
/*! GSM compression */
#define AST_FORMAT_GSM		(1 << 1)
/*! Raw mu-law data (G.711) */
#define AST_FORMAT_ULAW		(1 << 2)
/*! Raw A-law data (G.711) */
#define AST_FORMAT_ALAW		(1 << 3)
/*! ADPCM (G.726, 32kbps, AAL2 codeword packing) */
#define AST_FORMAT_G726_AAL2	(1 << 4)
/*! ADPCM (IMA) */
#define AST_FORMAT_ADPCM	(1 << 5)
/*! Raw 16-bit Signed Linear (8000 Hz) PCM */
#define AST_FORMAT_SLINEAR	(1 << 6)
/*! LPC10, 180 samples/frame */
#define AST_FORMAT_LPC10	(1 << 7)
/*! G.729A audio */
#define AST_FORMAT_G729A	(1 << 8)
/*! SpeeX Free Compression */
#define AST_FORMAT_SPEEX	(1 << 9)
/*! iLBC Free Compression */
#define AST_FORMAT_ILBC		(1 << 10)
/*! ADPCM (G.726, 32kbps, RFC3551 codeword packing) */
#define AST_FORMAT_G726		(1 << 11)
/*! G.722 */
#define AST_FORMAT_G722		(1 << 12)
/*! Unsupported audio bits */
#define AST_FORMAT_AUDIO_UNDEFINED	((1 << 13) | (1 << 14) | (1 << 15))
/*! Maximum audio format */
#define AST_FORMAT_MAX_AUDIO	(1 << 15)
/*! Maximum audio mask */
#define AST_FORMAT_AUDIO_MASK   ((1 << 16)-1)
/*! JPEG Images */
#define AST_FORMAT_JPEG		(1 << 16)
/*! PNG Images */
#define AST_FORMAT_PNG		(1 << 17)
/*! H.261 Video */
#define AST_FORMAT_H261		(1 << 18)
/*! H.263 Video */
#define AST_FORMAT_H263		(1 << 19)
/*! H.263+ Video */
#define AST_FORMAT_H263_PLUS	(1 << 20)
/*! H.264 Video */
#define AST_FORMAT_H264		(1 << 21)
/*! Maximum video format */
#define AST_FORMAT_MAX_VIDEO	(1 << 24)
#define AST_FORMAT_VIDEO_MASK   (((1 << 25)-1) & ~(AST_FORMAT_AUDIO_MASK))

enum ast_control_frame_type {
	AST_CONTROL_HANGUP = 1,		/*!< Other end has hungup */
	AST_CONTROL_RING = 2,		/*!< Local ring */
	AST_CONTROL_RINGING = 3,	/*!< Remote end is ringing */
	AST_CONTROL_ANSWER = 4,		/*!< Remote end has answered */
	AST_CONTROL_BUSY = 5,		/*!< Remote end is busy */
	AST_CONTROL_TAKEOFFHOOK = 6,	/*!< Make it go off hook */
	AST_CONTROL_OFFHOOK = 7,	/*!< Line is off hook */
	AST_CONTROL_CONGESTION = 8,	/*!< Congestion (circuits busy) */
	AST_CONTROL_FLASH = 9,		/*!< Flash hook */
	AST_CONTROL_WINK = 10,		/*!< Wink */
	AST_CONTROL_OPTION = 11,	/*!< Set a low-level option */
	AST_CONTROL_RADIO_KEY = 12,	/*!< Key Radio */
	AST_CONTROL_RADIO_UNKEY = 13,	/*!< Un-Key Radio */
	AST_CONTROL_PROGRESS = 14,	/*!< Indicate PROGRESS */
	AST_CONTROL_PROCEEDING = 15,	/*!< Indicate CALL PROCEEDING */
	AST_CONTROL_HOLD = 16,		/*!< Indicate call is placed on hold */
	AST_CONTROL_UNHOLD = 17,	/*!< Indicate call is left from hold */
	AST_CONTROL_VIDUPDATE = 18,	/*!< Indicate video frame update */
	AST_CONTROL_SRCUPDATE = 20,     /*!< Indicate source of media has changed */
};

#define AST_SMOOTHER_FLAG_G729		(1 << 0)
#define AST_SMOOTHER_FLAG_BE		(1 << 1)

/* Option identifiers and flags */
#define AST_OPTION_FLAG_REQUEST		0
#define AST_OPTION_FLAG_ACCEPT		1
#define AST_OPTION_FLAG_REJECT		2
#define AST_OPTION_FLAG_QUERY		4
#define AST_OPTION_FLAG_ANSWER		5
#define AST_OPTION_FLAG_WTF		6

/*! Verify touchtones by muting audio transmission 
	(and reception) and verify the tone is still present */
#define AST_OPTION_TONE_VERIFY		1		

/*! Put a compatible channel into TDD (TTY for the hearing-impared) mode */
#define	AST_OPTION_TDD			2

/*! Relax the parameters for DTMF reception (mainly for radio use) */
#define	AST_OPTION_RELAXDTMF		3

/*! Set (or clear) Audio (Not-Clear) Mode */
#define	AST_OPTION_AUDIO_MODE		4

/*! Set channel transmit gain 
 * Option data is a single signed char
   representing number of decibels (dB)
   to set gain to (on top of any gain
   specified in channel driver)
*/
#define AST_OPTION_TXGAIN		5

/*! Set channel receive gain
 * Option data is a single signed char
   representing number of decibels (dB)
   to set gain to (on top of any gain
   specified in channel driver)
*/
#define AST_OPTION_RXGAIN		6

/* set channel into "Operator Services" mode */
#define	AST_OPTION_OPRMODE		7

/*! Explicitly enable or disable echo cancelation for the given channel */
#define	AST_OPTION_ECHOCAN		8

struct oprmode {
	struct ast_channel *peer;
	int mode;
} ;

struct ast_option_header {
	/* Always keep in network byte order */
#if __BYTE_ORDER == __BIG_ENDIAN
        uint16_t flag:3;
        uint16_t option:13;
#else
#if __BYTE_ORDER == __LITTLE_ENDIAN
        uint16_t option:13;
        uint16_t flag:3;
#else
#error Byte order not defined
#endif
#endif
		uint8_t data[0];
};


/*! \brief Definition of supported media formats (codecs) */
struct ast_format_list {
	int visible;	/*!< Can we see this entry */
	int bits;	/*!< bitmask value */
	char *name;	/*!< short name */
	char *desc;	/*!< Description */
	int fr_len;	/*!< Single frame length in bytes */
	int min_ms;	/*!< Min value */
	int max_ms;	/*!< Max value */
	int inc_ms;	/*!< Increment */
	int def_ms;	/*!< Default value */
	unsigned int flags;	/*!< Smoother flags */
	int cur_ms;	/*!< Current value */
};


/*! \brief  Requests a frame to be allocated 
 * 
 * \param source 
 * Request a frame be allocated.  source is an optional source of the frame, 
 * len is the requested length, or "0" if the caller will supply the buffer 
 */
#if 0 /* Unimplemented */
struct ast_frame *ast_fralloc(char *source, int len);
#endif

/*!  
 * \brief Frees a frame or list of frames
 * 
 * \param fr Frame to free, or head of list to free
 * \param cache Whether to consider this frame for frame caching
 */
void ast_frame_free(struct ast_frame *fr, int cache);

#define ast_frfree(fr) ast_frame_free(fr, 1)

/*! \brief Makes a frame independent of any static storage
 * \param fr frame to act upon
 * Take a frame, and if it's not been malloc'd, make a malloc'd copy
 * and if the data hasn't been malloced then make the
 * data malloc'd.  If you need to store frames, say for queueing, then
 * you should call this function.
 * \return Returns a frame on success, NULL on error
 * \note This function may modify the frame passed to it, so you must
 * not assume the frame will be intact after the isolated frame has
 * been produced. In other words, calling this function on a frame
 * should be the last operation you do with that frame before freeing
 * it (or exiting the block, if the frame is on the stack.)
 */
struct ast_frame *ast_frisolate(struct ast_frame *fr);

/*! \brief Copies a frame 
 * \param fr frame to copy
 * Duplicates a frame -- should only rarely be used, typically frisolate is good enough
 * \return Returns a frame on success, NULL on error
 */
struct ast_frame *ast_frdup(const struct ast_frame *fr);

void ast_swapcopy_samples(void *dst, const void *src, int samples);

/* Helpers for byteswapping native samples to/from 
   little-endian and big-endian. */
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ast_frame_byteswap_le(fr) do { ; } while(0)
#define ast_frame_byteswap_be(fr) do { struct ast_frame *__f = (fr); ast_swapcopy_samples(__f->data, __f->data, __f->samples); } while(0)
#else
#define ast_frame_byteswap_le(fr) do { struct ast_frame *__f = (fr); ast_swapcopy_samples(__f->data, __f->data, __f->samples); } while(0)
#define ast_frame_byteswap_be(fr) do { ; } while(0)
#endif


/*! \brief Get the name of a format
 * \param format id of format
 * \return A static string containing the name of the format or "unknown" if unknown.
 */
char* ast_getformatname(int format);

/*! \brief Get the names of a set of formats
 * \param buf a buffer for the output string
 * \param size size of buf (bytes)
 * \param format the format (combined IDs of codecs)
 * Prints a list of readable codec names corresponding to "format".
 * ex: for format=AST_FORMAT_GSM|AST_FORMAT_SPEEX|AST_FORMAT_ILBC it will return "0x602 (GSM|SPEEX|ILBC)"
 * \return The return value is buf.
 */
char* ast_getformatname_multiple(char *buf, size_t size, int format);

/*!
 * \brief Gets a format from a name.
 * \param name string of format
 * \return This returns the form of the format in binary on success, 0 on error.
 */
int ast_getformatbyname(const char *name);

/*! \brief Get a name from a format 
 * Gets a name from a format
 * \param codec codec number (1,2,4,8,16,etc.)
 * \return This returns a static string identifying the format on success, 0 on error.
 */
char *ast_codec2str(int codec);

struct ast_smoother;

struct ast_format_list *ast_get_format_list_index(int index);
struct ast_format_list *ast_get_format_list(size_t *size);
struct ast_smoother *ast_smoother_new(int bytes);
void ast_smoother_set_flags(struct ast_smoother *smoother, int flags);
int ast_smoother_get_flags(struct ast_smoother *smoother);
int ast_smoother_test_flag(struct ast_smoother *s, int flag);
void ast_smoother_free(struct ast_smoother *s);
void ast_smoother_reset(struct ast_smoother *s, int bytes);

/*!
 * \brief Reconfigure an existing smoother to output a different number of bytes per frame
 * \param s the smoother to reconfigure
 * \param bytes the desired number of bytes per output frame
 * \return nothing
 *
 */
void ast_smoother_reconfigure(struct ast_smoother *s, int bytes);

int __ast_smoother_feed(struct ast_smoother *s, struct ast_frame *f, int swap);
struct ast_frame *ast_smoother_read(struct ast_smoother *s);
#define ast_smoother_feed(s,f) __ast_smoother_feed(s, f, 0)
#if __BYTE_ORDER == __LITTLE_ENDIAN
#define ast_smoother_feed_be(s,f) __ast_smoother_feed(s, f, 1)
#define ast_smoother_feed_le(s,f) __ast_smoother_feed(s, f, 0)
#else
#define ast_smoother_feed_be(s,f) __ast_smoother_feed(s, f, 0)
#define ast_smoother_feed_le(s,f) __ast_smoother_feed(s, f, 1)
#endif

void ast_frame_dump(const char *name, struct ast_frame *f, char *prefix);

/*! \page AudioCodecPref Audio Codec Preferences
	In order to negotiate audio codecs in the order they are configured
	in <channel>.conf for a device, we set up codec preference lists
	in addition to the codec capabilities setting. The capabilities
	setting is a bitmask of audio and video codecs with no internal
	order. This will reflect the offer given to the other side, where
	the prefered codecs will be added to the top of the list in the
	order indicated by the "allow" lines in the device configuration.
	
	Video codecs are not included in the preference lists since they
	can't be transcoded and we just have to pick whatever is supported
*/

/*! \brief Initialize an audio codec preference to "no preference" See \ref AudioCodecPref */
void ast_codec_pref_init(struct ast_codec_pref *pref);

/*! \brief Codec located at a particular place in the preference index See \ref AudioCodecPref */
int ast_codec_pref_index(struct ast_codec_pref *pref, int index);

/*! \brief Remove audio a codec from a preference list */
void ast_codec_pref_remove(struct ast_codec_pref *pref, int format);

/*! \brief Append a audio codec to a preference list, removing it first if it was already there 
*/
int ast_codec_pref_append(struct ast_codec_pref *pref, int format);

/*! \brief Prepend an audio codec to a preference list, removing it first if it was already there 
*/
void ast_codec_pref_prepend(struct ast_codec_pref *pref, int format, int only_if_existing);

/*! \brief Select the best audio format according to preference list from supplied options. 
   If "find_best" is non-zero then if nothing is found, the "Best" format of 
   the format list is selected, otherwise 0 is returned. */
int ast_codec_choose(struct ast_codec_pref *pref, int formats, int find_best);

/*! \brief Set packet size for codec
*/
int ast_codec_pref_setsize(struct ast_codec_pref *pref, int format, int framems);

/*! \brief Get packet size for codec
*/
struct ast_format_list ast_codec_pref_getsize(struct ast_codec_pref *pref, int format);

/*! \brief Parse an "allow" or "deny" line in a channel or device configuration 
        and update the capabilities mask and pref if provided.
	Video codecs are not added to codec preference lists, since we can not transcode
 */
void ast_parse_allow_disallow(struct ast_codec_pref *pref, int *mask, const char *list, int allowing);

/*! \brief Dump audio codec preference list into a string */
int ast_codec_pref_string(struct ast_codec_pref *pref, char *buf, size_t size);

/*! \brief Shift an audio codec preference list up or down 65 bytes so that it becomes an ASCII string */
void ast_codec_pref_convert(struct ast_codec_pref *pref, char *buf, size_t size, int right);

/*! \brief Returns the number of samples contained in the frame */
int ast_codec_get_samples(struct ast_frame *f);

/*! \brief Returns the number of bytes for the number of samples of the given format */
int ast_codec_get_len(int format, int samples);

/*! \brief Appends a frame to the end of a list of frames, truncating the maximum length of the list */
struct ast_frame *ast_frame_enqueue(struct ast_frame *head, struct ast_frame *f, int maxlen, int dupe);


/*! \brief Gets duration in ms of interpolation frame for a format */
static inline int ast_codec_interp_len(int format) 
{ 
	return (format == AST_FORMAT_ILBC) ? 30 : 20;
}

/*!
  \brief Adjusts the volume of the audio samples contained in a frame.
  \param f The frame containing the samples (must be AST_FRAME_VOICE and AST_FORMAT_SLINEAR)
  \param adjustment The number of dB to adjust up or down.
  \return 0 for success, non-zero for an error
 */
int ast_frame_adjust_volume(struct ast_frame *f, int adjustment);

/*!
  \brief Sums two frames of audio samples.
  \param f1 The first frame (which will contain the result)
  \param f2 The second frame
  \return 0 for success, non-zero for an error

  The frames must be AST_FRAME_VOICE and must contain AST_FORMAT_SLINEAR samples,
  and must contain the same number of samples.
 */
int ast_frame_slinear_sum(struct ast_frame *f1, struct ast_frame *f2);

/*!
 * \brief Get the sample rate for a given format.
 */
static force_inline int ast_format_rate(int format)
{
	if (format == AST_FORMAT_G722)
		return 16000;

	return 8000;
}

#if defined(__cplusplus) || defined(c_plusplus)
}
#endif

#endif /* _ASTERISK_FRAME_H */
