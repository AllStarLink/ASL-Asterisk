
#ifndef ALLSTARUTILS_H
#define ALLSTARUTILS_H


void rpt_telemetry(struct rpt *myrpt,int mode, void *data);
// void rpt_telem_select(struct rpt *myrpt, int command_source, struct rpt_link *mylink);
static void *rpt_tele_thread(void *this);
static void send_tele_link(struct rpt *myrpt,char *cmd);

#endif
