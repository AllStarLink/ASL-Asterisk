/*
 *
 *  */


#include "allstar/allstarutils.h"

/*
 * DAQ subsystem
 */

#define MAX_DAQ_RANGES 16  /* Max number of entries for range() */
#define MAX_DAQ_ENTRIES 10 /* Max number of DAQ devices */
#define MAX_DAQ_NAME 32 /* Max length of a device name */
#define MAX_DAQ_DEV 64 /* Max length of a daq device path */
#define MAX_METER_FILES 10 /* Max number of sound files in a meter def. */
#define DAQ_RX_TIMEOUT 50 /* Receive time out for DAQ subsystem */
#define DAQ_ADC_ACQINT 10 /* Acquire interval in sec. for ADC channels */
#define ADC_HIST_TIME 300 /* Time  in sec. to calculate short term avg, high and low peaks from. */
#define ADC_HISTORY_DEPTH ADC_HIST_TIME/DAQ_ADC_ACQINT

enum{DAQ_PS_IDLE = 0, DAQ_PS_START, DAQ_PS_BUSY, DAQ_PS_IN_MONITOR};
enum{DAQ_CMD_IN, DAQ_CMD_ADC, DAQ_CMD_OUT, DAQ_CMD_PINSET, DAQ_CMD_MONITOR};
enum{DAQ_SUB_CUR = 0, DAQ_SUB_MIN, DAQ_SUB_MAX, DAQ_SUB_STMIN, DAQ_SUB_STMAX, DAQ_SUB_STAVG};
enum{DAQ_PT_INADC = 1, DAQ_PT_INP, DAQ_PT_IN, DAQ_PT_OUT};
enum{DAQ_TYPE_UCHAMELEON};



/*
 * Structs used in the DAQ code
 */

struct daq_tx_entry_tag{
	char txbuff[32];
	struct daq_tx_entry_tag *prev;
	struct daq_tx_entry_tag *next;
};

struct daq_pin_entry_tag{
	int num;
	int pintype;
	int command;
	int state;
	int value;
	int valuemax;
	int valuemin;
	int ignorefirstalarm;
	int alarmmask;
	int adcnextupdate;
	int adchistory[ADC_HISTORY_DEPTH];
	char alarmargs[64];
	void (*monexec)(struct daq_pin_entry_tag *);
	struct daq_pin_entry_tag *next;
};


struct daq_entry_tag{
	char name[MAX_DAQ_NAME];
	char dev[MAX_DAQ_DEV];
	int type;
	int fd;
	int active;
	time_t adcacqtime;
	pthread_t threadid;
	ast_mutex_t lock;
	struct daq_tx_entry_tag *txhead;
	struct daq_tx_entry_tag *txtail;
	struct daq_pin_entry_tag *pinhead;
	struct daq_entry_tag *next;
};

struct daq_tag{
	int ndaqs;
	struct daq_entry_tag *hw;
};

/*
 * DAQ variables
 */

struct daq_tag daq;



/*
 * ***********************************
 * Uchameleon specific routines      *
 * ***********************************
 */

int uchameleon_do_long( struct daq_entry_tag *t, int pin,
int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);

static int matchkeyword(char *string, char **param, char *keywords[]);
static int explode_string1(char *str, char *strp[], int limit, char delim, char quote);
void *uchameleon_monitor_thread(void *this);
// char *strupr(char *str);

/*
 * **************************
 * Generic DAQ functions    *
 * **************************
 */


int saynum1(struct ast_channel *mychannel, int num);
static int sayfile(struct ast_channel *mychannel,char *fname);
int wait_interval(struct rpt *myrpt, int type, struct ast_channel *chan);



static void rpt_telem_select1(struct rpt *myrpt, int command_source, struct rpt_link *mylink)
{
int	src;

	if (mylink && mylink->chan)
	{
		src = LINKMODE_GUI;
		if (mylink->phonemode) src = LINKMODE_PHONE;
		else if (!strncasecmp(mylink->chan->name,"echolink",8)) src = LINKMODE_ECHOLINK;
		else if (!strncasecmp(mylink->chan->name,"tlb",8)) src = LINKMODE_TLB;
		if (myrpt->p.linkmodedynamic[src] && (mylink->linkmode >= 1) &&
		    (mylink->linkmode < 0x7ffffffe))
				mylink->linkmode = LINK_HANG_TIME;
	}
	if (!myrpt->p.telemdynamic) return;
	if (myrpt->telemmode == 0) return;
	if (myrpt->telemmode == 0x7fffffff) return;
	myrpt->telemmode = TELEM_HANG_TIME;
	return;
}

/*
 * Open a daq device
 */

struct daq_entry_tag *daq_open(int type, char *name, char *dev)
{
	int fd;
	struct daq_entry_tag *t;


	if(!name)
		return NULL;

        if((t = ast_malloc(sizeof(struct daq_entry_tag))) == NULL){
		ast_log(LOG_WARNING,"daq_open out of memory\n");
		return NULL;
	}


	memset(t, 0, sizeof(struct daq_entry_tag));


	/* Save the device path for open*/
	if(dev){
		strncpy(t->dev, dev, MAX_DAQ_DEV);
		t->dev[MAX_DAQ_DEV - 1] = 0;
	}



	/* Save the name*/
	strncpy(t->name, name, MAX_DAQ_NAME);
	t->dev[MAX_DAQ_NAME - 1] = 0;


	switch(type){
		case DAQ_TYPE_UCHAMELEON:
			if((fd = uchameleon_open(t)) == -1){
				ast_free(t);
				return NULL;
			}
			break;

		default:
			ast_free(t);
			return NULL;
	}
	t->type = type;
	return t;
}

/*
 * Close a daq device
 */


int daq_close(struct daq_entry_tag *t)
{
	int res  = -1;

	if(!t)
		return res;

	switch(t->type){
		case DAQ_TYPE_UCHAMELEON:
			res = uchameleon_close(t);
			break;
		default:
			break;
	}

	ast_free(t);
	return res;
}

/*
 * Look up a device entry for a particular device name
 */

struct daq_entry_tag *daq_devtoentry(char *name)
{
	struct daq_entry_tag *e = daq.hw;

	while(e){
		if(!strcmp(name, e->name))
			break;
		e = e->next;
	}
	return e;
}



/*
 * Do something with the daq subsystem
 */

