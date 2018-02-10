
#ifndef RADIO_H
#define RADIO_H


/*
 * Structure that holds information regarding app_rpt operation
*/
struct rpt;
struct rpt_link;


char *remote_rig_ft950="ft950";
char *remote_rig_ft897="ft897";
char *remote_rig_ft100="ft100";
char *remote_rig_rbi="rbi";
char *remote_rig_kenwood="kenwood";
char *remote_rig_tm271="tm271";
char *remote_rig_tmd700="tmd700";
char *remote_rig_ic706="ic706";
char *remote_rig_xcat="xcat";
char *remote_rig_rtx150="rtx150";
char *remote_rig_rtx450="rtx450";
char *remote_rig_ppp16="ppp16";	  		// parallel port programmable 16 channels

//static void rpt_telemetry(struct rpt *myrpt,int mode, void *data);


//int set_ft897(struct rpt *myrpt);
//int set_ft100(struct rpt *myrpt);
//int set_ft950(struct rpt *myrpt);
//int set_ic706(struct rpt *myrpt);
//int setkenwood(struct rpt *myrpt);
//int set_tm271(struct rpt *myrpt);
//int set_tmd700(struct rpt *myrpt);
int setrbi_check(struct rpt *myrpt);
int setxpmr(struct rpt *myrpt, int dotx);

int sendrxkenwood(struct rpt *myrpt, char *txstr, char *rxstr,char *cmpstr);

int setkenwood(struct rpt *myrpt);

int set_tmd700(struct rpt *myrpt);
int set_tm271(struct rpt *myrpt);

int sendkenwood(struct rpt *myrpt,char *txstr, char *rxstr);

/* take a PL frequency and turn it into a code */
int tm271_pltocode(char *str);


/* take a PL frequency and turn it into a code */
int ft950_pltocode(char *str);

/* take a PL frequency and turn it into a code */
int ft100_pltocode(char *str);

int check_freq_kenwood(int m, int d, int *defmode);


int check_freq_tm271(int m, int d, int *defmode);



/* Check for valid rbi frequency */
/* Hard coded limits now, configurable later, maybe? */

int check_freq_rbi(int m, int d, int *defmode);


/* Check for valid rtx frequency */
/* Hard coded limits now, configurable later, maybe? */

int check_freq_rtx(int m, int d, int *defmode, struct rpt *myrpt);


/*
 * Convert decimals of frequency to int
 */

int decimals2int(char *fraction);


/*
* Split frequency into mhz and decimals
*/

int split_freq(char *mhz, char *decimals, char *freq);



/*
* Split ctcss frequency into hertz and decimal
*/

int split_ctcss_freq(char *hertz, char *decimal, char *freq);





/*
* FT-897 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


int check_freq_ft897(int m, int d, int *defmode);




/*
* Set a new frequency for the FT897
*/

int set_freq_ft897(struct rpt *myrpt, char *newfreq);


/* ft-897 simple commands */

int simple_command_ft897(struct rpt *myrpt, char command);


/* ft-897 offset */

int set_offset_ft897(struct rpt *myrpt, char offset);


/* ft-897 mode */

int set_mode_ft897(struct rpt *myrpt, char newmode);

/* Set tone encode and decode modes */

int set_ctcss_mode_ft897(struct rpt *myrpt, char txplon, char rxplon);


/* Set transmit and receive ctcss tone frequencies */

int set_ctcss_freq_ft897(struct rpt *myrpt, char *txtone, char *rxtone);


int set_ft897(struct rpt *myrpt);


int closerem_ft897(struct rpt *myrpt);


/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

int multimode_bump_freq_ft897(struct rpt *myrpt, int interval);


/*
* FT-100 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


int check_freq_ft100(int m, int d, int *defmode);


/*
* Set a new frequency for the ft100
*/

int set_freq_ft100(struct rpt *myrpt, char *newfreq);


/* ft-897 simple commands */

int simple_command_ft100(struct rpt *myrpt, unsigned char command, unsigned char p1);



/* ft-897 offset */

int set_offset_ft100(struct rpt *myrpt, char offset);



/* ft-897 mode */

int set_mode_ft100(struct rpt *myrpt, char newmode);




/* Set tone encode and decode modes */

int set_ctcss_mode_ft100(struct rpt *myrpt, char txplon, char rxplon);





