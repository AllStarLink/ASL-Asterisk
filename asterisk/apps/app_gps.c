/* #define	OLD_ASTERISK */
/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 1999 - 2006, Digium, Inc.
 *
 * Copyright (C) 2010, Jim Dixon/WB6NIL
 * Jim Dixon, WB6NIL <jim@lambdatel.com>
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
 * \brief GPS device interface module
 * 
 * \author Jim Dixon, WB6NIL <jim@lambdatel.com>
 *
 * \ingroup applications
 */

/*** MODULEINFO
 ***/

/* The following are the recognized APRS icon codes: 

NOTE: Since the semicolon (';') is recognized by the
Asterisk config subsystem as a comment, we use the
question-mark ('?') instead when we want to specify
a 'portable tent'.


! - 3 vert bars (EMERGENCY)
" - RAIN
# - DIGI

$ - SUN (always yellow)
% - DX CLUSTER
& - HF GATEway
' - AIRCRAFT (small)
( - CLOUDY
) - Hump
* - SNOW
+ - Cross
, - reverse L shape

- - QTH
. - X
/ - Dot
0-9 Numerial Boxes
: - FIRE
? - Portable tent (note different then standard ';')
< - Advisory flag
= - RAILROAD ENGINE
> - CAR (SSID-9)

@ - HURRICANE or tropical storm
A - reserved
B - Blowing Snow  (also BBS)
C - reserved
D - Drizzle
E - Smoke
F - Freezing rain
G - Snow Shower
H - Haze

I - Rain Shower   (Also TCP-IP)
J - Lightening
K - School
L - Lighthouse
M - MacAPRS
N - Navigation Buoy
O - BALLOON
P - Police
Q - QUAKE

R - RECREATIONAL VEHICLE
S - Space/Satellite
T - THUNDERSTORM
U - BUS
V - VORTAC Nav Aid
W - National WX Service Site
X - HELO  (SSID-5)
Y - YACHT (sail SSID-6)
Z - UNIX X-APRS

[ - RUNNER
\ - TRIANGLE   (DF)
] - BOX with X (PBBS's)
^ - LARGE AIRCRAFT
_ - WEATHER SURFACE CONDITIONS (always blue)
` - Satellite Ground Station
a - AMBULANCE
b - BIKE
c - DX spot by callsign

d - Dual Garage (Fire dept)
e - SLEET
f - FIRE TRUCK
g - GALE FLAGS
h - HOSPITAL
i - IOTA (islands on the air)
j - JEEP (SSID-12)
k - TRUCK (SSID-14)
l - AREAS (box,circle,line,triangle) See below

m - MILEPOST (box displays 2 letters if in {35})
n - small triangle
o - small circle
p - PARTLY CLOUDY
q - GRID SQUARE (4 digit.  Not shown below 128 miles)
r - ANTENNA
s - SHIP (pwr boat SSID-8)
t - TORNADO
u - TRUCK (18 wheeler)

v - VAN (SSID-15)
w - FLOODING(water)
x - diamond (NODE)
y - YAGI @ QTH
z - WinAPRS
{ - FOG
| - reserved (Stream Switch)
} - diamond 
~ - reserved (Stream Switch)

*/

#include "asterisk.h"

#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <errno.h>
#include <unistd.h>
#include <stdlib.h>
#include <arpa/inet.h>
#include <fcntl.h>
#include <sys/ioctl.h>
#include <ctype.h>
#include <search.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
#include <pthread.h>
#include <signal.h>
#include <termios.h>
#include <math.h>

#include "asterisk/lock.h"
#include "asterisk/channel.h"
#include "asterisk/config.h"
#include "asterisk/logger.h"
#include "asterisk/module.h"
#include "asterisk/pbx.h"
#include "asterisk/options.h"
#include "asterisk/utils.h"
#include "asterisk/app.h"
#include "asterisk/translate.h"
#include "asterisk/cli.h"

#ifdef OLD_ASTERISK
#define ast_free free
#define ast_malloc malloc
#endif