int daq_do_long( struct daq_entry_tag *t, int pin, int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2)
{
	int res = -1;

	switch(t->type){
		case DAQ_TYPE_UCHAMELEON:
			res = uchameleon_do_long(t, pin, cmd, exec, arg1, arg2);
			break;
		default:
			break;
	}
	return res;
}

/*
 * Short version of above
 */

int daq_do( struct daq_entry_tag *t, int pin, int cmd, int arg1)
{
	int a1 = arg1;

	return daq_do_long(t, pin, cmd, NULL, &a1, NULL);
}


/*
 * Function to reset the long term minimum or maximum
 */

int daq_reset_minmax(char *device, int pin, int minmax)
{
	int res = -1;
	struct daq_entry_tag *t;

	if(!(t = daq_devtoentry(device)))
		return -1;
	switch(t->type){
		case DAQ_TYPE_UCHAMELEON:
			res = uchameleon_reset_minmax(t, pin, minmax);
			break;
		default:
			break;
	}
	return res;
}

/*
 * Initialize DAQ subsystem
 */

void daq_init(struct ast_config *cfg)
{
	struct ast_variable *var;
	struct daq_entry_tag **t_next, *t = NULL;
	char s[64];
	daq.ndaqs = 0;
	t_next = &daq.hw;
	var = ast_variable_browse(cfg,"daq-list");
	while(var){
		char *p;
		if(strncmp("device",var->name,6)){
			ast_log(LOG_WARNING,"Error in daq_entries stanza on line %d\n", var->lineno);
			break;
		}
		strncpy(s,var->value,sizeof(s)); /* Make copy of device entry */
		if(!(p = (char *) ast_variable_retrieve(cfg,s,"hwtype"))){
			ast_log(LOG_WARNING,"hwtype variable required for %s stanza\n", s);
			break;
		}
		if(strncmp(p,"uchameleon",10)){
			ast_log(LOG_WARNING,"Type must be uchameleon for %s stanza\n", s);
			break;
		}
                if(!(p = (char *) ast_variable_retrieve(cfg,s,"devnode"))){
                        ast_log(LOG_WARNING,"devnode variable required for %s stanza\n", s);
                        break;
                }
		if(!(t = daq_open(DAQ_TYPE_UCHAMELEON, (char *) s, (char *) p))){
			ast_log(LOG_WARNING,"Cannot open device name %s\n",p);
			break;
		}
		/* Add to linked list */
		*t_next = t;
		t_next = &t->next;

		daq.ndaqs++;
		if(daq.ndaqs >= MAX_DAQ_ENTRIES)
			break;
		var = var->next;
	}


}

/*
 * Uninitialize DAQ Subsystem
 */

void daq_uninit(void)
{
	struct daq_entry_tag *t_next, *t;

	/* Free daq memory */
	t = daq.hw;
	while(t){
		t_next = t->next;
		daq_close(t);
		t = t_next;
	}
	daq.hw = NULL;
}

/*
 * Start the Uchameleon monitor thread
 */




int uchameleon_thread_start(struct daq_entry_tag *t)
{
	int res, tries = 50;
	pthread_attr_t attr;


	ast_mutex_init(&t->lock);


	/*
 	* Start up uchameleon monitor thread
 	*/

       	pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	res = ast_pthread_create(&t->threadid,&attr,uchameleon_monitor_thread,(void *) t);
	if(res){
		ast_log(LOG_WARNING, "Could not start uchameleon monitor thread\n");
		return -1;
	}

	ast_mutex_lock(&t->lock);
	while((!t->active)&&(tries)){
		ast_mutex_unlock(&t->lock);
		usleep(100*1000);
		ast_mutex_lock(&t->lock);
		tries--;
	}
	ast_mutex_unlock(&t->lock);

	if(!tries)
		return -1;


        return 0;
}

int uchameleon_connect(struct daq_entry_tag *t)
{
	int count;
	char *idbuf = "id\n";
	char *ledbuf = "led on\n";
	char *expect = "Chameleon";
	char rxbuf[20];

        if((t->fd = serial_open(t->dev, B115200, 0)) == -1){
               	ast_log(LOG_WARNING, "serial_open on %s failed!\n", t->name);
                return -1;
        }
        if((count = serial_io(t->fd, idbuf, rxbuf, strlen(idbuf), 14, DAQ_RX_TIMEOUT, 0x0a)) < 1){
              	ast_log(LOG_WARNING, "serial_io on %s failed\n", t->name);
		close(t->fd);
		t->fd = -1;
                return -1;
        }
	if(debug >= 3)
        	ast_log(LOG_NOTICE,"count = %d, rxbuf = %s\n",count,rxbuf);
	if((count != 13)||(strncmp(expect, rxbuf+4, sizeof(expect)))){
		ast_log(LOG_WARNING, "%s is not a uchameleon device\n", t->name);
		close(t->fd);
		t->fd = -1;
		return -1;
	}
	/* uchameleon LED on solid once we communicate with it successfully */

	if(serial_io(t->fd, ledbuf, NULL, strlen(ledbuf), 0, DAQ_RX_TIMEOUT, 0) == -1){
		ast_log(LOG_WARNING, "Can't set LED on uchameleon device\n");
		close(t->fd);
		t->fd= -1;
		return -1;
	}
	return 0;
}

/*
 * Uchameleon alarm handler
 */


void uchameleon_alarm_handler(struct daq_pin_entry_tag *p)
{
	char *valuecopy;
	int i, busy;
	char *s;
	char *argv[7];
	int argc;


	if(!(valuecopy = ast_strdup(p->alarmargs))){
		ast_log(LOG_ERROR,"Out of memory\n");
		return;
	}

	argc = explode_string1(valuecopy, argv, 6, ',', 0);

	if(debug >= 3){
		ast_log(LOG_NOTICE, "Alarm event on device %s, pin %d, state = %d\n", argv[0], p->num, p->value);
	}

	/*
 	* Node: argv[3]
 	* low function: argv[4]
 	* high function: argv[5]
 	*
 	*/
	i = busy = 0;
	s = (p->value) ? argv[5]: argv[4];
	if((argc == 6)&&(s[0] != '-')){
		for(i = 0; i < nrpts; i++){
			if(!strcmp(argv[3], rpt_vars[i].name)){

				struct rpt *myrpt = &rpt_vars[i];
				rpt_mutex_lock(&myrpt->lock);
				if ((MAXMACRO - strlen(myrpt->macrobuf)) < strlen(s)){
					rpt_mutex_unlock(&myrpt->lock);
					busy=1;
				}
				if(!busy){
					myrpt->macrotimer = MACROTIME;
					strncat(myrpt->macrobuf,s,MAXMACRO - 1);
				}
				rpt_mutex_unlock(&myrpt->lock);

			}
		}
	}
	if(argc != 6){
		ast_log(LOG_WARNING, "Not enough arguments to process alarm\n");
	}
	else if(busy){
		ast_log(LOG_WARNING, "Function decoder busy while processing alarm");
	}
	ast_free(valuecopy);
}


