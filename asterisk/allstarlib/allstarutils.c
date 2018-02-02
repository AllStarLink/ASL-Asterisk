/* General utils for Allstar */


#include "allstar/allstarutils.h"



static void send_tele_link(struct rpt *myrpt,char *cmd)
{
char	str[400];
struct	ast_frame wf;
struct	rpt_link *l;

	snprintf(str, sizeof(str) - 1, "T %s %s", myrpt->name,cmd);
	wf.frametype = AST_FRAME_TEXT;
	wf.subclass = 0;
	wf.offset = 0;
	wf.mallocd = 0;
	wf.datalen = strlen(str) + 1;
	wf.samples = 0;
	wf.src = "send_tele_link";
	l = myrpt->links.next;
	/* give it to everyone */
	while(l != &myrpt->links)
	{
		AST_FRAME_DATA(wf) = str;
		if (l->chan && (l->mode == 1)) rpt_qwrite(l,&wf);
		l = l->next;
	}
	rpt_telemetry(myrpt,VARCMD,cmd);
	return;
}


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


/*
 *  Threaded telemetry handling routines - goes hand in hand with the previous routine (see above)
 *  This routine does a lot of processing of what you "hear" when app_rpt is running.
 *  Note that this routine could probably benefit from an overhaul to make it easier to read/debug.
 *  Many of the items here seem to have been bolted onto this routine as it app_rpt has evolved.
 */

static void *rpt_tele_thread(void *this)
{
struct dahdi_confinfo ci;  /* conference info */
int	res = 0,haslink,hastx,hasremote,imdone = 0, unkeys_queued, x;
struct	rpt_tele *mytele = (struct rpt_tele *)this;
struct  rpt_tele *tlist;
struct	rpt *myrpt;
struct	rpt_link *l,*l1,linkbase;
struct	ast_channel *mychannel;
int id_malloc, vmajor, vminor, m;
char *p,*ct,*ct_copy,*ident, *nodename,*cp;
time_t t,t1,was;
#ifdef	NEW_ASTERISK
struct ast_tm localtm;
#else
struct tm localtm;
#endif
char lbuf[MAXLINKLIST],*strs[MAXLINKLIST];
int	i,j,k,ns,rbimode;
unsigned int u;
char mhz[MAXREMSTR],decimals[MAXREMSTR],mystr[200];
char	lat[100],lon[100],elev[100],c;
FILE	*fp;
float	f;
struct stat mystat;
struct dahdi_params par;
#ifdef	_MDC_ENCODE_H_
struct	mdcparams *mdcp;
#endif

	/* get a pointer to myrpt */
	myrpt = mytele->rpt;

	/* Snag copies of a few key myrpt variables */
	rpt_mutex_lock(&myrpt->lock);
	nodename = ast_strdup(myrpt->name);
	if(!nodename)
	{
	    fprintf(stderr,"rpt:Sorry unable strdup nodename\n");
	    rpt_mutex_lock(&myrpt->lock);
	    remque((struct qelem *)mytele);
	    ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
	    rpt_mutex_unlock(&myrpt->lock);
	    ast_free(mytele);
	    pthread_exit(NULL);
	}

	if (myrpt->p.ident){
		ident = ast_strdup(myrpt->p.ident);
        	if(!ident)
		{
        	        fprintf(stderr,"rpt:Sorry unable strdup ident\n");
			rpt_mutex_lock(&myrpt->lock);
                	remque((struct qelem *)mytele);
                	ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",
			__LINE__, mytele->mode); /*@@@@@@@@@@@*/
                	rpt_mutex_unlock(&myrpt->lock);
			ast_free(nodename);
                	ast_free(mytele);
                	pthread_exit(NULL);
        	}
		else{
			id_malloc = 1;
		}
	}
	else
	{
		ident = "";
		id_malloc = 0;
	}
	rpt_mutex_unlock(&myrpt->lock);



	/* allocate a pseudo-channel thru asterisk */
	mychannel = ast_request(DAHDI_CHANNEL_NAME,AST_FORMAT_SLINEAR,"pseudo",NULL);
	if (!mychannel)
	{
		fprintf(stderr,"rpt:Sorry unable to obtain pseudo channel\n");
		rpt_mutex_lock(&myrpt->lock);
		remque((struct qelem *)mytele);
		ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
		rpt_mutex_unlock(&myrpt->lock);
		ast_free(nodename);
		if(id_malloc)
			ast_free(ident);
		ast_free(mytele);
		pthread_exit(NULL);
	}
#ifdef	AST_CDR_FLAG_POST_DISABLED
	if (mychannel->cdr)
		ast_set_flag(mychannel->cdr,AST_CDR_FLAG_POST_DISABLED);