#define	GPS_DEFAULT_SERVER "second.aprs.net"
#define	GPS_DEFAULT_PORT "10151"
#define	GPS_DEFAULT_COMMENT "Asterisk app_rpt server"
#define	GPS_DEFAULT_BAUDRATE B4800
#define	GPS_DEFAULT_ICON '>' /* car */
#define	GPS_WORK_FILE "/tmp/gps.tmp"
#define	GPS_DATA_FILE "/tmp/gps.dat"
#define	GPS_UPDATE_SECS 30
#define	SERIAL_MAXMS 10000

static char *config = "gps.conf";

static char *app = "GPS";

static char *synopsis = "GPS Device Interface Module";

static char *descrip = "Interfaces app_rpt to a NMEA 0183 (GGA records) compliant GPS device\n";

static  pthread_t gps_thread = 0;
static int run_forever = 1;
static char *call,*password,*server,*port,*comment,*comport,icon;
static char *deflat,*deflon,*defelev;
char power,height,gain,dir;
static int baudrate;
static int debug = 0;


/*
* Break up a delimited string into a table of substrings
*
* str - delimited string ( will be modified )
* strp- list of pointers to substrings (this is built by this function), NULL will be placed at end of list
* limit- maximum number of substrings to process
* delim- user specified delimeter
* quote- user specified quote for escaping a substring. Set to zero to escape nothing.
*
* Note: This modifies the string str, be suer to save an intact copy if you need it later.
*
* Returns number of substrings found.
*/
	

static int explode_string(char *str, char *strp[], int limit, char delim, char quote)
{
int     i,l,inquo;

        inquo = 0;
        i = 0;
        strp[i++] = str;
        if (!*str)
           {
                strp[0] = 0;
                return(0);
           }
        for(l = 0; *str && (l < limit) ; str++)
        {
		if(quote)
		{
                	if (*str == quote)
                   	{	
                        	if (inquo)
                           	{
                                	*str = 0;
                                	inquo = 0;
                           	}
                        	else
                           	{
                                	strp[i - 1] = str + 1;
                                	inquo = 1;
                           	}
			}
		}	
                if ((*str == delim) && (!inquo))
                {
                        *str = 0;
			l++;
                        strp[i++] = str + 1;
                }
        }
        strp[i] = 0;
        return(i);

}

static int getserialchar(int fd)
{
int	res;
char	c;
int	i;
fd_set	fds;
struct	timeval tv;


	for(i = 0; (i < (SERIAL_MAXMS / 100)) && run_forever; i++)
	{
		tv.tv_sec = 0;
		tv.tv_usec = 100000;
	        FD_ZERO(&fds);
	        FD_SET(fd, &fds);
		res = ast_select(fd + 1,&fds,NULL,NULL,&tv);
		if (res < 0) return -1;
		if (res)
		{
			if (read(fd,&c,1) < 1) return -1;
			return(c);
		}
	}
	return(0);
}

static int getserial(int fd, char *str, int max)
{
int	i;
char	c;

	for(i = 0; (i < max) && run_forever; i++)
	{
		c = getserialchar(fd);
		if (c < 1) return(c);
		if ((i == 0) && (c < ' '))
		{
			i--;
			continue;
		}
		if (c < ' ') break;
		str[i] = c;
	}
	str[i] = 0;
	return(i);
}