/*
 * Initialize pins
 */
int uchameleon_pin_init(struct daq_entry_tag *t)
{
	int i;
	struct ast_config *ourcfg;
	struct ast_variable *var,*var2;

	/* Pin Initialization */

	#ifdef	NEW_ASTERISK
		ourcfg = ast_config_load("rpt.conf",config_flags);
	#else
		ourcfg = ast_config_load("rpt.conf");
	#endif

	if(!ourcfg)
		return -1;

	var2 = ast_variable_browse(ourcfg, t->name);
	while(var2){
		unsigned int pin;
		int x = 0;
		char *pin_keywords[]={"inadc","inp","in","out",NULL};
		if((var2->name[0] < '0')||(var2->name[0] > '9')){
			var2 = var2->next;
			continue;
		}
		pin = (unsigned int) atoi(var2->name);
		i = matchkeyword1((char *)var2->value, NULL, pin_keywords);
		if(debug >= 3)
			ast_log(LOG_NOTICE, "Pin = %d, Pintype = %d\n", pin, i);
		if(i && i < 5){
			uchameleon_do_long(t, pin, DAQ_CMD_PINSET, NULL, &i, NULL);	 /* Set pin type */
			uchameleon_do_long(t, pin, DAQ_CMD_MONITOR, NULL, &x, NULL); /* Monitor off */
			if(i == DAQ_PT_OUT){
				if(debug >= 3)
					ast_log(LOG_NOTICE,"Set output pin %d low\n", pin); /* Set output pins low */
				uchameleon_do_long(t, pin, DAQ_CMD_OUT, NULL, &x, NULL);
			}
		}
		else
			ast_log(LOG_WARNING,"Invalid pin type: %s\n", var2->value);
		var2 = var2->next;
	}

	/*
 	* Alarm initialization
 	*/

	var = ast_variable_browse(ourcfg,"alarms");
	while(var){
		int ignorefirst,pin;
		char s[64];
		char *argv[7];
		struct daq_pin_entry_tag *p;


		/* Parse alarm entry */

		strncpy(s,var->value,sizeof(s));

		if(explode_string1(s, argv, 6, ',', 0) != 6){
			ast_log(LOG_WARNING,"Alarm arguments must be 6 for %s\n", var->name);
			var = var->next;
			continue;
		}

		ignorefirst = atoi(argv[2]);

		if(!(pin = atoi(argv[1]))){
			ast_log(LOG_WARNING,"Pin must be greater than 0 for %s\n",var->name);
			var = var->next;
			continue;
		}

		/* Find the pin entry */
		p = t->pinhead;
		while(p){
			if(p->num == pin)
				break;
			p = p->next;
		}
		if(!p){
			ast_log(LOG_WARNING,"Can't find pin %d for device %s\n", pin, argv[0]);
			var = var->next;
			continue;
		}

		if(!strcmp(argv[0], t->name)){
			strncpy(p->alarmargs, var->value, 64); /* Save the alarm arguments in the pin entry */
			p->alarmargs[63] = 0;
			ast_log(LOG_NOTICE,"Adding alarm %s on pin %d\n", var->name, pin);
			uchameleon_do_long(t, pin, DAQ_CMD_MONITOR, uchameleon_alarm_handler, &ignorefirst, NULL);
		}
		var = var->next;
	}

	ast_config_destroy(ourcfg);
	time(&t->adcacqtime); /* Start ADC Acquisition */
	return -0;
}


/*
 * Open the serial channel and test for the uchameleon device at the end of the link
 */

int uchameleon_open(struct daq_entry_tag *t)
{
	int res;


	if(!t)
		return -1;

	if(uchameleon_connect(t)){
		ast_log(LOG_WARNING,"Cannot open device %s", t->name);
		return -1;
	}

	res = uchameleon_thread_start(t);

	if(!res)
		res = uchameleon_pin_init(t);

	return res;

}

/*
 * Close uchameleon
 */

int uchameleon_close(struct daq_entry_tag *t)
{
	int res = 0;
	char *ledpat="led pattern 253\n";
	struct daq_pin_entry_tag *p,*pn;
	struct daq_tx_entry_tag *q,*qn;

	if(!t)
		return -1;

	ast_mutex_lock(&t->lock);

	if(t->active){
		res = pthread_kill(t->threadid, 0);
		if(res)
		ast_log(LOG_WARNING, "Can't kill monitor thread");
		ast_mutex_unlock(&t->lock);
		return -1;
	}

	if(t->fd > 0)
		serial_io(t->fd, ledpat, NULL, strlen(ledpat) ,0, 0, 0); /* LED back to flashing */

	/* Free linked lists */

	if(t->pinhead){
		p = t->pinhead;
		while(p){
			pn = p->next;
			ast_free(p);
			p = pn;
		}
		t->pinhead = NULL;
	}


	if(t->txhead){
		q = t->txhead;
		while(q){
			qn = q->next;
			ast_free(q);
			q = qn;
		}
		t->txhead = t->txtail = NULL;
	}

	if(t->fd > 0){
		res = close(t->fd);
		if(res)
			ast_log(LOG_WARNING, "Error closing serial port");
		t->fd = -1;
	}
	ast_mutex_unlock(&t->lock);
	ast_mutex_destroy(&t->lock);
	return res;
}

/*
 * Uchameleon generic interface which supports monitor thread
 */