#endif
	ast_answer(mychannel);
	rpt_mutex_lock(&myrpt->lock);
	mytele->chan = mychannel;
	while (myrpt->active_telem &&
	    ((myrpt->active_telem->mode == PAGE) || (
		myrpt->active_telem->mode == MDC1200)))
	{
                rpt_mutex_unlock(&myrpt->lock);
		usleep(100000);
		rpt_mutex_lock(&myrpt->lock);
	}
	rpt_mutex_unlock(&myrpt->lock);
	while((mytele->mode != SETREMOTE) && (mytele->mode != UNKEY) &&
	    (mytele->mode != LINKUNKEY) && (mytele->mode != LOCUNKEY) &&
		(mytele->mode != COMPLETE) && (mytele->mode != REMGO) &&
		    (mytele->mode != REMCOMPLETE))
	{
                rpt_mutex_lock(&myrpt->lock);
		if ((!myrpt->active_telem) &&
			(myrpt->tele.prev == mytele))
		{
			myrpt->active_telem = mytele;
	                rpt_mutex_unlock(&myrpt->lock);
			break;
		}
                rpt_mutex_unlock(&myrpt->lock);
		usleep(100000);
	}

	/* make a conference for the tx */
	ci.chan = 0;
	/* If the telemetry is only intended for a local audience, */
	/* only connect the ID audio to the local tx conference so */
	/* linked systems can't hear it */
	ci.confno = (((mytele->mode == ID1) || (mytele->mode == PLAYBACK) ||
	    (mytele->mode == TEST_TONE) || (mytele->mode == STATS_GPS_LEGACY)) ?
		myrpt->conf : myrpt->txconf);
	ci.confmode = DAHDI_CONF_CONFANN;
	/* first put the channel on the conference in announce mode */
	if (ioctl(mychannel->fds[0],DAHDI_SETCONF,&ci) == -1)
	{
		ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
		rpt_mutex_lock(&myrpt->lock);
		myrpt->active_telem = NULL;
		remque((struct qelem *)mytele);
		rpt_mutex_unlock(&myrpt->lock);
		ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
		ast_free(nodename);
		if(id_malloc)
			ast_free(ident);
		ast_free(mytele);
		ast_hangup(mychannel);
		pthread_exit(NULL);
	}
	ast_stopstream(mychannel);
	res = 0;
	switch(mytele->mode)
	{
	    case USEROUT:
		handle_userout_tele(myrpt, mychannel, mytele->param);
		imdone = 1;
		break;

	    case METER:
		handle_meter_tele(myrpt, mychannel, mytele->param);
		imdone = 1;
		break;

	    case VARCMD:
		handle_varcmd_tele(myrpt,mychannel,mytele->param);
		imdone = 1;
		break;
	    case ID:
	    case ID1:
		if (*ident)
		{
			/* wait a bit */
			if (!wait_interval(myrpt, (mytele->mode == ID) ? DLY_ID : DLY_TELEM,mychannel))
				res = telem_any(myrpt,mychannel, ident);
		}
		imdone=1;
		break;

	    case TAILMSG:
		/* wait a little bit longer */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, myrpt->p.tailmessages[myrpt->tailmessagen], mychannel->language);
		break;

	    case IDTALKOVER:
		if(debug >= 6)
			ast_log(LOG_NOTICE,"Tracepoint IDTALKOVER: in rpt_tele_thread()\n");
	    	p = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "idtalkover");
	    	if(p)
			res = telem_any(myrpt,mychannel, p);
		imdone=1;
	    	break;

	    case PROC:
		/* wait a little bit longer */
		if (wait_interval(myrpt, DLY_TELEM, mychannel))
			res = telem_lookup(myrpt, mychannel, myrpt->name, "patchup");
		if(res < 0){ /* Then default message */
			res = ast_streamfile(mychannel, "rpt/callproceeding", mychannel->language);
		}
		break;
	    case TERM:
		/* wait a little bit longer */
		if (!wait_interval(myrpt, DLY_CALLTERM, mychannel))
			res = telem_lookup(myrpt, mychannel, myrpt->name, "patchdown");
		if(res < 0){ /* Then default message */
			res = ast_streamfile(mychannel, "rpt/callterminated", mychannel->language);
		}
		break;
	    case COMPLETE:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		break;
	    case REMCOMPLETE:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = telem_lookup(myrpt,mychannel, myrpt->name, "remcomplete");
		break;
	    case MACRO_NOTFOUND:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/macro_notfound", mychannel->language);
		break;
	    case MACRO_BUSY:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/macro_busy", mychannel->language);
		break;
	    case PAGE:
		if (!wait_interval(myrpt, DLY_TELEM,  mychannel))
		{
			res = -1;
			if (mytele->submode.p)
			{
				res = ast_playtones_start(myrpt->txchannel,0,
					(char *) mytele->submode.p,0);
				while(myrpt->txchannel->generatordata)
				{
					if(ast_safe_sleep(myrpt->txchannel, 50))
					{
						res = -1;
						break;
					}
				}
				free((char *)mytele->submode.p);
			}
		}
		imdone = 1;
		break;
#ifdef	_MDC_ENCODE_H_
	    case MDC1200:
		mdcp = (struct mdcparams *)mytele->submode.p;
		if (mdcp)
		{
			if (mdcp->type[0] != 'A')
			{
				if (wait_interval(myrpt, DLY_TELEM,  mychannel) == -1)
				{
					res = -1;
					imdone = 1;
					break;
				}
			}
			else
			{
				if (wait_interval(myrpt, DLY_MDC1200,  mychannel) == -1)
				{
					res = -1;
					imdone = 1;
					break;
				}
			}
			res = mdc1200gen_start(myrpt->txchannel,mdcp->type,mdcp->UnitID,mdcp->DestID,mdcp->subcode);
			ast_free(mdcp);
			while(myrpt->txchannel->generatordata)
			{
				if(ast_safe_sleep(myrpt->txchannel, 50))
				{
					res = -1;
					break;
				}
			}
		}
		imdone = 1;
		break;
#endif
	    case UNKEY:
	    case LOCUNKEY:
		if(myrpt->patchnoct && myrpt->callmode){ /* If no CT during patch configured, then don't send one */
			imdone = 1;
			break;
		}

		/*
		* Reset the Unkey to CT timer
		*/

		x = get_wait_interval(myrpt, DLY_UNKEY);
		rpt_mutex_lock(&myrpt->lock);
		myrpt->unkeytocttimer = x; /* Must be protected as it is changed below */
		rpt_mutex_unlock(&myrpt->lock);

		/*
		* If there's one already queued, don't do another
		*/

		tlist = myrpt->tele.next;
		unkeys_queued = 0;
                if (tlist != &myrpt->tele)
                {
                        rpt_mutex_lock(&myrpt->lock);
                        while(tlist != &myrpt->tele){
                                if ((tlist->mode == UNKEY) ||
				    (tlist->mode == LOCUNKEY)) unkeys_queued++;
                                tlist = tlist->next;
                        }
                        rpt_mutex_unlock(&myrpt->lock);
		}
		if( unkeys_queued > 1){
			imdone = 1;
			break;
		}

		/* Wait for the telemetry timer to expire */
		/* Periodically check the timer since it can be re-initialized above */
		while(myrpt->unkeytocttimer)
		{
			int ctint;
			if(myrpt->unkeytocttimer > 100)
				ctint = 100;
			else
				ctint = myrpt->unkeytocttimer;
			ast_safe_sleep(mychannel, ctint);
			rpt_mutex_lock(&myrpt->lock);
			if(myrpt->unkeytocttimer < ctint)
				myrpt->unkeytocttimer = 0;
			else
				myrpt->unkeytocttimer -= ctint;
			rpt_mutex_unlock(&myrpt->lock);
		}

		/*
		* Now, the carrier on the rptr rx should be gone.
		* If it re-appeared, then forget about sending the CT
		*/
		if(myrpt->keyed){
			imdone = 1;
			break;
		}

		rpt_mutex_lock(&myrpt->lock); /* Update the kerchunk counters */
		myrpt->dailykerchunks++;
		myrpt->totalkerchunks++;
		rpt_mutex_unlock(&myrpt->lock);

