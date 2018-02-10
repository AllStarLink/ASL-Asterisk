
#ifndef ALLSTARUTILS_H
#define ALLSTARUTILS_H

/*
 * Structure that holds information regarding app_rpt operation
*/
struct rpt;


void rpt_telemetry(struct rpt *myrpt,int mode, void *data);
//static int multimode_capable(struct rpt *myrpt);
int play_tone_pair(struct ast_channel *chan, int f1, int f2, int duration, int amplitude);
int play_tone(struct ast_channel *chan, int freq, int duration, int amplitude);
//static int saynum(struct ast_channel *mychannel, int num);

// void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink);
// static void *rpt_tele_thread(void *this);
//static void send_tele_link(struct rpt *myrpt,char *cmd);

#endif
