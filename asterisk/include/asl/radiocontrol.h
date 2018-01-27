
static int sendrxkenwood(struct rpt *myrpt, char *txstr, char *rxstr,char *cmpstr);

static int setkenwood(struct rpt *myrpt);

static int set_tmd700(struct rpt *myrpt);
static int set_tm271(struct rpt *myrpt);

static int sendkenwood(struct rpt *myrpt,char *txstr, char *rxstr);

/* take a PL frequency and turn it into a code */
static int tm271_pltocode(char *str);


/* take a PL frequency and turn it into a code */
static int ft950_pltocode(char *str);

/* take a PL frequency and turn it into a code */
static int ft100_pltocode(char *str);

static int check_freq_kenwood(int m, int d, int *defmode);


static int check_freq_tm271(int m, int d, int *defmode);



/* Check for valid rbi frequency */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_rbi(int m, int d, int *defmode);


/* Check for valid rtx frequency */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_rtx(int m, int d, int *defmode, struct rpt *myrpt);


/*
 * Convert decimals of frequency to int
 */

static int decimals2int(char *fraction);


/*
* Split frequency into mhz and decimals
*/

static int split_freq(char *mhz, char *decimals, char *freq);



/*
* Split ctcss frequency into hertz and decimal
*/

static int split_ctcss_freq(char *hertz, char *decimal, char *freq);





/*
* FT-897 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


static int check_freq_ft897(int m, int d, int *defmode);




/*
* Set a new frequency for the FT897
*/

static int set_freq_ft897(struct rpt *myrpt, char *newfreq);


/* ft-897 simple commands */

static int simple_command_ft897(struct rpt *myrpt, char command);


/* ft-897 offset */

static int set_offset_ft897(struct rpt *myrpt, char offset);


/* ft-897 mode */

static int set_mode_ft897(struct rpt *myrpt, char newmode);

/* Set tone encode and decode modes */

static int set_ctcss_mode_ft897(struct rpt *myrpt, char txplon, char rxplon);


/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft897(struct rpt *myrpt, char *txtone, char *rxtone);


static int set_ft897(struct rpt *myrpt);


static int closerem_ft897(struct rpt *myrpt);


/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

static int multimode_bump_freq_ft897(struct rpt *myrpt, int interval);


/*
* FT-100 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


static int check_freq_ft100(int m, int d, int *defmode);


/*
* Set a new frequency for the ft100
*/

static int set_freq_ft100(struct rpt *myrpt, char *newfreq);


/* ft-897 simple commands */

static int simple_command_ft100(struct rpt *myrpt, unsigned char command, unsigned char p1);



/* ft-897 offset */

static int set_offset_ft100(struct rpt *myrpt, char offset);



/* ft-897 mode */

static int set_mode_ft100(struct rpt *myrpt, char newmode);




/* Set tone encode and decode modes */

static int set_ctcss_mode_ft100(struct rpt *myrpt, char txplon, char rxplon);





/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft100(struct rpt *myrpt, char *txtone, char *rxtone);



static int set_ft100(struct rpt *myrpt);


static int closerem_ft100(struct rpt *myrpt);




/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

static int multimode_bump_freq_ft100(struct rpt *myrpt, int interval);

/*
* FT-950 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


static int check_freq_ft950(int m, int d, int *defmode);

/*
* Set a new frequency for the ft950
*/

static int set_freq_ft950(struct rpt *myrpt, char *newfreq);



/* ft-950 offset */

static int set_offset_ft950(struct rpt *myrpt, char offset);

/* ft-950 mode */

static int set_mode_ft950(struct rpt *myrpt, char newmode);


/* Set tone encode and decode modes */

static int set_ctcss_mode_ft950(struct rpt *myrpt, char txplon, char rxplon);
static int set_ctcss_freq_ft950(struct rpt *myrpt, char *txtone, char *rxtone);


static int set_ft950(struct rpt *myrpt);

/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

static int multimode_bump_freq_ft950(struct rpt *myrpt, int interval);

/*
* IC-706 I/O handlers
*/

/* Check to see that the frequency is valid */
/* returns 0 if frequency is valid          */

static int check_freq_ic706(int m, int d, int *defmode, char mars);


		/* take a PL frequency and turn it into a code */
		static int ic706_pltocode(char *str);



/* ic-706 simple commands */

static int simple_command_ic706(struct rpt *myrpt, char command, char subcommand);


/*
* Set a new frequency for the ic706
*/

static int set_freq_ic706(struct rpt *myrpt, char *newfreq);

/* ic-706 offset */

static int set_offset_ic706(struct rpt *myrpt, char offset);


/* ic-706 mode */

static int set_mode_ic706(struct rpt *myrpt, char newmode);


/* Set tone encode and decode modes */

static int set_ctcss_mode_ic706(struct rpt *myrpt, char txplon, char rxplon);



/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ic706(struct rpt *myrpt, char *txtone, char *rxtone);

static int vfo_ic706(struct rpt *myrpt);

static int mem2vfo_ic706(struct rpt *myrpt);

static int select_mem_ic706(struct rpt *myrpt, int slot);

static int set_ic706(struct rpt *myrpt);

/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

static int multimode_bump_freq_ic706(struct rpt *myrpt, int interval);

/*
* XCAT I/O handlers
*/

/* Check to see that the frequency is valid */
/* returns 0 if frequency is valid          */


static int check_freq_xcat(int m, int d, int *defmode);

static int simple_command_xcat(struct rpt *myrpt, char command, char subcommand);
/*
* Set a new frequency for the xcat
*/

static int set_freq_xcat(struct rpt *myrpt, char *newfreq);

static int set_offset_xcat(struct rpt *myrpt, char offset;
/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_xcat(struct rpt *myrpt, char *txtone, char *rxtone);

static int set_xcat(struct rpt *myrpt);

/*
* Dispatch to correct I/O handler
*/
static int setrem(struct rpt *myrpt);

static int closerem(struct rpt *myrpt);

/*
* Dispatch to correct RX frequency checker
*/

static int check_freq(struct rpt *myrpt, int m, int d, int *defmode);

/*
 * Check TX frequency before transmitting
   rv=1 if tx frequency in ok.
*/

static char check_tx_freq(struct rpt *myrpt);

/*
* Dispatch to correct frequency bumping function
*/

static int multimode_bump_freq(struct rpt *myrpt, int interval);


/*
* Queue announcment that scan has been stopped
*/

static void stop_scan(struct rpt *myrpt);


/*
* This is called periodically when in scan mode
*/


static int service_scan(struct rpt *myrpt);
/*
	retrieve memory setting and set radio
*/
static int get_mem_set(struct rpt *myrpt, char *digitbuf);

/*
	steer the radio selected channel to either one programmed into the radio
	or if the radio is VFO agile, to an rpt.conf memory location.
*/
static int channel_steer(struct rpt *myrpt, char *data);

/*
*/
static int channel_revert(struct rpt *myrpt);

/*
* Remote base function
*/

static int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);





























































