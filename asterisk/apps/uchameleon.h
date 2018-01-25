static int uchameleon_do_long( struct daq_entry_tag *t, int pin,
int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);
static int matchkeyword(char *string, char **param, char *keywords[]);
static int explode_string(char *str, char *strp[], int limit, char delim, char quote);
static void *uchameleon_monitor_thread(void *this);
static char *strupr(char *str);

static int saynum(struct ast_channel *mychannel, int num);
static int sayfile(struct ast_channel *mychannel,char *fname);
static int wait_interval(struct rpt *myrpt, int type, struct ast_channel *chan);
static void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink);
static void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink);

static struct daq_entry_tag *daq_open(int type, char *name, char *dev);

static int daq_close(struct daq_entry_tag *t);

/*
 * Look up a device entry for a particular device name
 */

static struct daq_entry_tag *daq_devtoentry(char *name);

/*
 * Do something with the daq subsystem
 */

static int daq_do_long( struct daq_entry_tag *t, int pin, int cmd,
void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);

/*
 * Short version of above
 */

static int daq_do( struct daq_entry_tag *t, int pin, int cmd, int arg1);
/*
 * Function to reset the long term minimum or maximum
 */

static int daq_reset_minmax(char *device, int pin, int minmax);

/*
 * Initialize DAQ subsystem
 */

static void daq_init(struct ast_config *cfg);

/*
 * Uninitialize DAQ Subsystem
 */

static void daq_uninit(void);

/*
 * Start the Uchameleon monitor thread
 */
static int uchameleon_thread_start(struct daq_entry_tag *t);

static int uchameleon_connect(struct daq_entry_tag *t);

/*
 * Uchameleon alarm handler
 */


static void uchameleon_alarm_handler(struct daq_pin_entry_tag *p);

/*
 * Initialize pins
 */



static int uchameleon_pin_init(struct daq_entry_tag *t);

/*
 * Open the serial channel and test for the uchameleon device at the end of the link
 */

static int uchameleon_open(struct daq_entry_tag *t);

/*
 * Close uchameleon
 */

static int uchameleon_close(struct daq_entry_tag *t);

/*
 * Uchameleon generic interface which supports monitor thread
 */

static int uchameleon_do_long( struct daq_entry_tag *t, int pin,
int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);



/*
 * Reset a minimum or maximum reading
 */

static int uchameleon_reset_minmax(struct daq_entry_tag *t, int pin, int minmax);



/*
 * Queue up a tx command (used exclusively by uchameleon_monitor() )
 */

static void uchameleon_queue_tx(struct daq_entry_tag *t, char *txbuff;




/*
 * Monitor thread for Uchameleon devices
 *
 * started by uchameleon_open() and shutdown by uchameleon_close()
 *
 */
static void *uchameleon_monitor_thread(void *this);








