int uchameleon_do_long( struct daq_entry_tag *t, int pin,
int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2)
{
	int i,j,x;
	struct daq_pin_entry_tag *p, *listl, *listp;

	if(!t)
		return -1;

	ast_mutex_lock(&t->lock);

	if(!t->active){
		/* Try to restart thread and re-open device */
		ast_mutex_unlock(&t->lock);
		uchameleon_close(t);
		usleep(10*1000);
		if(uchameleon_open(t)){
			ast_log(LOG_WARNING,"Could not re-open Uchameleon\n");
			return -1;
		}
		ast_mutex_lock(&t->lock);
		/* We're back in business! */
	}


	/* Find our pin */

	listp = listl = t->pinhead;
	while(listp){
		listl = listp;
		if(listp->num == pin)
			break;
		listp = listp->next;
	}
	if(listp){
		if(cmd == DAQ_CMD_PINSET){
			if(arg1 && *arg1 && (*arg1 < 19)){
				while(listp->state){
					ast_mutex_unlock(&t->lock);
					usleep(10*1000); /* Wait */
					ast_mutex_lock(&t->lock);
				}
				listp->command = DAQ_CMD_PINSET;
				listp->pintype = *arg1; /* Pin redefinition */
				listp->valuemin = 255;
				listp->valuemax = 0;
				listp->state = DAQ_PS_START;
			}
			else{
				ast_log(LOG_WARNING,"Invalid pin number for pinset\n");
			}
		}
		else{
			/* Return ADC value */

			if(cmd == DAQ_CMD_ADC){
				if(arg2){
					switch(*((int *) arg2)){
						case DAQ_SUB_CUR:
							if(arg1)
								*arg1 = listp->value;
							break;

						case DAQ_SUB_STAVG: /* Short term average */
							x = 0;
							i = listp->adcnextupdate;
							for(j = 0 ; j < ADC_HISTORY_DEPTH; j++){
								if(debug >= 4){
									ast_log(LOG_NOTICE, "Sample for avg: %d\n",
									listp->adchistory[i]);
								}
								x += listp->adchistory[i];
								if(++i >= ADC_HISTORY_DEPTH)
									i = 0;
							}
							x /= ADC_HISTORY_DEPTH;
							if(debug >= 3)
								ast_log(LOG_NOTICE, "Average: %d\n", x);
							if(arg1)
								*arg1 = x;
							break;

						case DAQ_SUB_STMAX: /* Short term maximum */
							x = 0;
							i = listp->adcnextupdate;
							for(j = 0 ; j < ADC_HISTORY_DEPTH; j++){
								if(debug >= 4){
									ast_log(LOG_NOTICE, "Sample for max: %d\n",
									listp->adchistory[i]);
								}
								if(listp->adchistory[i] > x)
									x = listp->adchistory[i];
								if(++i >= ADC_HISTORY_DEPTH)
									i = 0;
							}
							if(debug >= 3)
								ast_log(LOG_NOTICE, "Maximum: %d\n", x);
							if(arg1)
								*arg1 = x;
							break;

						case DAQ_SUB_STMIN: /* Short term minimum */
							x = 255 ;
							i = listp->adcnextupdate;
							if(i >= ADC_HISTORY_DEPTH)
								i = 0;
							for(j = 0 ; j < ADC_HISTORY_DEPTH; j++){
								if(debug >= 4){
									ast_log(LOG_NOTICE, "Sample for min: %d\n",
									listp->adchistory[i]);
								}
								if(listp->adchistory[i] < x)
									x = listp->adchistory[i];
								if(++i >= ADC_HISTORY_DEPTH)
									i = 0;
								}
							if(debug >= 3)
								ast_log(LOG_NOTICE, "Minimum: %d\n", x);
							if(arg1)
								*arg1 = x;
							break;

						case DAQ_SUB_MAX: /* Max since start or reset */
							if(arg1)
								*arg1 = listp->valuemax;
							break;

						case DAQ_SUB_MIN: /* Min since start or reset */
							if(arg1)
								*arg1 = listp->valuemin;
							break;

						default:
							ast_mutex_unlock(&t->lock);
							return -1;
					}
				}
				else{
					if(arg1)
						*arg1 = listp->value;
				}
				ast_mutex_unlock(&t->lock);
				return 0;
			}

			/* Don't deadlock if monitor has been previously issued for a pin */

			if(listp->state == DAQ_PS_IN_MONITOR){
				if((cmd != DAQ_CMD_MONITOR) || (exec)){
					ast_log(LOG_WARNING,
						"Monitor was previously set on pin %d, command ignored\n",listp->num);
					ast_mutex_unlock(&t->lock);
					return -1;
				}
			}

			/* Rest of commands are processed here */

			while(listp->state){
				ast_mutex_unlock(&t->lock);
				usleep(10*1000); /* Wait */
				ast_mutex_lock(&t->lock);
			}

			if(cmd == DAQ_CMD_MONITOR){
				if(arg1)
					listp->ignorefirstalarm = *arg1;
				listp->monexec = exec;
			}

			listp->command = cmd;

			if(cmd == DAQ_CMD_OUT){
				if(arg1){
					listp->value = *arg1;
				}
				else{
					ast_mutex_unlock(&t->lock);
					return 0;
				}
			}
			listp->state = DAQ_PS_START;
			if((cmd == DAQ_CMD_OUT)||(cmd == DAQ_CMD_MONITOR)){
				ast_mutex_unlock(&t->lock);
				return 0;
			}

 			while(listp->state){
				ast_mutex_unlock(&t->lock);
				usleep(10*1000); /* Wait */
				ast_mutex_lock(&t->lock);
			}
			*arg1 = listp->value;
			ast_mutex_unlock(&t->lock);
			return 0;
		}
	}
	else{ /* Pin not in list */
		if(cmd == DAQ_CMD_PINSET){
			if(arg1 && *arg1 && (*arg1 < 19)){
				/* New pin definition */
				if(!(p = (struct daq_pin_entry_tag *) malloc(sizeof(struct daq_pin_entry_tag)))){
					ast_log(LOG_ERROR,"Out of memory");
					ast_mutex_unlock(&t->lock);
					return -1;
				}
				memset(p, 0, sizeof(struct daq_pin_entry_tag));
				p->pintype = *arg1;
				p->command = DAQ_CMD_PINSET;
				p->num = pin;
				if(!listl){
					t->pinhead = p;
				}
				else{
					listl->next = p;
				}
				p->state = DAQ_PS_START;
				ast_mutex_unlock(&t->lock);
				return 0;
			}
			else{
				ast_log(LOG_WARNING,"Invalid pin number for pinset\n");
			}
		}
		else{
			ast_log(LOG_WARNING,"Invalid pin number for pin I/O command\n");
		}
	}
	ast_mutex_unlock(&t->lock);
	return -1;
}