/* Set transmit and receive ctcss tone frequencies */

int set_ctcss_freq_ft100(struct rpt *myrpt, char *txtone, char *rxtone);



int set_ft100(struct rpt *myrpt);


int closerem_ft100(struct rpt *myrpt);




/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

int multimode_bump_freq_ft100(struct rpt *myrpt, int interval);

/*
* FT-950 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


int check_freq_ft950(int m, int d, int *defmode);

/*
* Set a new frequency for the ft950
*/

int set_freq_ft950(struct rpt *myrpt, char *newfreq);



/* ft-950 offset */

int set_offset_ft950(struct rpt *myrpt, char offset);

/* ft-950 mode */

int set_mode_ft950(struct rpt *myrpt, char newmode);


/* Set tone encode and decode modes */

int set_ctcss_mode_ft950(struct rpt *myrpt, char txplon, char rxplon);
int set_ctcss_freq_ft950(struct rpt *myrpt, char *txtone, char *rxtone);


int set_ft950(struct rpt *myrpt);

/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

int multimode_bump_freq_ft950(struct rpt *myrpt, int interval);

/*
* IC-706 I/O handlers
*/

/* Check to see that the frequency is valid */
/* returns 0 if frequency is valid          */

int check_freq_ic706(int m, int d, int *defmode, char mars);


		/* take a PL frequency and turn it into a code */
		int ic706_pltocode(char *str);



/* ic-706 simple commands */

int simple_command_ic706(struct rpt *myrpt, char command, char subcommand);


/*
* Set a new frequency for the ic706
*/

int set_freq_ic706(struct rpt *myrpt, char *newfreq);

/* ic-706 offset */

int set_offset_ic706(struct rpt *myrpt, char offset);


/* ic-706 mode */

int set_mode_ic706(struct rpt *myrpt, char newmode);


/* Set tone encode and decode modes */

int set_ctcss_mode_ic706(struct rpt *myrpt, char txplon, char rxplon);



/* Set transmit and receive ctcss tone frequencies */

int set_ctcss_freq_ic706(struct rpt *myrpt, char *txtone, char *rxtone);

int vfo_ic706(struct rpt *myrpt);

int mem2vfo_ic706(struct rpt *myrpt);

int select_mem_ic706(struct rpt *myrpt, int slot);

int set_ic706(struct rpt *myrpt);

/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

int multimode_bump_freq_ic706(struct rpt *myrpt, int interval);

/*
* XCAT I/O handlers
*/

/* Check to see that the frequency is valid */
/* returns 0 if frequency is valid          */


int check_freq_xcat(int m, int d, int *defmode);

int simple_command_xcat(struct rpt *myrpt, char command, char subcommand);
/*
* Set a new frequency for the xcat
*/

int set_freq_xcat(struct rpt *myrpt, char *newfreq);

int set_offset_xcat(struct rpt *myrpt, char offset);
/* Set transmit and receive ctcss tone frequencies */

int set_ctcss_freq_xcat(struct rpt *myrpt, char *txtone, char *rxtone);

int set_xcat(struct rpt *myrpt);

/*
* Dispatch to correct I/O handler
*/
int setrem(struct rpt *myrpt);

int closerem(struct rpt *myrpt);

/*
* Dispatch to correct RX frequency checker
*/

int check_freq(struct rpt *myrpt, int m, int d, int *defmode);

/*
 * Check TX frequency before transmitting
   rv=1 if tx frequency in ok.
*/

char check_tx_freq(struct rpt *myrpt);

/*
* Dispatch to correct frequency bumping function
*/

int multimode_bump_freq(struct rpt *myrpt, int interval);


/*
* Queue announcment that scan has been stopped
*/

void stop_scan(struct rpt *myrpt);


/*
* This is called periodically when in scan mode
*/


int service_scan(struct rpt *myrpt);
/*
	retrieve memory setting and set radio
*/
int get_mem_set(struct rpt *myrpt, char *digitbuf);

/*
	steer the radio selected channel to either one programmed into the radio
	or if the radio is VFO agile, to an rpt.conf memory location.
*/
int channel_steer(struct rpt *myrpt, char *data);

/*
*/
int channel_revert(struct rpt *myrpt);

/*
* Remote base function
*/

int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);


#endif /* RADIO_H  */


























































