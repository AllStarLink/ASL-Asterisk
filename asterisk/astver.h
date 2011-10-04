/* #define OLD_ASTERISK */
/* #define NEW_ASTERISK */

/* ISEMPTY macro -- completely and utterly whacked way of testing to see if a preprocessor macro
is empty or not */

#define _ARG16(_0, _1, _2, _3, _4, _5, _6, _7, _8, _9, _10, _11, _12, _13, _14, _15, ...) _15
#define HAS_COMMA(...) _ARG16(__VA_ARGS__, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 1, 0)
#define _TRIGGER_PARENTHESIS_(...) ,
 
#define ISEMPTY(...)                                                    \
_ISEMPTY(                                                               \
          /* test if there is just one argument, eventually an empty    \
             one */                                                     \
          HAS_COMMA(__VA_ARGS__),                                       \
          /* test if _TRIGGER_PARENTHESIS_ together with the argument   \
             adds a comma */                                            \
          HAS_COMMA(_TRIGGER_PARENTHESIS_ __VA_ARGS__),                 \
          /* test if the argument together with a parenthesis           \
             adds a comma */                                            \
          HAS_COMMA(__VA_ARGS__ (~)),                                   \
          /* test if placing it between _TRIGGER_PARENTHESIS_ and the   \
             parenthesis adds a comma */                                \
          HAS_COMMA(_TRIGGER_PARENTHESIS_ __VA_ARGS__ (~))              \
          )

#define PASTE5(_0, _1, _2, _3, _4) _0 ## _1 ## _2 ## _3 ## _4
#define _ISEMPTY(_0, _1, _2, _3) HAS_COMMA(PASTE5(_IS_EMPTY_CASE_, _0, _1, _2, _3))
#define _IS_EMPTY_CASE_0001 ,


#include "asterisk/version.h"

#if defined(ASTERISK_VERSION_NUM) && !ISEMPTY(ASTERISK_VERSION_NUM)
#if ASTERISK_VERSION_NUM >= 10600
#define NEW_ASTERISK
#endif
#endif

#ifdef	NEW_ASTERISK
#define AST_FRAME_DATA(x) x.data.ptr
#define AST_FRAME_DATAP(x) x->data.ptr
#else
#define	AST_FRAME_DATA(x) x.data
#define	AST_FRAME_DATAP(x) x->data
#endif

#ifdef  HAVE_DAHDI
#define DAHDI_CHANNEL_NAME "DAHDI"
#define DAHDI_PSEUDO_NAME "DAHDI/pseudo"
#define DAHDI_PSEUDO_DEV_NAME "/dev/dahdi/pseudo"
#include <dahdi/user.h>
#include <dahdi/tonezone.h>
#else
#include <zaptel/tonezone.h>
#define DAHDI_CHANNEL_NAME "Zap"
#define DAHDI_PSEUDO_NAME "Zap/pseudo"
#define DAHDI_PSEUDO_DEV_NAME "/dev/zap/pseudo"
#include "asterisk/dahdi_compat.h"
#define	DAHDI_IOMUX_WRITEEMPTY ZT_IOMUX_WRITEEMPTY
#define	DAHDI_IOMUX_NOWAIT ZT_IOMUX_NOWAIT
#define DAHDI_FLUSH_EVENT ZT_FLUSH_EVENT
#define	DAHDI_CONF_MONITOR ZT_CONF_MONITOR
#define DAHDI_CONF_MONITORTX ZT_CONF_MONITORTX
#define DAHDI_RADIO_GETPARAM ZT_RADIO_GETPARAM
#define DAHDI_RADIO_SETPARAM ZT_RADIO_SETPARAM
#define DAHDI_RADPAR_IGNORECT ZT_RADPAR_IGNORECT
#define DAHDI_RADPAR_NOENCODE ZT_RADPAR_NOENCODE
#define DAHDI_RADPAR_REMMODE ZT_RADPAR_REMMODE
#define DAHDI_RADPAR_REM_RBI1 ZT_RADPAR_REM_RBI1
#define DAHDI_RADPAR_REM_NONE ZT_RADPAR_REM_NONE
#define DAHDI_RADPAR_REMCOMMAND ZT_RADPAR_REMCOMMAND
#define DAHDI_RADPAR_UIOMODE ZT_RADPAR_UIOMODE
#define	DAHDI_RADPAR_UIODATA ZT_RADPAR_UIODATA
#define DAHDI_RADPAR_REM_SERIAL ZT_RADPAR_REM_SERIAL
#define DAHDI_RADPAR_REM_SERIAL_ASCII ZT_RADPAR_REM_SERIAL_ASCII
#define	DAHDI_RADPAR_REM_NONE ZT_RADPAR_REM_NONE
#define dahdi_radio_param zt_radio_param
#define dahdi_confinfo zt_confinfo
#define dahdi_bufferinfo zt_bufferinfo
#define dahdi_params zt_params
#endif