static void *gpsthread(void *data)
{
char	buf[300],c,*strs[100],lat[100],lon[100],basecall[20];
char	*cp,latc,lonc,astr[50];
int	res,i,n,fd,sockfd,has_comport = 0;
float	mylat,lata,latb,latd;
float	mylon,lona,lonb,lond;
FILE	*fp;
time_t	t,lastupdate;
struct termios mode;
struct ast_hostent ahp;
struct hostent *hp;
struct sockaddr_in servaddr;




	if (comport) has_comport = 1;
	else comport = "/dev/null";

	fd = open(comport,O_RDWR);
	if (fd == -1)
	{
		ast_log(LOG_WARNING,"Cannot open serial port %s\n",comport);
		goto err;
	}

	if (has_comport)
	{
		memset(&mode, 0, sizeof(mode));
		if (tcgetattr(fd, &mode)) {
			ast_log(LOG_WARNING, "Unable to get serial parameters on %s: %s\n", comport, strerror(errno));
			close(fd);
			goto err;
		}
#ifndef	SOLARIS
		cfmakeraw(&mode);
#else
	        mode.c_iflag &= ~(IGNBRK|BRKINT|PARMRK|ISTRIP
                        |INLCR|IGNCR|ICRNL|IXON);
	        mode.c_lflag &= ~(ECHO|ECHONL|ICANON|ISIG|IEXTEN);
	        mode.c_cflag &= ~(CSIZE|PARENB|CRTSCTS);
	        mode.c_cflag |= CS8;
		mode.c_cc[VTIME] = 3;
		mode.c_cc[VMIN] = 1; 
#endif

		cfsetispeed(&mode, baudrate);
		cfsetospeed(&mode, baudrate);
		if (tcsetattr(fd, TCSANOW, &mode)) 
		{
			ast_log(LOG_WARNING, "Unable to set serial parameters on %s: %s\n", comport, strerror(errno));
			close(fd);
			goto err;
		}
	}
	usleep(100000);
	lastupdate = 0;

	while(run_forever)
	{
		if (has_comport)
		{
			res = getserial(fd,buf,sizeof(buf) - 1);
			if (res < 0)
			{
				ast_log(LOG_ERROR,"GPS fatal error!!\n");
				continue;
			}
		}
		else
		{
			sleep(10);
			res = 0;
		}
		if (!res)
		{
			if ((!deflat) || (!deflon)) 
			{
				if (has_comport) ast_log(LOG_WARNING,"GPS timeout\n");
				continue;
			}
			if (has_comport) ast_log(LOG_WARNING,"GPS timeout -- Using default (fixed location) parameters instead\n");
			mylat = strtof(deflat,NULL);
			mylon = strtof(deflon,NULL);
			latc = (mylat >= 0.0) ? 'N' : 'S';
			lonc = (mylon >= 0.0) ? 'E' : 'W';
			lata = fabs(mylat);
			lona = fabs(mylon);
			latb = (lata - floor(lata)) * 60;
                        latd = (latb - floor(latb)) * 100 + 0.5;
                        lonb = (lona - floor(lona)) * 60;
                        lond = (lonb - floor(lonb)) * 100 + 0.5;
			sprintf(lat,"%02d%02d.%02d%c",(int)lata,(int)latb,(int)latd,latc);
			sprintf(lon,"%03d%02d.%02d%c",(int)lona,(int)lonb,(int)lond,lonc);
			if (defelev)
			{
				mylat = strtof(defelev,NULL);
				lata = (mylat - floor(mylat)) * 10 + 0.5;
				sprintf(astr,"%03d.%1d",(int)mylat,(int)lata);
				strs[9] = astr;
			}
			else
				strs[9] = "000.0";
			strs[10] = "M";
		}
		else
		{
			c = 0;
			if (buf[0] != '$')
			{
				ast_log(LOG_WARNING,"GPS Invalid data format (no '$' at beginning)\n");
				continue;
			}
			for(i = 1; buf[i]; i++)
			{
				if (buf[i] == '*') break;
				c ^= buf[i];
			}
			if ((!buf[i]) || (strlen(buf) < (i + 3)))
			{
				ast_log(LOG_WARNING,"GPS Invalid data format (checksum format)\n");
				continue;
			}
			if ((sscanf(buf + i + 1,"%x",&i) != 1) || (c != i))
			{
				ast_log(LOG_WARNING,"GPS Invalid checksum\n");
				continue;
			}
			n = explode_string(buf,strs,100,',','\"');
			if (!n)
			{
				ast_log(LOG_WARNING,"GPS Invalid data format (no data)\n");
				continue;
			}
			if (strcasecmp(strs[0],"$GPGGA")) continue;
			if (n != 15)
			{
				ast_log(LOG_WARNING,"GPS Invalid data format (invalid format for GGA record)\n");
				continue;
			}
			if (*strs[6] < '1')
			{
				ast_log(LOG_WARNING,"GPS data not available\n");
				continue;
			}
			snprintf(lat,sizeof(lat) - 1,"%s%s",strs[2],strs[3]);
			snprintf(lon,sizeof(lon) - 1,"%s%s",strs[4],strs[5]);
		}
		if (debug) ast_log(LOG_NOTICE,"got lat: %s, long: %s, elev: %s%s\n",lat,lon,strs[9],strs[10]);
		fp = fopen(GPS_WORK_FILE,"w");
		if (!fp)
		{
			ast_log(LOG_ERROR,"Unable to open GPS work file!!\n");
			continue;
		}
		time(&t);
		fprintf(fp,"%u %s %s %s%s\n",(unsigned int) t,lat,lon,
			strs[9],strs[10]);
		fclose(fp);
		sprintf(buf,"/bin/mv %s %s > /dev/null 2>&1",GPS_WORK_FILE,GPS_DATA_FILE);
		ast_safe_system(buf);
		if (t < (lastupdate + GPS_UPDATE_SECS)) continue;
		lastupdate = t;	

		sockfd = socket(AF_INET, SOCK_STREAM, 0);
		if (sockfd < 0)
		{
			ast_log(LOG_ERROR,"Error opening socket\n");
			break;
		}
		bzero(&servaddr, sizeof(servaddr));
		servaddr.sin_family = AF_INET;
		servaddr.sin_port = htons(atoi(port));
		hp = ast_gethostbyname(server, &ahp);
		if (!hp)
		{
			ast_log(LOG_WARNING, "server %s cannot be found!!\n",server);
			close(sockfd);
			continue;
		}
		memcpy(&servaddr.sin_addr,hp->h_addr,sizeof(in_addr_t));
		if (connect(sockfd,(struct sockaddr *) &servaddr, sizeof(servaddr)) < 0)
		{
			ast_log(LOG_WARNING, "server %s cannot be found!!\n",server);
			close(sockfd);
			continue;
		}
		strncpy(basecall,call,sizeof(basecall) - 1);
		cp = strchr(basecall,'-');
		if (cp) *cp = 0;
		cp = strchr(lat,'.');
		if (cp && (strlen(cp) >= 3))
		{
			*(cp + 3) = lat[strlen(lat) - 1];
			*(cp + 4) = 0;
		}
		cp = strchr(lon,'.');
		if (cp && (strlen(cp) >= 3)) 
		{
			*(cp + 3) = lon[strlen(lon) - 1];
			*(cp + 4) = 0;
		}
		sprintf(buf,"user %s pass %s vers \"Asterisk app_gps\"\n",
			call,password);
		if (send(sockfd,buf,strlen(buf),0) < 0)
		{
			ast_log(LOG_WARNING, "Can not send signon to server\n");
			close(sockfd);
			continue;
		}
		if (debug) ast_log(LOG_NOTICE,"sent packet: %s",buf);
		sprintf(buf,"%s>APRS,qAR,%s-VS:=%s/%s%cPHG%d%d%d%d/%s\n",
			call,basecall,lat,lon,icon,power,height,gain,dir,comment);
		if (send(sockfd,buf,strlen(buf),0) < 0)
		{
			ast_log(LOG_WARNING, "Can not send signon to server\n");
			close(sockfd);
			continue;
		}
		if (debug) ast_log(LOG_NOTICE,"sent packet: %s",buf);
		close(sockfd);	
	}
	close(fd);





err:
	if (call) ast_free(call);
	if (password) ast_free(password);
	if (server) ast_free(server);
	if (port) ast_free(port);
	if (comment) ast_free(comment);
	if (comport) ast_free(comport);
	pthread_exit(NULL);
	return NULL;
}