treataslocal:

		rpt_mutex_lock(&myrpt->lock);
		/* get all the nodes */
		__mklinklist(myrpt,NULL,lbuf,0);
		rpt_mutex_unlock(&myrpt->lock);
		/* parse em */
		ns = finddelim(lbuf,strs,MAXLINKLIST);
		haslink = 0;
		for(i = 0; i < ns; i++)
		{
			char *cpr = strs[i] + 1;
			if (!strcmp(cpr,myrpt->name)) continue;
			if (ISRANGER(cpr)) haslink = 1;
		}

		/* if has a RANGER node connected to it, use special telemetry for RANGER mode */
		if (haslink)
		{
			res = telem_lookup(myrpt,mychannel, myrpt->name, "ranger");
			if(res)
				ast_log(LOG_WARNING, "telem_lookup:ranger failed on %s\n", mychannel->name);
		}

		if ((mytele->mode == LOCUNKEY) &&
		    ((ct = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "localct")))) { /* Local override ct */
			ct_copy = ast_strdup(ct);
			if(ct_copy)
			{
				res = telem_lookup(myrpt,mychannel, myrpt->name, ct_copy);
				ast_free(ct_copy);
			}
			else
				res = -1;
			if(res)
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);
		}
		haslink = 0;
		hastx = 0;
		hasremote = 0;
		l = myrpt->links.next;
		if (l != &myrpt->links)
		{
			rpt_mutex_lock(&myrpt->lock);
			while(l != &myrpt->links)
			{
				int v,w;

				if (l->name[0] == '0')
				{
					l = l->next;
					continue;
				}
				w = 1;
				if (myrpt->p.nolocallinkct)
				{

					for(v = 0; v < nrpts; v++)
					{
						if (&rpt_vars[v] == myrpt) continue;
						if (rpt_vars[v].remote) continue;
						if (strcmp(rpt_vars[v].name,l->name)) continue;
						w = 0;
						break;
					}
				}
				if (myrpt->p.locallinknodesn)
				{
					for(v = 0; v < myrpt->p.locallinknodesn; v++)
					{
						if (strcmp(l->name,myrpt->p.locallinknodes[v])) continue;
						w = 0;
						break;
					}
				}
				if (w) haslink = 1;
				if (l->mode == 1) {
					hastx++;
					if (l->isremote) hasremote++;
				}
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (haslink)
		{

			res = telem_lookup(myrpt,mychannel, myrpt->name, (!hastx) ? "remotemon" : "remotetx");
			if(res)
				ast_log(LOG_WARNING, "telem_lookup:remotexx failed on %s\n", mychannel->name);


			/* if in remote cmd mode, indicate it */
			if (myrpt->cmdnode[0] && strcmp(myrpt->cmdnode,"aprstt"))
			{
				ast_safe_sleep(mychannel,200);
				res = telem_lookup(myrpt,mychannel, myrpt->name, "cmdmode");
				if(res)
				 	ast_log(LOG_WARNING, "telem_lookup:cmdmode failed on %s\n", mychannel->name);
				ast_stopstream(mychannel);
			}
		}
		else if((ct = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "unlinkedct"))){ /* Unlinked Courtesy Tone */
			ct_copy = ast_strdup(ct);
			if(ct_copy)
			{
				res = telem_lookup(myrpt,mychannel, myrpt->name, ct_copy);
				ast_free(ct_copy);
			}
			else
				res = -1;
			if(res)
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);
		}
		if (hasremote && ((!myrpt->cmdnode[0]) || (!strcmp(myrpt->cmdnode,"aprstt"))))
		{
			/* set for all to hear */
			ci.chan = 0;
			ci.confno = myrpt->conf;
			ci.confmode = DAHDI_CONF_CONFANN;
			/* first put the channel on the conference in announce mode */
			if (ioctl(mychannel->fds[0],DAHDI_SETCONF,&ci) == -1)
			{
				ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
				rpt_mutex_lock(&myrpt->lock);
				myrpt->active_telem = NULL;
				remque((struct qelem *)mytele);
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
				ast_free(nodename);
				if(id_malloc)
					ast_free(ident);
				ast_free(mytele);
				ast_hangup(mychannel);
				pthread_exit(NULL);
			}
			if((ct = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "remotect"))){ /* Unlinked Courtesy Tone */
				ast_safe_sleep(mychannel,200);
				ct_copy = ast_strdup(ct);
				if(ct_copy)
				{
					res = telem_lookup(myrpt,mychannel, myrpt->name, ct_copy);
					ast_free(ct_copy);
				}
				else
					res = -1;

				if(res)
				 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);
			}
		}
#if	defined(_MDC_DECODE_H_) && defined(MDC_SAY_WHEN_DOING_CT)
		if (myrpt->lastunit)
		{
			char mystr[10];

			ast_safe_sleep(mychannel,200);
			/* set for all to hear */
			ci.chan = 0;
			ci.confno = myrpt->txconf;
			ci.confmode = DAHDI_CONF_CONFANN;
			/* first put the channel on the conference in announce mode */
			if (ioctl(mychannel->fds[0],DAHDI_SETCONF,&ci) == -1)
			{
				ast_log(LOG_WARNING, "Unable to set conference mode to Announce\n");
				rpt_mutex_lock(&myrpt->lock);
				myrpt->active_telem = NULL;
				remque((struct qelem *)mytele);
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
				ast_free(nodename);
				if(id_malloc)
					ast_free(ident);
				ast_free(mytele);
				ast_hangup(mychannel);
				pthread_exit(NULL);
			}
			sprintf(mystr,"%04x",myrpt->lastunit);
			myrpt->lastunit = 0;
			ast_say_character_str(mychannel,mystr,NULL,mychannel->language);
			break;
		}
