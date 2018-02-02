/* General utils for Allstar */


#include "allstar/allstarutils.h"


/*
 *  Repeater telemetry routines. Used radiocontrol, uchameleon and app_rpt.
 */

void rpt_telemetry(struct rpt *myrpt,int mode, void *data)
{
struct rpt_tele *tele;
struct rpt_link *mylink = NULL;
int res,vmajor,vminor,i,ns;
pthread_attr_t attr;
char *v1, *v2,mystr[300],*p,haslink,lat[100],lon[100],elev[100];
char lbuf[MAXLINKLIST],*strs[MAXLINKLIST];
time_t	t,was;
unsigned int k;
FILE *fp;
struct stat mystat;
struct rpt_link *l;

	if(debug >= 6)
		ast_log(LOG_NOTICE,"Tracepoint rpt_telemetry() entered mode=%i\n",mode);


	if ((mode == ID) && is_paging(myrpt))
	{
		myrpt->deferid = 1;
		return;
	}

	switch(mode)
	{
	    case CONNECTED:
 		mylink = (struct rpt_link *) data;
		if ((mylink->name[0] == '3') && (!myrpt->p.eannmode)) return;
		break;
	    case REMDISC:
 		mylink = (struct rpt_link *) data;
		if ((mylink->name[0] == '3') && (!myrpt->p.eannmode)) return;
		if ((!mylink) || (mylink->name[0] == '0')) return;
		if ((!mylink->gott) && (!mylink->isremote) && (!mylink->outbound) &&
		    mylink->chan && strncasecmp(mylink->chan->name,"echolink",8) &&
			strncasecmp(mylink->chan->name,"tlb",3)) return;
		break;
	    case VARCMD:
		if (myrpt->telemmode < 2) return;
		break;
	    case UNKEY:
	    case LOCUNKEY:
		if (myrpt->p.nounkeyct) return;
		/* if any of the following are defined, go ahead and do it,
		   otherwise, dont bother */
		v1 = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name,
			"unlinkedct");
		v2 = (char *) ast_variable_retrieve(myrpt->cfg, myrpt->name,
			"remotect");
		if (telem_lookup(myrpt,NULL, myrpt->name, "remotemon") &&
		  telem_lookup(myrpt,NULL, myrpt->name, "remotetx") &&
		  telem_lookup(myrpt,NULL, myrpt->name, "cmdmode") &&
		  (!(v1 && telem_lookup(myrpt,NULL, myrpt->name, v1))) &&
		  (!(v2 && telem_lookup(myrpt,NULL, myrpt->name, v2)))) return;
		break;
	    case LINKUNKEY:
 		mylink = (struct rpt_link *) data;
		if (myrpt->p.locallinknodesn)
		{
			int v,w;

			w = 0;
			for(v = 0; v < myrpt->p.locallinknodesn; v++)
			{
				if (strcmp(mylink->name,myrpt->p.locallinknodes[v])) continue;
				w = 1;
				break;
			}
			if (w) break;
		}
		if (!ast_variable_retrieve(myrpt->cfg, myrpt->name, "linkunkeyct"))
			return;
		break;
	    default:
		break;
	}
	if (!myrpt->remote) /* dont do if we are a remote */
	{
		/* send appropriate commands to everyone on link(s) */
		switch(mode)
		{

		    case REMGO:
			send_tele_link(myrpt,"REMGO");
			return;
		    case REMALREADY:
			send_tele_link(myrpt,"REMALREADY");
			return;
		    case REMNOTFOUND:
			send_tele_link(myrpt,"REMNOTFOUND");
			return;
		    case COMPLETE:
			send_tele_link(myrpt,"COMPLETE");
			return;
		    case PROC:
			send_tele_link(myrpt,"PROC");
			return;
		    case TERM:
			send_tele_link(myrpt,"TERM");
			return;
		    case MACRO_NOTFOUND:
			send_tele_link(myrpt,"MACRO_NOTFOUND");
			return;
		    case MACRO_BUSY:
			send_tele_link(myrpt,"MACRO_BUSY");
			return;
		    case CONNECTED:
			mylink = (struct rpt_link *) data;
			if ((!mylink) || (mylink->name[0] == '0')) return;
			sprintf(mystr,"CONNECTED,%s,%s",myrpt->name,mylink->name);
			send_tele_link(myrpt,mystr);
			return;
		    case CONNFAIL:
			mylink = (struct rpt_link *) data;
			if ((!mylink) || (mylink->name[0] == '0')) return;
			sprintf(mystr,"CONNFAIL,%s",mylink->name);
			send_tele_link(myrpt,mystr);
			return;
		    case REMDISC:
			mylink = (struct rpt_link *) data;
			if ((!mylink) || (mylink->name[0] == '0')) return;
			l = myrpt->links.next;
			haslink = 0;
			/* dont report if a link for this one still on system */
			if (l != &myrpt->links)
			{
				rpt_mutex_lock(&myrpt->lock);
				while(l != &myrpt->links)
				{
					if (l->name[0] == '0')
					{
						l = l->next;
						continue;
					}
					if (!strcmp(l->name,mylink->name))
					{
						haslink = 1;
						break;
					}
					l = l->next;
				}
				rpt_mutex_unlock(&myrpt->lock);
			}
			if (haslink) return;
			sprintf(mystr,"REMDISC,%s",mylink->name);
			send_tele_link(myrpt,mystr);
			return;
		    case STATS_TIME:
			t = time(NULL);
			sprintf(mystr,"STATS_TIME,%u",(unsigned int) t);
			send_tele_link(myrpt,mystr);
			return;
		    case STATS_VERSION:
			p = strstr(tdesc, "version");
			if (!p) return;
			if(sscanf(p, "version %d.%d", &vmajor, &vminor) != 2)
				return;
			sprintf(mystr,"STATS_VERSION,%d.%d",vmajor,vminor);
			send_tele_link(myrpt,mystr);
			return;
		    case STATS_GPS:
			fp = fopen(GPSFILE,"r");
			if (!fp) break;
			if (fstat(fileno(fp),&mystat) == -1) break;
			if (mystat.st_size >= 100) break;
			elev[0] = 0;
			if (fscanf(fp,"%u %s %s %s",&k,lat,lon,elev) < 3) break;
			fclose(fp);
			was = (time_t) k;
			time(&t);
			if ((was + GPS_VALID_SECS) < t) break;
			sprintf(mystr,"STATS_GPS,%s,%s,%s,%s",myrpt->name,
				lat,lon,elev);
			send_tele_link(myrpt,mystr);
			return;
		    case ARB_ALPHA:
			sprintf(mystr,"ARB_ALPHA,%s",(char *)data);
			send_tele_link(myrpt,mystr);
			return;
		    case REV_PATCH:
			p = (char *)data;
			for(i = 0; p[i]; i++) if (p[i] == ',') p[i] = '^';
			sprintf(mystr,"REV_PATCH,%s,%s",myrpt->name,p);
			send_tele_link(myrpt,mystr);
			return;
		    case LASTNODEKEY:
			if (!myrpt->lastnodewhichkeyedusup[0]) return;
			sprintf(mystr,"LASTNODEKEY,%s",myrpt->lastnodewhichkeyedusup);
			send_tele_link(myrpt,mystr);
			return;
		    case LASTUSER:
			if ((!myrpt->lastdtmfuser[0]) && (!myrpt->curdtmfuser[0])) return;
			else if (myrpt->lastdtmfuser[0] && (!myrpt->curdtmfuser[0]))
				sprintf(mystr,"LASTUSER,%s",myrpt->lastdtmfuser);
			else if ((!myrpt->lastdtmfuser[0]) && myrpt->curdtmfuser[0])
				sprintf(mystr,"LASTUSER,%s",myrpt->curdtmfuser);
			else
			{
				if (strcmp(myrpt->curdtmfuser,myrpt->lastdtmfuser))
					sprintf(mystr,"LASTUSER,%s,%s",myrpt->curdtmfuser,myrpt->lastdtmfuser);
				else
					sprintf(mystr,"LASTUSER,%s",myrpt->curdtmfuser);
			}
			send_tele_link(myrpt,mystr);
			return;
		    case STATUS:
			rpt_mutex_lock(&myrpt->lock);
			sprintf(mystr,"STATUS,%s,%d",myrpt->name,myrpt->callmode);
			/* make our own list of links */
			l = myrpt->links.next;
			while(l != &myrpt->links)
			{
				char s;

				if (l->name[0] == '0')
				{
					l = l->next;
					continue;
				}
				s = 'T';
				if (!l->mode) s = 'R';
				if (l->mode > 1) s = 'L';
				if (!l->thisconnected) s = 'C';
				snprintf(mystr + strlen(mystr),sizeof(mystr),",%c%s",
					s,l->name);
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
			send_tele_link(myrpt,mystr);
			return;
		    case FULLSTATUS:
			rpt_mutex_lock(&myrpt->lock);
			sprintf(mystr,"STATUS,%s,%d",myrpt->name,myrpt->callmode);
			/* get all the nodes */
			__mklinklist(myrpt,NULL,lbuf,0);
			rpt_mutex_unlock(&myrpt->lock);
			/* parse em */
			ns = finddelim(lbuf,strs,MAXLINKLIST);
			/* sort em */
			if (ns) qsort((void *)strs,ns,sizeof(char *),mycompar);
			/* go thru all the nodes in list */
			for(i = 0; i < ns; i++)
			{
				char s,m = 'T';

				/* if a mode spec at first, handle it */
				if ((*strs[i] < '0') || (*strs[i] > '9'))
				{
					m = *strs[i];
					strs[i]++;
				}
				s = 'T';
				if (m == 'R') s = 'R';
				if (m == 'C') s = 'C';
				snprintf(mystr + strlen(mystr),sizeof(mystr),",%c%s",
					s,strs[i]);
			}
			send_tele_link(myrpt,mystr);
			return;
		}
	}
	tele = ast_malloc(sizeof(struct rpt_tele));
	if (!tele)
	{
		ast_log(LOG_WARNING, "Unable to allocate memory\n");
		pthread_exit(NULL);
		return;
	}
	/* zero it out */
	memset((char *)tele,0,sizeof(struct rpt_tele));
	tele->rpt = myrpt;
	tele->mode = mode;
	if (mode == PARROT) {
		tele->submode.p = data;
		tele->parrot = (unsigned int) tele->submode.i;
		tele->submode.p = 0;
	}
	else mylink = (struct rpt_link *) (void *) data;
	rpt_mutex_lock(&myrpt->lock);
	if((mode == CONNFAIL) || (mode == REMDISC) || (mode == CONNECTED) ||
	    (mode == LINKUNKEY)){
		memset(&tele->mylink,0,sizeof(struct rpt_link));
		if (mylink){
			memcpy(&tele->mylink,mylink,sizeof(struct rpt_link));
		}
	}
	else if ((mode == ARB_ALPHA) || (mode == REV_PATCH) ||
	    (mode == PLAYBACK) || (mode == LOCALPLAY) ||
            (mode == VARCMD) || (mode == METER) || (mode == USEROUT)) {
		strncpy(tele->param, (char *) data, TELEPARAMSIZE - 1);
		tele->param[TELEPARAMSIZE - 1] = 0;
	}
	if ((mode == REMXXX) || (mode == PAGE) || (mode == MDC1200)) tele->submode.p= data;
	insque((struct qelem *)tele, (struct qelem *)myrpt->tele.next);
	rpt_mutex_unlock(&myrpt->lock);
        pthread_attr_init(&attr);
        pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_DETACHED);
	res = ast_pthread_create(&tele->threadid,&attr,rpt_tele_thread,(void *) tele);
	if(res < 0){
		rpt_mutex_lock(&myrpt->lock);
		remque((struct qlem *) tele); /* We don't like stuck transmitters, remove it from the queue */
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_WARNING, "Could not create telemetry thread: %s",strerror(res));
	}
	if(debug >= 6)
			ast_log(LOG_NOTICE,"Tracepoint rpt_telemetry() exit\n");

	return;
}



