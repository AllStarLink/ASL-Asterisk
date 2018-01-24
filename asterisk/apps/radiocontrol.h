


static int sendrxkenwood(struct rpt *myrpt, char *txstr, char *rxstr,char *cmpstr);

static int setkenwood(struct rpt *myrpt);

static int set_tmd700(struct rpt *myrpt);
static int set_tm271(struct rpt *myrpt);

static int sendkenwood(struct rpt *myrpt,char *txstr, char *rxstr);

/* take a PL frequency and turn it into a code */
static int tm271_pltocode(char *str);


/* take a PL frequency and turn it into a code */
static int ft950_pltocode(char *str)



/* take a PL frequency and turn it into a code */
static int ft100_pltocode(char *str)

static int check_freq_kenwood(int m, int d, int *defmode)


static int check_freq_tm271(int m, int d, int *defmode)



/* Check for valid rbi frequency */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_rbi(int m, int d, int *defmode)


/* Check for valid rtx frequency */
/* Hard coded limits now, configurable later, maybe? */

static int check_freq_rtx(int m, int d, int *defmode, struct rpt *myrpt)


/*
 * Convert decimals of frequency to int
 */

static int decimals2int(char *fraction)


/*
* Split frequency into mhz and decimals
*/

static int split_freq(char *mhz, char *decimals, char *freq)



/*
* Split ctcss frequency into hertz and decimal
*/

static int split_ctcss_freq(char *hertz, char *decimal, char *freq)





/*
* FT-897 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


static int check_freq_ft897(int m, int d, int *defmode)




/*
* Set a new frequency for the FT897
*/

static int set_freq_ft897(struct rpt *myrpt, char *newfreq)


/* ft-897 simple commands */

static int simple_command_ft897(struct rpt *myrpt, char command)


/* ft-897 offset */

static int set_offset_ft897(struct rpt *myrpt, char offset)


/* ft-897 mode */

static int set_mode_ft897(struct rpt *myrpt, char newmode)

/* Set tone encode and decode modes */

static int set_ctcss_mode_ft897(struct rpt *myrpt, char txplon, char rxplon)


/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft897(struct rpt *myrpt, char *txtone, char *rxtone)


static int set_ft897(struct rpt *myrpt)


static int closerem_ft897(struct rpt *myrpt)


/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

static int multimode_bump_freq_ft897(struct rpt *myrpt, int interval)


/*
* FT-100 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


static int check_freq_ft100(int m, int d, int *defmode)


/*
* Set a new frequency for the ft100
*/

static int set_freq_ft100(struct rpt *myrpt, char *newfreq)


/* ft-897 simple commands */

static int simple_command_ft100(struct rpt *myrpt, unsigned char command, unsigned char p1)



/* ft-897 offset */

static int set_offset_ft100(struct rpt *myrpt, char offset)



/* ft-897 mode */

static int set_mode_ft100(struct rpt *myrpt, char newmode)




/* Set tone encode and decode modes */

static int set_ctcss_mode_ft100(struct rpt *myrpt, char txplon, char rxplon)





/* Set transmit and receive ctcss tone frequencies */

static int set_ctcss_freq_ft100(struct rpt *myrpt, char *txtone, char *rxtone)



static int set_ft100(struct rpt *myrpt)


static int closerem_ft100(struct rpt *myrpt)




/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

static int multimode_bump_freq_ft100(struct rpt *myrpt, int interval)

/*
* FT-950 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


static int check_freq_ft950(int m, int d, int *defmode)

/*
* Set a new frequency for the ft950
*/

static int set_freq_ft950(struct rpt *myrpt, char *newfreq)



/* ft-950 offset */

static int set_offset_ft950(struct rpt *myrpt, char offset)

/* ft-950 mode */

static int set_mode_ft950(struct rpt *myrpt, char newmode)


/* Set tone encode and decode modes */

static int set_ctcss_mode_ft950(struct rpt *myrpt, char txplon, char rxplon)





