/*
 * Reset a minimum or maximum reading
 */

int uchameleon_reset_minmax(struct daq_entry_tag *t, int pin, int minmax)
{
	struct daq_pin_entry_tag *p;

	/* Find the pin */
	p = t->pinhead;
	while(p){
		if(p->num == pin)
			break;
		p = p->next;
	}
	if(!p)
		return -1;
	ast_mutex_lock(&t->lock);
	if(minmax){
		ast_log(LOG_NOTICE, "Resetting maximum on device %s, pin %d\n",t->name, pin);
		p->valuemax = 0;
	}
	else{
		p->valuemin = 255;
		ast_log(LOG_NOTICE, "Resetting minimum on device %s, pin %d\n",t->name, pin);
	}
	ast_mutex_unlock(&t->lock);
	return 0;
}




/*
 * Queue up a tx command (used exclusively by uchameleon_monitor() )
 */

void uchameleon_queue_tx(struct daq_entry_tag *t, char *txbuff)
{
	struct daq_tx_entry_tag *q;

	if(!t)
		return;

	if(!(q = (struct daq_tx_entry_tag *) ast_malloc(sizeof(struct daq_tx_entry_tag)))){
		ast_log(LOG_WARNING, "Out of memory\n");
		return;
	}

	memset(q, 0, sizeof(struct daq_tx_entry_tag));

	strncpy(q->txbuff, txbuff, 32);
	q->txbuff[31] = 0;

	if(t->txtail){
		t->txtail->next = q;
		q->prev = t->txtail;
		t->txtail = q;
	}
	else
		t->txhead = t->txtail = q;
	return;
}


/*
 * Monitor thread for Uchameleon devices
 *
 * started by uchameleon_open() and shutdown by uchameleon_close()
 *
 */
void *uchameleon_monitor_thread(void *this)
{
	int pin = 0, sample = 0;
	int i,res,valid,adc_acquire;
	time_t now;
	char rxbuff[32];
	char txbuff[32];
	char *rxargs[4];
	struct daq_entry_tag *t = (struct daq_entry_tag *) this;
	struct daq_pin_entry_tag *p;
	struct daq_tx_entry_tag *q;



	if(debug)
		ast_log(LOG_NOTICE, "DAQ: thread started\n");

	ast_mutex_lock(&t->lock);
	t->active = 1;
	ast_mutex_unlock(&t->lock);

	for(;;){
		adc_acquire = 0;
		 /* If receive data */
		res = serial_rx(t->fd, rxbuff, sizeof(rxbuff), DAQ_RX_TIMEOUT, 0x0a);
		if(res == -1){
			ast_log(LOG_ERROR,"serial_rx failed\n");
			close(t->fd);
			ast_mutex_lock(&t->lock);
			t->fd = -1;
			t->active = 0;
			ast_mutex_unlock(&t->lock);
			return this; /* Now, we die */
		}
		if(res){
			if(debug >= 5)
				ast_log(LOG_NOTICE, "Received: %s\n", rxbuff);
			valid = 0;
			/* Parse return string */
			i = explode_string1(rxbuff, rxargs, 3, ' ', 0);
			if(i == 3){
				if(!strcmp(rxargs[0],"pin")){
					valid = 1;
					pin = atoi(rxargs[1]);
					sample = atoi(rxargs[2]);
				}
				if(!strcmp(rxargs[0],"adc")){
					valid = 2;
					pin = atoi(rxargs[1]);
					sample = atoi(rxargs[2]);
				}
			}
			if(valid){
				/* Update the correct pin list entry */
				ast_mutex_lock(&t->lock);
				p = t->pinhead;
				while(p){
					if(p->num == pin){
						if((valid == 1)&&((p->pintype == DAQ_PT_IN)||
							(p->pintype == DAQ_PT_INP)||(p->pintype == DAQ_PT_OUT))){
							p->value = sample ? 1 : 0;
							if(debug >= 3)
								ast_log(LOG_NOTICE,"Input pin %d is a %d\n",
									p->num, p->value);
							/* Exec monitor fun if state is monitor */

							if(p->state == DAQ_PS_IN_MONITOR){
								if(!p->alarmmask && !p->ignorefirstalarm && p->monexec){
									(*p->monexec)(p);
								}
								p->ignorefirstalarm = 0;
							}
							else
								p->state = DAQ_PS_IDLE;
						}
						if((valid == 2)&&(p->pintype == DAQ_PT_INADC)){
							p->value = sample;
							if(sample > p->valuemax)
								p->valuemax = sample;
							if(sample < p->valuemin)
								p->valuemin = sample;
							p->adchistory[p->adcnextupdate++] = sample;
							if(p->adcnextupdate >= ADC_HISTORY_DEPTH)
								p->adcnextupdate = 0;
							p->state = DAQ_PS_IDLE;
						}
						break;
					}
					p = p->next;
				}
				ast_mutex_unlock(&t->lock);
			}
		}


		if(time(&now) >= t->adcacqtime){
			t->adcacqtime = now + DAQ_ADC_ACQINT;
			if(debug >= 4)
				ast_log(LOG_NOTICE,"Acquiring analog data\n");
			adc_acquire = 1;
		}

		/* Go through the pin linked list looking for new work */
		ast_mutex_lock(&t->lock);
		p = t->pinhead;
		while(p){
			/* Time to acquire all ADC channels ? */
			if((adc_acquire) && (p->pintype == DAQ_PT_INADC)){
				p->state = DAQ_PS_START;
				p->command = DAQ_CMD_ADC;
			}
			if(p->state == DAQ_PS_START){
				p->state = DAQ_PS_BUSY; /* Assume we are busy */
				switch(p->command){
					case DAQ_CMD_OUT:
						if(p->pintype == DAQ_PT_OUT){
							snprintf(txbuff,sizeof(txbuff),"pin %d %s\n", p->num, (p->value) ?
							"hi" : "lo");
							if(debug >= 3)
								ast_log(LOG_NOTICE, "DAQ_CMD_OUT: %s\n", txbuff);
							uchameleon_queue_tx(t, txbuff);
							p->state = DAQ_PS_IDLE; /* TX is considered done */
						}
						else{
							ast_log(LOG_WARNING,"Wrong pin type for out command\n");
							p->state = DAQ_PS_IDLE;
						}
						break;

					case DAQ_CMD_MONITOR:
						snprintf(txbuff, sizeof(txbuff), "pin %d monitor %s\n",
						p->num, p->monexec ? "on" : "off");
						uchameleon_queue_tx(t, txbuff);
						if(!p->monexec)
							p->state = DAQ_PS_IDLE; /* Restore to idle channel */
						else{
							p->state = DAQ_PS_IN_MONITOR;
						}
						break;

					case DAQ_CMD_IN:
						if((p->pintype == DAQ_PT_IN)||
							(p->pintype == DAQ_PT_INP)||(p->pintype == DAQ_PT_OUT)){
							snprintf(txbuff,sizeof(txbuff),"pin %d state\n", p->num);
							uchameleon_queue_tx(t, txbuff);
						}
						else{
							ast_log(LOG_WARNING,"Wrong pin type for in or inp command\n");
							p->state = DAQ_PS_IDLE;
						}
						break;

					case DAQ_CMD_ADC:
						if(p->pintype == DAQ_PT_INADC){
							snprintf(txbuff,sizeof(txbuff),"adc %d\n", p->num);
							uchameleon_queue_tx(t, txbuff);
						}
						else{
							ast_log(LOG_WARNING,"Wrong pin type for adc command\n");
							p->state = DAQ_PS_IDLE;
						}
						break;

					case DAQ_CMD_PINSET:
						if((!p->num)||(p->num > 18)){
							ast_log(LOG_WARNING,"Invalid pin number %d\n", p->num);
							p->state = DAQ_PS_IDLE;
						}
						switch(p->pintype){
							case DAQ_PT_IN:
							case DAQ_PT_INADC:
							case DAQ_PT_INP:
								if((p->pintype == DAQ_PT_INADC) && (p->num > 8)){
									ast_log(LOG_WARNING,
									"Invalid ADC pin number %d\n", p->num);
									p->state = DAQ_PS_IDLE;
									break;
								}
								if((p->pintype == DAQ_PT_INP) && (p->num < 9)){
									ast_log(LOG_WARNING,
									"Invalid INP pin number %d\n", p->num);
									p->state = DAQ_PS_IDLE;
									break;
								}
								snprintf(txbuff, sizeof(txbuff), "pin %d in\n", p->num);
								uchameleon_queue_tx(t, txbuff);
								if(p->num > 8){
									snprintf(txbuff, sizeof(txbuff),
									"pin %d pullup %d\n", p->num,
									(p->pintype == DAQ_PT_INP) ? 1 : 0);
									uchameleon_queue_tx(t, txbuff);
								}
								p->valuemin = 255;
								p->valuemax = 0;
								p->state = DAQ_PS_IDLE;
								break;

							case DAQ_PT_OUT:
                        					snprintf(txbuff, sizeof(txbuff), "pin %d out\n", p->num);
								uchameleon_queue_tx(t, txbuff);
								p->state = DAQ_PS_IDLE;
								break;

							default:
								break;
						}
						break;

					default:
						ast_log(LOG_WARNING,"Unrecognized uchameleon command\n");
						p->state = DAQ_PS_IDLE;
						break;
				} /* switch */
			} /* if */
		p = p->next;
		} /* while */

		/* Transmit queued commands */
		while(t->txhead){
			q = t->txhead;
			strncpy(txbuff,q->txbuff,sizeof(txbuff));
			txbuff[sizeof(txbuff)-1] = 0;
			t->txhead = q->next;
			if(t->txhead)
				t->txhead->prev = NULL;
			else
				t->txtail = NULL;
			ast_free(q);
			ast_mutex_unlock(&t->lock);
			if(serial_txstring(t->fd, txbuff) == -1){
				close(t->fd);
				ast_mutex_lock(&t->lock);
				t->active= 0;
				t->fd = -1;
				ast_mutex_unlock(&t->lock);
				ast_log(LOG_ERROR,"Tx failed, terminating monitor thread\n");
				return this; /* Now, we die */
			}

			ast_mutex_lock(&t->lock);
		}/* while */
		ast_mutex_unlock(&t->lock);
	} /* for(;;) */
	return this;
}


