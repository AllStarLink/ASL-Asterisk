/* uchameleon header */
#ifndef UCHAMELEON_H
#define UCHAMELEON_H

//static int matchkeyword1(char *string, char **param, char *keywords[]);

int explode_string1(char *str, char *strp[], int limit, char delim, char quote);
void *uchameleon_monitor_thread(void *this);


static int saynum1(struct ast_channel *mychannel, int num);
static int sayfile(struct ast_channel *mychannel,char *fname);
//static void rpt_telem_select1(struct rpt *myrpt, int command_source, struct rpt_link *mylink);

struct daq_entry_tag *daq_open(int type, char *name, char *dev);

int daq_close(struct daq_entry_tag *t);

/*
 * Look up a device entry for a particular device name
 */

struct daq_entry_tag *daq_devtoentry(char *name);

/*
 * Do something with the daq subsystem
 */

int daq_do_long( struct daq_entry_tag *t, int pin, int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);

/*
 * Short version of above
 */

int daq_do( struct daq_entry_tag *t, int pin, int cmd, int arg1);
/*
 * Function to reset the long term minimum or maximum
 */

int daq_reset_minmax(char *device, int pin, int minmax);

/*
 * Initialize DAQ subsystem
 */

void daq_init(struct ast_config *cfg);

/*
 * Uninitialize DAQ Subsystem
 */

void daq_uninit(void);

/*
 * Start the Uchameleon monitor thread
 */
int uchameleon_thread_start(struct daq_entry_tag *t);

int uchameleon_connect(struct daq_entry_tag *t);

/*
 * Uchameleon alarm handler
 */

void uchameleon_alarm_handler(struct daq_pin_entry_tag *p);

/*
 * Initialize pins
 */

int uchameleon_pin_init(struct daq_entry_tag *t);

/*
 * Open the serial channel and test for the uchameleon device at the end of the link
 */

int uchameleon_open(struct daq_entry_tag *t);

/*
 * Close uchameleon
 */

int uchameleon_close(struct daq_entry_tag *t);

/*
 * Uchameleon generic interface which supports monitor thread
 */

int uchameleon_do_long( struct daq_entry_tag *t, int pin, int cmd, void (*exec)(struct daq_pin_entry_tag *), int *arg1, void *arg2);


/*
 * Reset a minimum or maximum reading
 */

int uchameleon_reset_minmax(struct daq_entry_tag *t, int pin, int minmax);


/*
 * Queue up a tx command (used exclusively by uchameleon_monitor() )
 */

void uchameleon_queue_tx(struct daq_entry_tag *t, char *txbuff);


/*
 * Monitor thread for Uchameleon devices
 *
 * started by uchameleon_open() and shutdown by uchameleon_close()
 *
 */
void *uchameleon_monitor_thread(void *this);

/*
 * Parse a request METER request for telemetry thread
 * This is passed in a comma separated list of items from the function table entry
 * There should be 3 or 4 fields in the function table entry: device, channel, meter face, and  optionally: filter
 */

int handle_meter_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args);


/*
 * Handle USEROUT telemetry
 */

int handle_userout_tele(struct rpt *myrpt, struct ast_channel *mychannel, char *args);

/*
*  Playback a meter reading
*/
int function_meter(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);


/*
*  Set or reset a USER Output bit
*/
int function_userout(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);


/*
*  Execute shell command
*/
int function_cmd(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink);


		/*
		 **********************
		 * End of DAQ functions*
		 * *********************
		 */
#endif /* UCHAMELEON_H  */





