static int gps_exec(struct ast_channel *chan, void *data)
{
	return 0;
}

#ifdef	OLD_ASTERISK
int unload_module()
#else
static int unload_module(void)
#endif
{
int	res;

	run_forever = 0;
	res = ast_unregister_application(app);
	return res;
}

#ifndef	OLD_ASTERISK
static
#endif
int load_module(void)
{
	struct ast_config *cfg = NULL;
        char *ctg = "general",*val;
	int res;
	pthread_attr_t attr;

#ifdef  NEW_ASTERISK
        struct ast_flags zeroflag = {0};
#endif

#ifdef  NEW_ASTERISK
        if (!(cfg = ast_config_load(config,zeroflag))) {
#else
        if (!(cfg = ast_config_load(config))) {
#endif
                ast_log(LOG_NOTICE, "Unable to load config %s\n", config);
                return AST_MODULE_LOAD_DECLINE;
        }
	val = (char *) ast_variable_retrieve(cfg,ctg,"call");	
	if (val) call = ast_strdup(val); else call = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"password");	
	if (val) password = ast_strdup(val); else password = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"server");	
	if (val) server = ast_strdup(val); else server = ast_strdup(GPS_DEFAULT_SERVER);
	val = (char *) ast_variable_retrieve(cfg,ctg,"port");	
	if (val) port = ast_strdup(val); else port = ast_strdup(GPS_DEFAULT_PORT);
	val = (char *) ast_variable_retrieve(cfg,ctg,"comment");	
	if (val) comment = ast_strdup(val); else comment = ast_strdup(GPS_DEFAULT_COMMENT);
	val = (char *) ast_variable_retrieve(cfg,ctg,"comport");	
	if (val) comport = ast_strdup(val); else comport = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"lat");	
	if (val) deflat = ast_strdup(val); else deflat = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"lon");	
	if (val) deflon = ast_strdup(val); else deflon = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"elev");	
	if (val) defelev = ast_strdup(val); else defelev = NULL;
	val = (char *) ast_variable_retrieve(cfg,ctg,"power");	
	if (val) power = (char)strtol(val,NULL,0); else power = 0;
	val = (char *) ast_variable_retrieve(cfg,ctg,"height");	
	if (val) height = (char)strtol(val,NULL,0); else height = 0;
	val = (char *) ast_variable_retrieve(cfg,ctg,"gain");	
	if (val) gain = (char)strtol(val,NULL,0); else gain = 0;
	val = (char *) ast_variable_retrieve(cfg,ctg,"dir");	
	if (val) dir = (char)strtol(val,NULL,0); else dir = 0;
	if (val)
	{
	    switch(atoi(val))
	    {
		case 2400:
			baudrate = B2400;
			break;
		case 4800:
			baudrate = B4800;
			break;
		case 19200:
			baudrate = B19200;
			break;
		case 38400:
			baudrate = B38400;
			break;
		case 57600:
			baudrate = B57600;
			break;
		default:
			ast_log(LOG_ERROR,"%s is not valid baud rate for iospeed\n",val);
			break;
	    }
	} else baudrate = GPS_DEFAULT_BAUDRATE;
	val = (char *) ast_variable_retrieve(cfg,ctg,"debug");	
	if (val) debug = ast_true(val);
	val = (char *) ast_variable_retrieve(cfg,ctg,"icon");	
	if (val && *val) icon = *val; else icon = GPS_DEFAULT_ICON;
	if (icon == '?') icon = ';';  /* allow for entry of portable tent */

	if ((!call) || (!password))
	{
		ast_log(LOG_ERROR,"You must specify call and password\n");
                return AST_MODULE_LOAD_DECLINE;
	}		
        ast_config_destroy(cfg);
        cfg = NULL; 

        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
        ast_pthread_create(&gps_thread,&attr,gpsthread,NULL);
	res = ast_register_application(app, gps_exec, synopsis, descrip);
	return res;
}

#ifdef	OLD_ASTERISK
char *description()
{
	return (char *)el_tech.description;
}

int usecount()
{
	return usecnt;
}

char *key()
{
	return ASTERISK_GPL_KEY;
}
#else
AST_MODULE_INFO_STANDARD(ASTERISK_GPL_KEY, "GPS interface module");
#endif