/*
 * Parse a request METER request for telemetry thread
 * This is passed in a comma separated list of items from the function table entry
 * There should be 3 or 4 fields in the function table entry: device, channel, meter face, and  optionally: filter
 */


int handle_meter_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args)
{
	int i,res,files,filter,val;
	int pin = 0;
	int pintype = 0;
	int device = 0;
	int metertype = 0;
	int numranges = 0;
	int filtertype = 0;
	int rangemin,rangemax;
	float scaledval = 0.0, scalepre = 0.0, scalepost = 0.0, scalediv = 1.0, valtoround;
	char *myargs,*meter_face;
	const char *p;
	char *start, *end;
	char *sounds = NULL;
	char *rangephrase = NULL;
	char *argv[5];
	char *sound_files[MAX_METER_FILES+1];
	char *range_strings[MAX_DAQ_RANGES+1];
	char *bitphrases[3];
	char *filter_keywords[]={"none","max","min","stmin","stmax","stavg",NULL};
	struct daq_entry_tag *entry;

	if(!(myargs = ast_strdup(args))){ /* Make a local copy to slice and dice */
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}

	i = explode_string1(myargs, argv, 4, ',', 0);
	if((i != 4) && (i != 3)){ /* Must have 3 or 4 substrings, no more, no less */
		ast_log(LOG_WARNING,"Wrong number of arguments for meter telemetry function is: %d s/b 3 or 4", i);
		ast_free(myargs);
		return -1;
	}
	if(debug >= 3){
		ast_log(LOG_NOTICE,"Device: %s, Pin: %s, Meter Face: %s Filter: %s\n",
		argv[0],argv[1],argv[2], argv[3]);
	}

	if(i == 4){
		filter = matchkeyword1(argv[3], NULL, filter_keywords);
		if(!filter){
			ast_log(LOG_WARNING,"Unsupported filter type: %s\n",argv[3]);
			ast_free(myargs);
			return -1;
		}
		filter--;
	}
	else
		filter = DAQ_SUB_CUR;

	/* Find our device */
	if(!(entry = daq_devtoentry(argv[0]))){
		ast_log(LOG_WARNING,"Cannot find device %s in daq-list\n",argv[0]);
		ast_free(myargs);
		return -1;
	}

	/* Check for compatible pin type */
	if(!(p = ast_variable_retrieve(myrpt->cfg,argv[0],argv[1]))){
		ast_log(LOG_WARNING,"Channel %s not defined for %s\n", argv[1], argv[0]);
		ast_free(myargs);
		return -1;
	}

	if(!strcmp("inadc",p))
		pintype = 1;
	if((!strcmp("inp",p))||(!strcmp("in",p)||(!strcmp("out", p))))
		pintype = 2;
	if(!pintype){
		ast_log(LOG_WARNING,"Pin type must be one of inadc, inp, in, or out for channel %s\n",argv[1]);
		ast_free(myargs);
		return -1;
	}
	if(debug >= 3)
		ast_log(LOG_NOTICE,"Pintype = %d\n",pintype);

	pin = atoi(argv[1]);

	/*
 	Look up and parse the meter face

	[meter-faces]
	batvolts=scale(0,12.8,0),thevoltage,is,volts
	winddir=range(0-33:north,34-96:west,97-160:south,161-224:east,225-255:north),thewindis,?
	door=bit(closed,open),thedooris,?

	*/

	if(!(p = ast_variable_retrieve(myrpt->cfg,"meter-faces", argv[2]))){
		ast_log(LOG_WARNING,"Meter face %s not found", argv[2]);
		ast_free(myargs);
		return -1;
	}

	if(!(meter_face = ast_strdup(p))){
		ast_log(LOG_WARNING,"Out of memory");
		ast_free(myargs);
		return -1;
	}

	if(!strncmp("scale", meter_face, 5)){ /* scale function? */
		metertype = 1;
		if((!(end = strchr(meter_face,')')))||
			(!(start = strchr(meter_face, '(')))||
			(!end[1])||(!end[2])||(end[1] != ',')){ /* Properly formed? */
			ast_log(LOG_WARNING,"Syntax error in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		*start++ = 0; /* Points to comma delimited scaling values */
		*end = 0;
		sounds = end + 2; /* Start of sounds part */
		if(sscanf(start,"%f,%f,%f",&scalepre, &scalediv, &scalepost) != 3){
			ast_log(LOG_WARNING,"Scale must have 3 args in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		if(scalediv < 1.0){
			ast_log(LOG_WARNING,"scalediv must be >= 1\n");
			ast_free(myargs);
			ast_free(meter_face);
			return -1;

		}
	}
	else if(!strncmp("range", meter_face, 5)){ /* range function */
		metertype = 2;
		if((!(end = strchr(meter_face,')')))||
			(!(start = strchr(meter_face, '(')))||
			(!end[1])||(!end[2])||(end[1] != ',')){ /* Properly formed? */
			ast_log(LOG_WARNING,"Syntax error in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		*start++ = 0;
		*end = 0;
		sounds = end + 2;
		/*
 		* Parse range entries
 		*/
		if((numranges = explode_string1(start, range_strings, MAX_DAQ_RANGES, ',', 0)) < 2 ){
			ast_log(LOG_WARNING, "At least 2 ranges required for range() in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}

	}
	else if(!strncmp("bit", meter_face, 3)){ /* bit function */
		metertype = 3;
		if((!(end = strchr(meter_face,')')))||
			(!(start = strchr(meter_face, '(')))||
			(!end[1])||(!end[2])||(end[1] != ',')){ /* Properly formed? */
			ast_log(LOG_WARNING,"Syntax error in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
		*start++ = 0;
		*end = 0;
		sounds = end + 2;
		if(2 != explode_string1(start, bitphrases, 2, ',', 0)){
			ast_log(LOG_WARNING, "2 phrases required for bit() in meter face %s\n", argv[2]);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
	}
	else{
		ast_log(LOG_WARNING,"Meter face %s needs to specify one of scale, range or bit\n", argv[2]);
		ast_free(myargs);
		ast_free(meter_face);
		return -1;
	}

	/*
 	* Acquire
 	*/

	val = 0;
	if(pintype == 1){
		res = daq_do_long(entry, pin, DAQ_CMD_ADC, NULL, &val, &filter);
		if(!res)
			scaledval = ((val + scalepre)/scalediv) + scalepost;
	}
	else{
		res = daq_do_long(entry, pin, DAQ_CMD_IN, NULL, &val, NULL);
	}

	if(res){ /* DAQ Subsystem is down */
		ast_free(myargs);
		ast_free(meter_face);
		return res;
	}

	/*
 	* Select Range
 	*/

	if(metertype == 2){
		for(i = 0; i < numranges; i++){
			if(2 != sscanf(range_strings[i],"%u-%u:", &rangemin, &rangemax)){
				ast_log(LOG_WARNING,"Range variable error on meter face %s\n", argv[2]);
				ast_free(myargs);
				ast_free(meter_face);
				return -1;
			}
			if((!(rangephrase = strchr(range_strings[i],':')) || (!rangephrase[1]))){
				ast_log(LOG_WARNING,"Range phrase missing on meter face %s\n", argv[2]);
				ast_free(myargs);
				ast_free(meter_face);
				return -1;
			}
			rangephrase++;
			if((val >= rangemin) && (val <= rangemax))
				break;
		}
		if(i == numranges){
			ast_log(LOG_WARNING,"Range missing on meter face %s for value %d\n", argv[2], val);
			ast_free(myargs);
			ast_free(meter_face);
			return -1;
		}
	}

	if(debug >= 3){ /* Spew the variables */
		ast_log(LOG_NOTICE,"device = %d, pin = %d, pintype = %d, metertype = %d\n",device, pin, pintype, metertype);
		ast_log(LOG_NOTICE,"raw value = %d\n", val);
		if(metertype == 1){
			ast_log(LOG_NOTICE,"scalepre = %f, scalediv = %f, scalepost = %f\n",scalepre, scalediv, scalepost);
			ast_log(LOG_NOTICE,"scaled value = %f\n", scaledval);
		}
		if(metertype == 2){
			ast_log(LOG_NOTICE,"Range phrase is: %s for meter face %s\n", rangephrase, argv[2]);
		ast_log(LOG_NOTICE,"filtertype = %d\n", filtertype);
		}
		ast_log(LOG_NOTICE,"sounds = %s\n", sounds);

 	}

	/* Wait the normal telemetry delay time */

	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) goto done;


	/* Split up the sounds string */

	files = explode_string1(sounds, sound_files, MAX_METER_FILES, ',', 0);
	if(files == 0){
		ast_log(LOG_WARNING,"No sound files to say for meter %s\n",argv[2]);
		ast_free(myargs);
		ast_free(meter_face);
		return -1;
	}
	/* Say the files one by one acting specially on the ? character */
	res = 0;
	for(i = 0; i < files && !res; i++){
		if(sound_files[i][0] == '?'){ /* Insert sample */
			if(metertype == 1){
				int integer, decimal, precision = 0;
				if((scalediv >= 10) && (scalediv < 100)) /* Adjust precision of decimal places */
					precision = 10;
				else if(scalediv >= 100)
					precision = 100;
				integer = (int) scaledval;
				valtoround = ((scaledval - integer) * precision);
				 /* grrr.. inline lroundf doesn't work with uClibc! */
				decimal = (int) ((valtoround + ((valtoround >= 0) ? 0.5 : -0.5)));
				if((precision) && (decimal == precision)){
					decimal = 0;
					integer++;
				}
				if(debug)
					ast_log(LOG_NOTICE,"integer = %d, decimal = %d\n", integer, decimal);
				res = saynum1(mychannel, integer);
				if(!res && precision && decimal){
					res = sayfile(mychannel,"point");
					if(!res)
						res = saynum1(mychannel, decimal);
				}
			}
			if(metertype == 2){
				res = sayfile(mychannel, rangephrase);
			}
			if(metertype == 3){
				res = sayfile(mychannel, bitphrases[(val) ? 1: 0]);
			}

		}
		else{
			res = sayfile(mychannel, sound_files[i]); /* Say the next word in the list */
		}
	}
done:
	/* Done */
	ast_free(myargs);
	ast_free(meter_face);
	return 0;
}

/*
 * Handle USEROUT telemetry
 */

int handle_userout_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args)
{
	int argc, i, pin, reqstate, res;
	char *myargs;
	char *argv[11];
	struct daq_entry_tag *t;

	if(!(myargs = ast_strdup(args))){ /* Make a local copy to slice and dice */
		ast_log(LOG_WARNING, "Out of memory\n");
		return -1;
	}

	if(debug >= 3)
		ast_log(LOG_NOTICE, "String: %s\n", myargs);

	argc = explode_string1(myargs, argv, 10, ',', 0);
	if(argc < 4){ /* Must have at least 4 arguments */
		ast_log(LOG_WARNING,"Incorrect number of arguments for USEROUT function");
		ast_free(myargs);
		return -1;
	}
	if(debug >= 3){
		ast_log(LOG_NOTICE,"USEROUT Device: %s, Pin: %s, Requested state: %s\n",
		argv[0],argv[1],argv[2]);
	}
	pin = atoi(argv[1]);
	reqstate = atoi(argv[2]);

	/* Find our device */
	if(!(t = daq_devtoentry(argv[0]))){
		ast_log(LOG_WARNING,"Cannot find device %s in daq-list\n",argv[0]);
		ast_free(myargs);
		return -1;
	}

	if(debug >= 3){
		ast_log(LOG_NOTICE, "Output to pin %d a value of %d with argc = %d\n", pin, reqstate, argc);
	}

	/* Set or reset the bit */

	res = daq_do( t, pin, DAQ_CMD_OUT, reqstate);

	/* Wait the normal telemetry delay time */

	if(!res)
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) goto done;

	/* Say the files one by one at argc index 3 */
	for(i = 3; i < argc && !res; i++){
		res = sayfile(mychannel, argv[i]); /* Say the next word in the list */
	}

done:
	ast_free(myargs);
	return 0;
}


/*
*  Playback a meter reading
*/

int function_meter(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	if (myrpt->remote)
		return DC_ERROR;

	if(debug)
		ast_log(LOG_NOTICE, "meter param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);

	rpt_telem_select(myrpt,command_source,mylink);
	rpt_telemetry(myrpt,METER,param);
	return DC_COMPLETE;
}



/*
*  Set or reset a USER Output bit
*/

int function_userout(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{

	if (myrpt->remote)
		return DC_ERROR;

		ast_log(LOG_NOTICE, "userout param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);

	rpt_telem_select(myrpt,command_source,mylink);
	rpt_telemetry(myrpt,USEROUT,param);
	return DC_COMPLETE;
}


/*
*  Execute shell command
*/

int function_cmd(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	char *cp;

	if (myrpt->remote)
		return DC_ERROR;

	ast_log(LOG_NOTICE, "cmd param = %s, digitbuf = %s\n", (param)? param : "(null)", digitbuf);

	if (param) {
		if (*param == '#') /* to execute asterisk cli command */
		{
			ast_cli_command(nullfd,param + 1);
		}
		else
		{
			cp = ast_malloc(strlen(param) + 10);
			if (!cp)
			{
				ast_log(LOG_NOTICE,"Unable to alloc");
				return DC_ERROR;
			}
			memset(cp,0,strlen(param) + 10);
			sprintf(cp,"%s &",param);
			ast_safe_system(cp);
			free(cp);
		}
	}
	return DC_COMPLETE;
}


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


static int explode_string1(char *str, char *strp[], int limit, char delim, char quote)
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


/*
* Match a keyword in a list, and return index of string plus 1 if there was a match,* else return 0.
* If param is passed in non-null, then it will be set to the first character past the match
*/

static int matchkeyword1(char *string, char **param, char *keywords[])
{
int	i,ls;
	for( i = 0 ; keywords[i] ; i++){
		ls = strlen(keywords[i]);
		if(!ls){
			if(param)
				*param = NULL;
			return 0;
		}
		if(!strncmp(string, keywords[i], ls)){
			if(param)
				*param = string + ls;
			return i + 1;
		}
	}
	if(param)
		*param = NULL;
	return 0;
}



//# Say a file - streams file to output channel

static int sayfile(struct ast_channel *mychannel,char *fname)
{
int	res;

	res = ast_streamfile(mychannel, fname, mychannel->language);
	if (!res)
		res = ast_waitstream(mychannel, "");
	else
		 ast_log(LOG_WARNING, "ast_streamfile %s failed on %s\n", fname, mychannel->name);
	ast_stopstream(mychannel);
	return res;
}

/*
 **********************
* End of DAQ functions*
* *********************
*/