#endif
		imdone = 1;
		break;
	    case LINKUNKEY:
		if(myrpt->patchnoct && myrpt->callmode){ /* If no CT during patch configured, then don't send one */
			imdone = 1;
			break;
		}
		if (myrpt->p.locallinknodesn)
		{
			int v,w;

			w = 0;
			for(v = 0; v < myrpt->p.locallinknodesn; v++)
			{
				if (strcmp(mytele->mylink.name,myrpt->p.locallinknodes[v])) continue;
				w = 1;
				break;
			}
			if (w)
			{
				/*
				* If there's one already queued, don't do another
				*/

				tlist = myrpt->tele.next;
				unkeys_queued = 0;
		                if (tlist != &myrpt->tele)
		                {
		                        rpt_mutex_lock(&myrpt->lock);
		                        while(tlist != &myrpt->tele){
		                                if ((tlist->mode == UNKEY) ||
						    (tlist->mode == LOCUNKEY)) unkeys_queued++;
		                                tlist = tlist->next;
		                        }
		                        rpt_mutex_unlock(&myrpt->lock);
				}
				if( unkeys_queued > 1){
					imdone = 1;
					break;
				}

				x = get_wait_interval(myrpt, DLY_UNKEY);
				rpt_mutex_lock(&myrpt->lock);
				myrpt->unkeytocttimer = x; /* Must be protected as it is changed below */
				rpt_mutex_unlock(&myrpt->lock);

				/* Wait for the telemetry timer to expire */
				/* Periodically check the timer since it can be re-initialized above */
				while(myrpt->unkeytocttimer)
				{
					int ctint;
					if(myrpt->unkeytocttimer > 100)
						ctint = 100;
					else
						ctint = myrpt->unkeytocttimer;
					ast_safe_sleep(mychannel, ctint);
					rpt_mutex_lock(&myrpt->lock);
					if(myrpt->unkeytocttimer < ctint)
						myrpt->unkeytocttimer = 0;
					else
						myrpt->unkeytocttimer -= ctint;
					rpt_mutex_unlock(&myrpt->lock);
				}
			}
			goto treataslocal;
		}
		if (myrpt->p.nolocallinkct) /* if no CT if this guy is on local system */
		{
			int v,w;
			w = 0;
			for(v = 0; v < nrpts; v++)
			{
				if (&rpt_vars[v] == myrpt) continue;
				if (rpt_vars[v].remote) continue;
				if (strcmp(rpt_vars[v].name,
					mytele->mylink.name)) continue;
				w = 1;
				break;
			}
			if (w)
			{
				imdone = 1;
				break;
			}
		}
		/*
		* Reset the Unkey to CT timer
		*/

		x = get_wait_interval(myrpt, DLY_LINKUNKEY);
		mytele->mylink.linkunkeytocttimer = x; /* Must be protected as it is changed below */

		/*
		* If there's one already queued, don't do another
		*/

		tlist = myrpt->tele.next;
		unkeys_queued = 0;
                if (tlist != &myrpt->tele)
                {
                        rpt_mutex_lock(&myrpt->lock);
                        while(tlist != &myrpt->tele){
                                if (tlist->mode == LINKUNKEY) unkeys_queued++;
                                tlist = tlist->next;
                        }
                        rpt_mutex_unlock(&myrpt->lock);
		}
		if( unkeys_queued > 1){
			imdone = 1;
			break;
		}

		/* Wait for the telemetry timer to expire */
		/* Periodically check the timer since it can be re-initialized above */
		while(mytele->mylink.linkunkeytocttimer)
		{
			int ctint;
			if(mytele->mylink.linkunkeytocttimer > 100)
				ctint = 100;
			else
				ctint = mytele->mylink.linkunkeytocttimer;
			ast_safe_sleep(mychannel, ctint);
			rpt_mutex_lock(&myrpt->lock);
			if(mytele->mylink.linkunkeytocttimer < ctint)
				mytele->mylink.linkunkeytocttimer = 0;
			else
				mytele->mylink.linkunkeytocttimer -= ctint;
			rpt_mutex_unlock(&myrpt->lock);
		}
		l = myrpt->links.next;
		unkeys_queued = 0;
                rpt_mutex_lock(&myrpt->lock);
                while (l != &myrpt->links)
                {
                        if (!strcmp(l->name,mytele->mylink.name))
			{
				unkeys_queued = l->lastrx;
				break;
                        }
                        l = l->next;
		}
                rpt_mutex_unlock(&myrpt->lock);
		if( unkeys_queued ){
			imdone = 1;
			break;
		}

		if((ct = (char *) ast_variable_retrieve(myrpt->cfg, nodename, "linkunkeyct"))){ /* Unlinked Courtesy Tone */
			ct_copy = ast_strdup(ct);
			if(ct_copy){
				res = telem_lookup(myrpt,mychannel, myrpt->name, ct_copy);
				ast_free(ct_copy);
			}
			else
				res = -1;
			if(res)
			 	ast_log(LOG_WARNING, "telem_lookup:ctx failed on %s\n", mychannel->name);
		}
		imdone = 1;
		break;
	    case REMDISC:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
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
				if (!strcmp(l->name,mytele->mylink.name))
				{
					haslink = 1;
					break;
				}
				l = l->next;
			}
			rpt_mutex_unlock(&myrpt->lock);
		}
		if (haslink)
		{
			imdone = 1;
			break;
		}
		res = saynode(myrpt,mychannel,mytele->mylink.name);
		if (!res)
		    res = ast_streamfile(mychannel, ((mytele->mylink.hasconnected) ?
			"rpt/remote_disc" : "rpt/remote_busy"), mychannel->language);
		break;
	    case REMALREADY:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/remote_already", mychannel->language);
		break;
	    case REMNOTFOUND:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/remote_notfound", mychannel->language);
		break;
	    case REMGO:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/remote_go", mychannel->language);
		break;
	    case CONNECTED:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM,  mychannel) == -1) break;
		res = saynode(myrpt,mychannel,mytele->mylink.name);
		if (!res)
		    res = ast_streamfile(mychannel, "rpt/connected", mychannel->language);
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		res = ast_streamfile(mychannel, "digits/2", mychannel->language);
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		res = saynode(myrpt,mychannel,myrpt->name);
		imdone = 1;
		break;
	    case CONNFAIL:
		res = saynode(myrpt,mychannel,mytele->mylink.name);
		if (!res)
		    res = ast_streamfile(mychannel, "rpt/connection_failed", mychannel->language);
		break;
	    case MEMNOTFOUND:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/memory_notfound", mychannel->language);
		break;
	    case PLAYBACK:
            case LOCALPLAY:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		res = ast_streamfile(mychannel, mytele->param, mychannel->language);
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		imdone = 1;
		break;
	    case TOPKEY:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		for(i = 0; i < TOPKEYN; i++)
		{
			if (!myrpt->topkey[i].node[0]) continue;
			if ((!myrpt->topkeylong) && (myrpt->topkey[i].keyed)) continue;
			res = saynode(myrpt, mychannel,	myrpt->topkey[i].node);
			if (!res) res = sayfile(mychannel,(myrpt->topkey[i].keyed) ?
				"rpt/keyedfor" : "rpt/unkeyedfor");
			if (!res) res = saynum(mychannel,
				myrpt->topkey[i].timesince);
			if (!res) res = sayfile(mychannel,"rpt/seconds");
			if (!myrpt->topkeylong) break;
		}
		imdone = 1;
		break;
	    case SETREMOTE:
		ast_mutex_lock(&myrpt->remlock);
		res = 0;
		myrpt->remsetting = 1;
		if(!strcmp(myrpt->remoterig, remote_rig_ft897))
		{
			res = set_ft897(myrpt);
		}
		if(!strcmp(myrpt->remoterig, remote_rig_ft100))
		{
			res = set_ft100(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_ft950))
		{
			res = set_ft950(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_tm271))
		{
			setxpmr(myrpt,0);
			res = set_tm271(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_ic706))
		{
			res = set_ic706(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_xcat))
		{
			res = set_xcat(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_rbi)||!strcmp(myrpt->remoterig, remote_rig_ppp16))
		{
			if (ioperm(myrpt->p.iobase,1,1) == -1)
			{
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_WARNING, "Cant get io permission on IO port %x hex\n",myrpt->p.iobase);
				res = -1;
			}
			else res = setrbi(myrpt);
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_kenwood))
		{
			if (myrpt->iofd >= 0) setdtr(myrpt,myrpt->iofd,1);
			res = setkenwood(myrpt);
			if (myrpt->iofd >= 0) setdtr(myrpt,myrpt->iofd,0);
			setxpmr(myrpt,0);
			if (ast_safe_sleep(mychannel,200) == -1)
			{
				myrpt->remsetting = 0;
				ast_mutex_unlock(&myrpt->remlock);
				res = -1;
				break;
			}
			if (myrpt->iofd < 0)
			{
				i = DAHDI_FLUSH_EVENT;
				if (ioctl(myrpt->zaptxchannel->fds[0],DAHDI_FLUSH,&i) == -1)
				{
					myrpt->remsetting = 0;
					ast_mutex_unlock(&myrpt->remlock);
					ast_log(LOG_ERROR,"Cant flush events");
					res = -1;
					break;
				}
				if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_GET_PARAMS,&par) == -1)
				{
					myrpt->remsetting = 0;
					ast_mutex_unlock(&myrpt->remlock);
					ast_log(LOG_ERROR,"Cant get params");
					res = -1;
					break;
				}
				myrpt->remoterx =
					(par.rxisoffhook || (myrpt->tele.next != &myrpt->tele));
			}
		}
		else if(!strcmp(myrpt->remoterig, remote_rig_tmd700))
		{
			res = set_tmd700(myrpt);
			setxpmr(myrpt,0);
		}

		myrpt->remsetting = 0;
		ast_mutex_unlock(&myrpt->remlock);
		if (!res)
		{
			if ((!strcmp(myrpt->remoterig, remote_rig_tm271)) ||
			   (!strcmp(myrpt->remoterig, remote_rig_kenwood)))
				telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
			break;
		}
		/* fall thru to invalid freq */
	    case INVFREQ:
		/* wait a little bit */
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = ast_streamfile(mychannel, "rpt/invalid-freq", mychannel->language);
		break;
	    case REMMODE:
		cp = 0;
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		switch(myrpt->remmode)
		{
		    case REM_MODE_FM:
			saycharstr(mychannel,"FM");
			break;
		    case REM_MODE_USB:
			saycharstr(mychannel,"USB");
			break;
		    case REM_MODE_LSB:
			saycharstr(mychannel,"LSB");
			break;
		    case REM_MODE_AM:
			saycharstr(mychannel,"AM");
			break;
		}
		if (!wait_interval(myrpt, DLY_COMP, mychannel))
			if (!res) res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		break;
	    case LOGINREQ:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		sayfile(mychannel,"rpt/login");
		saycharstr(mychannel,myrpt->name);
		break;
	    case REMLOGIN:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		saycharstr(mychannel,myrpt->loginuser);
		saynode(myrpt,mychannel,myrpt->name);
		wait_interval(myrpt, DLY_COMP, mychannel);
		if (!res) res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		break;
	    case REMXXX:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		res = 0;
		switch(mytele->submode.i)
		{
		    case 100: /* RX PL Off */
			sayfile(mychannel, "rpt/rxpl");
			sayfile(mychannel, "rpt/off");
			break;
		    case 101: /* RX PL On */
			sayfile(mychannel, "rpt/rxpl");
			sayfile(mychannel, "rpt/on");
			break;
		    case 102: /* TX PL Off */
			sayfile(mychannel, "rpt/txpl");
			sayfile(mychannel, "rpt/off");
			break;
		    case 103: /* TX PL On */
			sayfile(mychannel, "rpt/txpl");
			sayfile(mychannel, "rpt/on");
			break;
		    case 104: /* Low Power */
			sayfile(mychannel, "rpt/lopwr");
			break;
		    case 105: /* Medium Power */
			sayfile(mychannel, "rpt/medpwr");
			break;
		    case 106: /* Hi Power */
			sayfile(mychannel, "rpt/hipwr");
			break;
		    case 113: /* Scan down slow */
			sayfile(mychannel,"rpt/down");
			sayfile(mychannel, "rpt/slow");
			break;
		    case 114: /* Scan down quick */
			sayfile(mychannel,"rpt/down");
			sayfile(mychannel, "rpt/quick");
			break;
		    case 115: /* Scan down fast */
			sayfile(mychannel,"rpt/down");
			sayfile(mychannel, "rpt/fast");
			break;
		    case 116: /* Scan up slow */
			sayfile(mychannel,"rpt/up");
			sayfile(mychannel, "rpt/slow");
			break;
		    case 117: /* Scan up quick */
			sayfile(mychannel,"rpt/up");
			sayfile(mychannel, "rpt/quick");
			break;
		    case 118: /* Scan up fast */
			sayfile(mychannel,"rpt/up");
			sayfile(mychannel, "rpt/fast");
			break;
		    default:
			res = -1;
		}
		if (strcmp(myrpt->remoterig, remote_rig_tm271) &&
		   strcmp(myrpt->remoterig, remote_rig_kenwood))
		{
			if (!wait_interval(myrpt, DLY_COMP, mychannel))
				if (!res) res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		}
		break;
	    case SCAN:
		ast_mutex_lock(&myrpt->remlock);
		if (myrpt->hfscanstop)
		{
			myrpt->hfscanstatus = 0;
			myrpt->hfscanmode = 0;
			myrpt->hfscanstop = 0;
			mytele->mode = SCANSTAT;
			ast_mutex_unlock(&myrpt->remlock);
			if (ast_safe_sleep(mychannel,1000) == -1) break;
			sayfile(mychannel, "rpt/stop");
			imdone = 1;
			break;
		}
		if (myrpt->hfscanstatus > -2) service_scan(myrpt);
		i = myrpt->hfscanstatus;
		myrpt->hfscanstatus = 0;
		if (i) mytele->mode = SCANSTAT;
		ast_mutex_unlock(&myrpt->remlock);
		if (i < 0) sayfile(mychannel, "rpt/stop");
		else if (i > 0) saynum(mychannel,i);
		imdone = 1;
		break;
	    case TUNE:
		ast_mutex_lock(&myrpt->remlock);
		if (!strcmp(myrpt->remoterig,remote_rig_ic706))
		{
			set_mode_ic706(myrpt, REM_MODE_AM);
			if(play_tone(mychannel, 800, 6000, 8192) == -1) break;
			ast_safe_sleep(mychannel,500);
			set_mode_ic706(myrpt, myrpt->remmode);
			myrpt->tunerequest = 0;
			ast_mutex_unlock(&myrpt->remlock);
			imdone = 1;
			break;
		}
		if (!strcmp(myrpt->remoterig,remote_rig_ft100))
		{
			set_mode_ft100(myrpt, REM_MODE_AM);
			simple_command_ft100(myrpt, 0x0f, 1);
			if(play_tone(mychannel, 800, 6000, 8192) == -1) break;
			simple_command_ft100(myrpt, 0x0f, 0);
			ast_safe_sleep(mychannel,500);
			set_mode_ft100(myrpt, myrpt->remmode);
			myrpt->tunerequest = 0;
			ast_mutex_unlock(&myrpt->remlock);
			imdone = 1;
			break;
		}
		ast_safe_sleep(mychannel,500);
		set_mode_ft897(myrpt, REM_MODE_AM);
		ast_safe_sleep(mychannel,500);
		myrpt->tunetx = 1;
		if (play_tone(mychannel, 800, 6000, 8192) == -1) break;
		myrpt->tunetx = 0;
		ast_safe_sleep(mychannel,500);
		set_mode_ft897(myrpt, myrpt->remmode);
		ast_playtones_stop(mychannel);
		myrpt->tunerequest = 0;
		ast_mutex_unlock(&myrpt->remlock);
		imdone = 1;
		break;
#if 0
		set_mode_ft897(myrpt, REM_MODE_AM);
		simple_command_ft897(myrpt, 8);
		if(play_tone(mychannel, 800, 6000, 8192) == -1) break;
		simple_command_ft897(myrpt, 0x88);
		ast_safe_sleep(mychannel,500);
		set_mode_ft897(myrpt, myrpt->remmode);
		myrpt->tunerequest = 0;
		ast_mutex_unlock(&myrpt->remlock);
		imdone = 1;
		break;
#endif
	    case REMSHORTSTATUS:
	    case REMLONGSTATUS:
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		res = saynode(myrpt,mychannel,myrpt->name);
		if(!res)
			res = sayfile(mychannel,"rpt/frequency");
		if(!res)
			res = split_freq(mhz, decimals, myrpt->freq);
		if (!multimode_capable(myrpt))
		{
			if (decimals[4] == '0')
			{
				decimals[4] = 0;
				if (decimals[3] == '0') decimals[3] = 0;
			}
			decimals[5] = 0;
		}
		if(!res){
			m = atoi(mhz);
			if(m < 100)
				res = saynum(mychannel, m);
			else
				res = saycharstr(mychannel, mhz);
		}
		if(!res)
			res = sayfile(mychannel, "letters/dot");
		if(!res)
			res = saycharstr(mychannel, decimals);

		if(res)	break;
		if(myrpt->remmode == REM_MODE_FM){ /* Mode FM? */
			switch(myrpt->offset){

				case REM_MINUS:
					res = sayfile(mychannel,"rpt/minus");
					break;

				case REM_SIMPLEX:
					res = sayfile(mychannel,"rpt/simplex");
					break;

				case REM_PLUS:
					res = sayfile(mychannel,"rpt/plus");
					break;

				default:
					break;
			}
		}
		else{ /* Must be USB, LSB, or AM */
			switch(myrpt->remmode){

				case REM_MODE_USB:
					res = saycharstr(mychannel, "USB");
					break;

				case REM_MODE_LSB:
					res = saycharstr(mychannel, "LSB");
					break;

				case REM_MODE_AM:
					res = saycharstr(mychannel, "AM");
					break;


				default:
					break;
			}
		}

		if (res == -1) break;

		if(mytele->mode == REMSHORTSTATUS){ /* Short status? */
			if (!wait_interval(myrpt, DLY_COMP, mychannel))
				if (!res) res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
			break;
		}

		if (strcmp(myrpt->remoterig,remote_rig_ic706))
		{
			switch(myrpt->powerlevel){

				case REM_LOWPWR:
					res = sayfile(mychannel,"rpt/lopwr") ;
					break;
				case REM_MEDPWR:
					res = sayfile(mychannel,"rpt/medpwr");
					break;
				case REM_HIPWR:
					res = sayfile(mychannel,"rpt/hipwr");
					break;
				}
		}

		rbimode = ((!strncmp(myrpt->remoterig,remote_rig_rbi,3))
		  || (!strncmp(myrpt->remoterig,remote_rig_ft100,3))
		  || (!strncmp(myrpt->remoterig,remote_rig_ic706,3)));
		if (res || (sayfile(mychannel,"rpt/rxpl") == -1)) break;
		if (rbimode && (sayfile(mychannel,"rpt/txpl") == -1)) break;
		if ((sayfile(mychannel,"rpt/frequency") == -1) ||
			(saycharstr(mychannel,myrpt->rxpl) == -1)) break;
		if ((!rbimode) && ((sayfile(mychannel,"rpt/txpl") == -1) ||
			(sayfile(mychannel,"rpt/frequency") == -1) ||
			(saycharstr(mychannel,myrpt->txpl) == -1))) break;
		if(myrpt->remmode == REM_MODE_FM){ /* Mode FM? */
			if ((sayfile(mychannel,"rpt/rxpl") == -1) ||
				(sayfile(mychannel,((myrpt->rxplon) ? "rpt/on" : "rpt/off")) == -1) ||
				(sayfile(mychannel,"rpt/txpl") == -1) ||
				(sayfile(mychannel,((myrpt->txplon) ? "rpt/on" : "rpt/off")) == -1))
				{
					break;
				}
		}
		if (!wait_interval(myrpt, DLY_COMP, mychannel))
			if (!res) res = telem_lookup(myrpt,mychannel, myrpt->name, "functcomplete");
		break;
	    case STATUS:
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		hastx = 0;
		linkbase.next = &linkbase;
		linkbase.prev = &linkbase;
		rpt_mutex_lock(&myrpt->lock);
		/* make our own list of links */
		l = myrpt->links.next;
		while(l != &myrpt->links)
		{
			if (l->name[0] == '0')
			{
				l = l->next;
				continue;
			}
			l1 = ast_malloc(sizeof(struct rpt_link));
			if (!l1)
			{
				ast_log(LOG_WARNING, "Cannot alloc memory on %s\n", mychannel->name);
				remque((struct qelem *)mytele);
				myrpt->active_telem = NULL;
				rpt_mutex_unlock(&myrpt->lock);
				ast_log(LOG_NOTICE,"Telemetry thread aborted at line %d, mode: %d\n",__LINE__, mytele->mode); /*@@@@@@@@@@@*/
				ast_free(nodename);
				if(id_malloc)
					ast_free(ident);
				ast_free(mytele);
				ast_hangup(mychannel);
				pthread_exit(NULL);
			}
			memcpy(l1,l,sizeof(struct rpt_link));
			l1->next = l1->prev = NULL;
			insque((struct qelem *)l1,(struct qelem *)linkbase.next);
			l = l->next;
		}
		rpt_mutex_unlock(&myrpt->lock);
		res = saynode(myrpt,mychannel,myrpt->name);
		if (myrpt->callmode)
		{
			hastx = 1;
			res = ast_streamfile(mychannel, "rpt/autopatch_on", mychannel->language);
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		l = linkbase.next;
		while(l != &linkbase)
		{
			char *s;

			hastx = 1;
			res = saynode(myrpt,mychannel,l->name);
			s = "rpt/tranceive";
			if (!l->mode) s = "rpt/monitor";
			if (l->mode > 1) s = "rpt/localmonitor";
			if (!l->thisconnected) s = "rpt/connecting";
			res = ast_streamfile(mychannel, s, mychannel->language);
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
			l = l->next;
		}
		if (!hastx)
		{
			res = ast_streamfile(mychannel, "rpt/repeat_only", mychannel->language);
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		/* destroy our local link queue */
		l = linkbase.next;
		while(l != &linkbase)
		{
			l1 = l;
			l = l->next;
			remque((struct qelem *)l1);
			ast_free(l1);
		}
		imdone = 1;
		break;
	    case LASTUSER:
		if (myrpt->curdtmfuser[0])
		{
			sayphoneticstr(mychannel,myrpt->curdtmfuser);
		}
		if (myrpt->lastdtmfuser[0] &&
			strcmp(myrpt->lastdtmfuser,myrpt->curdtmfuser))
		{
			if (myrpt->curdtmfuser[0])
				sayfile(mychannel,"and");
			sayphoneticstr(mychannel,myrpt->lastdtmfuser);
		}
		imdone = 1;
		break;
	    case FULLSTATUS:
		rpt_mutex_lock(&myrpt->lock);
		/* get all the nodes */
		__mklinklist(myrpt,NULL,lbuf,0);
		rpt_mutex_unlock(&myrpt->lock);
		/* parse em */
		ns = finddelim(lbuf,strs,MAXLINKLIST);
		/* sort em */
		if (ns) qsort((void *)strs,ns,sizeof(char *),mycompar);
		/* wait a little bit */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		hastx = 0;
		res = saynode(myrpt,mychannel,myrpt->name);
		if (myrpt->callmode)
		{
			hastx = 1;
			res = ast_streamfile(mychannel, "rpt/autopatch_on", mychannel->language);
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		/* go thru all the nodes in list */
		for(i = 0; i < ns; i++)
		{
			char *s,mode = 'T';

			/* if a mode spec at first, handle it */
			if ((*strs[i] < '0') || (*strs[i] > '9'))
			{
				mode = *strs[i];
				strs[i]++;
			}

			hastx = 1;
			res = saynode(myrpt,mychannel,strs[i]);
			s = "rpt/tranceive";
			if (mode == 'R') s = "rpt/monitor";
			if (mode == 'C') s = "rpt/connecting";
			res = ast_streamfile(mychannel, s, mychannel->language);
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		if (!hastx)
		{
			res = ast_streamfile(mychannel, "rpt/repeat_only", mychannel->language);
			if (!res)
				res = ast_waitstream(mychannel, "");
			else
				 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			ast_stopstream(mychannel);
		}
		imdone = 1;
		break;

	    case LASTNODEKEY: /* Identify last node which keyed us up */
		rpt_mutex_lock(&myrpt->lock);
		if(myrpt->lastnodewhichkeyedusup){
			p = ast_strdup(myrpt->lastnodewhichkeyedusup); /* Make a local copy of the node name */
			if(!p){
				ast_log(LOG_WARNING, "ast_strdup failed in telemetery LASTNODEKEY");
				imdone = 1;
				break;
			}
		}
		else
			p = NULL;
		rpt_mutex_unlock(&myrpt->lock);
		if(!p){
			imdone = 1; /* no node previously keyed us up, or the node which did has been disconnected */
			break;
		}
		if (!wait_interval(myrpt, DLY_TELEM, mychannel))
			res = saynode(myrpt,mychannel,p);
		ast_free(p);
		imdone = 1;
		break;

	    case UNAUTHTX: /* Say unauthorized transmit frequency */
		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		res = ast_streamfile(mychannel, "rpt/unauthtx", mychannel->language);
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		imdone = 1;
		break;

	    case PARROT: /* Repeat stuff */

		sprintf(mystr,PARROTFILE,myrpt->name,mytele->parrot);
		if (ast_fileexists(mystr,NULL,mychannel->language) <= 0)
		{
			imdone = 1;
			myrpt->parrotstate = 0;
			break;
		}
		if (wait_interval(myrpt, DLY_PARROT, mychannel) == -1) break;
		sprintf(mystr,PARROTFILE,myrpt->name,mytele->parrot);
		res = ast_streamfile(mychannel, mystr, mychannel->language);
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		sprintf(mystr,PARROTFILE,myrpt->name,mytele->parrot);
		strcat(mystr,".wav");
		unlink(mystr);
		imdone = 1;
		myrpt->parrotstate = 0;
		myrpt->parrotonce = 0;
		break;

	    case TIMEOUT:
		res = saynode(myrpt,mychannel,myrpt->name);
		if (!res)
		   res = ast_streamfile(mychannel, "rpt/timeout", mychannel->language);
		break;

	    case TIMEOUT_WARNING:
		time(&t);
		res = saynode(myrpt,mychannel,myrpt->name);
		if (!res)
		   res = ast_streamfile(mychannel, "rpt/timeout-warning", mychannel->language);
		if (!res)
			res = ast_waitstream(mychannel, "");
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		ast_stopstream(mychannel);
		if(!res) /* Say number of seconds */
			ast_say_number(mychannel, myrpt->p.remotetimeout -
			    (t - myrpt->last_activity_time),
				"", mychannel->language, (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		res = ast_streamfile(mychannel, "queue-seconds", mychannel->language);
		break;

	    case ACT_TIMEOUT_WARNING:
		time(&t);
		res = saynode(myrpt,mychannel,myrpt->name);
		if (!res)
		    res = ast_streamfile(mychannel, "rpt/act-timeout-warning", mychannel->language);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if(!res) /* Say number of seconds */
			ast_say_number(mychannel, myrpt->p.remoteinacttimeout -
			    (t - myrpt->last_activity_time),
				"", mychannel->language, (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (!res)
			res = ast_streamfile(mychannel, "queue-seconds", mychannel->language);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		imdone = 1;
		break;

	    case STATS_TIME:
            case STATS_TIME_LOCAL:
	    	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		t = time(NULL);
		rpt_localtime(&t, &localtm, myrpt->p.timezone);
		t1 = rpt_mktime(&localtm,NULL);
		/* Say the phase of the day is before the time */
		if((localtm.tm_hour >= 0) && (localtm.tm_hour < 12))
			p = "rpt/goodmorning";
		else if((localtm.tm_hour >= 12) && (localtm.tm_hour < 18))
			p = "rpt/goodafternoon";
		else
			p = "rpt/goodevening";
		if (sayfile(mychannel,p) == -1)
		{
			imdone = 1;
			break;
		}
		/* Say the time is ... */
		if (sayfile(mychannel,"rpt/thetimeis") == -1)
		{
			imdone = 1;
			break;
		}
		/* Say the time */
	    	res = ast_say_time(mychannel, t1, "", mychannel->language);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		imdone = 1;
	    	break;
	    case STATS_VERSION:
		p = strstr(tdesc, "version");
		if(!p)
			break;
		if(sscanf(p, "version %d.%d", &vmajor, &vminor) != 2)
			break;
    		if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		/* Say "version" */
		if (sayfile(mychannel,"rpt/version") == -1)
		{
			imdone = 1;
			break;
		}
		if(!res) /* Say "X" */
			ast_say_number(mychannel, vmajor, "", mychannel->language, (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (saycharstr(mychannel,".") == -1)
		{
			imdone = 1;
			break;
		}
		if(!res) /* Say "Y" */
			ast_say_number(mychannel, vminor, "", mychannel->language, (char *) NULL);
		if (!res){
			res = ast_waitstream(mychannel, "");
			ast_stopstream(mychannel);
		}
		else
			 ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
		imdone = 1;
	    	break;
	    case STATS_GPS:
	    case STATS_GPS_LEGACY:
		fp = fopen(GPSFILE,"r");
		if (!fp) break;
		if (fstat(fileno(fp),&mystat) == -1) break;
		if (mystat.st_size >= 100) break;
		elev[0] = 0;
		if (fscanf(fp,"%u %s %s %s",&u,lat,lon,elev) < 3) break;
		fclose(fp);
		was = (time_t) u;
		time(&t);
		if ((was + GPS_VALID_SECS) < t) break;
	    	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
		if (saynode(myrpt,mychannel,myrpt->name) == -1) break;
		if (sayfile(mychannel,"location") == -1) break;
		c = lat[strlen(lat) - 1];
		lat[strlen(lat) - 1] = 0;
		if (sscanf(lat,"%2d%d.%d",&i,&j,&k) != 3) break;
		res = ast_say_number(mychannel, i, "", mychannel->language, (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (sayfile(mychannel,"degrees") == -1) break;
		res = ast_say_number(mychannel, j, "", mychannel->language, (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (saycharstr(mychannel,lat + 4) == -1) break;
		if (sayfile(mychannel,"minutes") == -1) break;
		if (sayfile(mychannel,(c == 'N') ? "north" : "south") == -1) break;
		if (sayfile(mychannel,"rpt/latitude") == -1) break;
		c = lon[strlen(lon) - 1];
		lon[strlen(lon) - 1] = 0;
		if (sscanf(lon,"%3d%d.%d",&i,&j,&k) != 3) break;
		res = ast_say_number(mychannel, i, "", mychannel->language, (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (sayfile(mychannel,"degrees") == -1) break;
		res = ast_say_number(mychannel, j, "", mychannel->language, (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (saycharstr(mychannel,lon + 5) == -1) break;
		if (sayfile(mychannel,"minutes") == -1) break;
		if (sayfile(mychannel,(c == 'E') ? "east" : "west") == -1) break;
		if (sayfile(mychannel,"rpt/longitude") == -1) break;
		if (!elev[0]) break;
		c = elev[strlen(elev) - 1];
		elev[strlen(elev) - 1] = 0;
		if (sscanf(elev,"%f",&f) != 1) break;
		if (myrpt->p.gpsfeet)
		{
			if (c == 'M') f *= 3.2808399;
		}
		else
		{
			if (c != 'M') f /= 3.2808399;
		}
		sprintf(mystr,"%0.1f",f);
		if (sscanf(mystr,"%d.%d",&i,&j) != 2) break;
		res = ast_say_number(mychannel, i, "", mychannel->language, (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (saycharstr(mychannel,".") == -1) break;
		res = ast_say_number(mychannel, j, "", mychannel->language, (char *) NULL);
		if (!res)
			res = ast_waitstream(mychannel, "");
		ast_stopstream(mychannel);
		if (sayfile(mychannel,(myrpt->p.gpsfeet) ? "feet" : "meters") == -1) break;
		if (saycharstr(mychannel,"AMSL") == -1) break;
		ast_stopstream(mychannel);
		imdone = 1;
		break;
	    case ARB_ALPHA:
	    	if (!wait_interval(myrpt, DLY_TELEM, mychannel))
		    	if(mytele->param)
		    		saycharstr(mychannel, mytele->param);
	    	imdone = 1;
		break;
	    case REV_PATCH:
	    	if (wait_interval(myrpt, DLY_TELEM, mychannel) == -1) break;
	    	if(mytele->param) {

			/* Parts of this section taken from app_parkandannounce */
			char *tpl_working, *tpl_current;
			char *tmp[100], *myparm;
			int looptemp=0,i=0, dres = 0;


			tpl_working = ast_strdup(mytele->param);
			myparm = strsep(&tpl_working,",");
			tpl_current=strsep(&tpl_working, ":");

			while(tpl_current && looptemp < sizeof(tmp)) {
				tmp[looptemp]=tpl_current;
				looptemp++;
				tpl_current=strsep(&tpl_working,":");
			}

			for(i=0; i<looptemp; i++) {
				if(!strcmp(tmp[i], "PARKED")) {
					ast_say_digits(mychannel, atoi(myparm), "", mychannel->language);
				} else if(!strcmp(tmp[i], "NODE")) {
					ast_say_digits(mychannel, atoi(myrpt->name), "", mychannel->language);
				} else {
					dres = ast_streamfile(mychannel, tmp[i], mychannel->language);
					if(!dres) {
						dres = ast_waitstream(mychannel, "");
					} else {
						ast_log(LOG_WARNING, "ast_streamfile of %s failed on %s\n", tmp[i], mychannel->name);
						dres = 0;
					}
				}
			}
			ast_free(tpl_working);
		}
	    	imdone = 1;
		break;
	    case TEST_TONE:
		imdone = 1;
		if (myrpt->stopgen) break;
		myrpt->stopgen = -1;
	        if ((res = ast_tonepair_start(mychannel, 1000.0, 0, 99999999, 7200.0)))
		{
			myrpt->stopgen = 0;
			break;
		}
	        while(mychannel->generatordata && (myrpt->stopgen <= 0)) {
			if (ast_safe_sleep(mychannel,1)) break;
		    	imdone = 1;
			}
		myrpt->stopgen = 0;
		if (myrpt->remote && (myrpt->remstopgen < 0)) myrpt->remstopgen = 1;
		break;
	    case PFXTONE:
		res = telem_lookup(myrpt,mychannel, myrpt->name, "pfxtone");
		break;
	    default:
	    	break;
	}
	if (!imdone)
	{
		if (!res)
			res = ast_waitstream(mychannel, "");
		else {
			ast_log(LOG_WARNING, "ast_streamfile failed on %s\n", mychannel->name);
			res = 0;
		}
	}
	ast_stopstream(mychannel);
	rpt_mutex_lock(&myrpt->lock);
	if (mytele->mode == TAILMSG)
	{
		if (!res)
		{
			myrpt->tailmessagen++;
			if(myrpt->tailmessagen >= myrpt->p.tailmessagemax) myrpt->tailmessagen = 0;
		}
		else
		{
			myrpt->tmsgtimer = myrpt->p.tailsquashedtime;
		}
	}
	remque((struct qelem *)mytele);
	myrpt->active_telem = NULL;
	rpt_mutex_unlock(&myrpt->lock);
	ast_free(nodename);
	if(id_malloc)
		ast_free(ident);
	ast_free(mytele);
	ast_hangup(mychannel);
#ifdef  APP_RPT_LOCK_DEBUG
	{
		struct lockthread *t;

		sleep(5);
		ast_mutex_lock(&locklock);
		t = get_lockthread(pthread_self());
		if (t) memset(t,0,sizeof(struct lockthread));
		ast_mutex_unlock(&locklock);
	}
#endif
	pthread_exit(NULL);
}


