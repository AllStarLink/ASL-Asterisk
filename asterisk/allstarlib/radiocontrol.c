


#include "allstar/allstarutils.h"


#define	KENWOOD_RETRIES 5

#define FT897_SERIAL_DELAY 75000		/* # of usec to wait between some serial commands on FT-897 */
#define FT100_SERIAL_DELAY 75000		/* # of usec to wait between some serial commands on FT-897 */


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


enum {HF_SCAN_OFF,HF_SCAN_DOWN_SLOW,HF_SCAN_DOWN_QUICK,
      HF_SCAN_DOWN_FAST,HF_SCAN_UP_SLOW,HF_SCAN_UP_QUICK,HF_SCAN_UP_FAST};



/*
* Return 1 if rig is narrow capable
*/

static int narrow_capable(struct rpt *myrpt)
{
	if(!strcmp(myrpt->remoterig, remote_rig_kenwood))
		return 1;
	if(!strcmp(myrpt->remoterig, remote_rig_tmd700))
		return 1;
	if(!strcmp(myrpt->remoterig, remote_rig_tm271))
		return 1;
	return 0;
}

/* Doug Hall RBI-1 serial data definitions:
 *
 * Byte 0: Expansion external outputs
 * Byte 1:
 *	Bits 0-3 are BAND as follows:
 *	Bits 4-5 are POWER bits as follows:
 *		00 - Low Power
 *		01 - Hi Power
 *		02 - Med Power
 *	Bits 6-7 are always set
 * Byte 2:
 *	Bits 0-3 MHZ in BCD format
 *	Bits 4-5 are offset as follows:
 *		00 - minus
 *		01 - plus
 *		02 - simplex
 *		03 - minus minus (whatever that is)
 *	Bit 6 is the 0/5 KHZ bit
 *	Bit 7 is always set
 * Byte 3:
 *	Bits 0-3 are 10 KHZ in BCD format
 *	Bits 4-7 are 100 KHZ in BCD format
 * Byte 4: PL Tone code and encode/decode enable bits
 *	Bits 0-5 are PL tone code (comspec binary codes)
 *	Bit 6 is encode enable/disable
 *	Bit 7 is decode enable/disable
 */

/* take the frequency from the 10 mhz digits (and up) and convert it
   to a band number */

static int rbi_mhztoband(char *str)
{
int	i;

	i = atoi(str) / 10; /* get the 10's of mhz */
	switch(i)
	{
	    case 2:
		return 10;
	    case 5:
		return 11;
	    case 14:
		return 2;
	    case 22:
		return 3;
	    case 44:
		return 4;
	    case 124:
		return 0;
	    case 125:
		return 1;
	    case 126:
		return 8;
	    case 127:
		return 5;
	    case 128:
		return 6;
	    case 129:
		return 7;
	    default:
		break;
	}
	return -1;
}



/* take a PL frequency and turn it into a code */
static int rbi_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 0;
	    case 719:
		return 1;
	    case 744:
		return 2;
	    case 770:
		return 3;
	    case 797:
		return 4;
	    case 825:
		return 5;
	    case 854:
		return 6;
	    case 885:
		return 7;
	    case 915:
		return 8;
	    case 948:
		return 9;
	    case 974:
		return 10;
	    case 1000:
		return 11;
	    case 1035:
		return 12;
	    case 1072:
		return 13;
	    case 1109:
		return 14;
	    case 1148:
		return 15;
	    case 1188:
		return 16;
	    case 1230:
		return 17;
	    case 1273:
		return 18;
	    case 1318:
		return 19;
	    case 1365:
		return 20;
	    case 1413:
		return 21;
	    case 1462:
		return 22;
	    case 1514:
		return 23;
	    case 1567:
		return 24;
	    case 1622:
		return 25;
	    case 1679:
		return 26;
	    case 1738:
		return 27;
	    case 1799:
		return 28;
	    case 1862:
		return 29;
	    case 1928:
		return 30;
	    case 2035:
		return 31;
	    case 2107:
		return 32;
	    case 2181:
		return 33;
	    case 2257:
		return 34;
	    case 2336:
		return 35;
	    case 2418:
		return 36;
	    case 2503:
		return 37;
	}
	return -1;
}

/*
* Shift out a formatted serial bit stream
*/

static void rbi_out_parallel(struct rpt *myrpt,unsigned char *data)
    {
#ifdef __i386__
    int i,j;
    unsigned char od,d;
    static volatile long long delayvar;

    for(i = 0 ; i < 5 ; i++){
        od = *data++;
        for(j = 0 ; j < 8 ; j++){
            d = od & 1;
            outb(d,myrpt->p.iobase);
	    /* >= 15 us */
	    for(delayvar = 1; delayvar < 15000; delayvar++);
            od >>= 1;
            outb(d | 2,myrpt->p.iobase);
	    /* >= 30 us */
	    for(delayvar = 1; delayvar < 30000; delayvar++);
            outb(d,myrpt->p.iobase);
	    /* >= 10 us */
	    for(delayvar = 1; delayvar < 10000; delayvar++);
            }
        }
	/* >= 50 us */
        for(delayvar = 1; delayvar < 50000; delayvar++);
#endif
    }




static void rbi_out(struct rpt *myrpt,unsigned char *data)
{
struct dahdi_radio_param r;

	memset(&r,0,sizeof(struct dahdi_radio_param));
	r.radpar = DAHDI_RADPAR_REMMODE;
	r.data = DAHDI_RADPAR_REM_RBI1;
	/* if setparam ioctl fails, its probably not a pciradio card */
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&r) == -1)
	{
		rbi_out_parallel(myrpt,data);
		return;
	}
	r.radpar = DAHDI_RADPAR_REMCOMMAND;
	memcpy(&r.data,data,5);
	if (ioctl(myrpt->zaprxchannel->fds[0],DAHDI_RADIO_SETPARAM,&r) == -1)
	{
		ast_log(LOG_WARNING,"Cannot send RBI command for channel %s\n",myrpt->zaprxchannel->name);
		return;
	}
}

int setrbi(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",*s;
unsigned char rbicmd[5];
int	band,txoffset = 0,txpower = 0,rxpl;

	/* must be a remote system */
	if (!myrpt->remoterig) return(0);
	if (!myrpt->remoterig[0]) return(0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig,remote_rig_rbi,3)) return(0);
	if (setrbi_check(myrpt) == -1) return(-1);
	strncpy(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp,'.');
	/* if no decimal, is invalid */

	if (s == NULL){
		if(debug)
			printf("@@@@ Frequency needs a decimal\n");
		return -1;
	}

	*s++ = 0;
	if (strlen(tmp) < 2){
		if(debug)
			printf("@@@@ Bad MHz digits: %s\n", tmp);
	 	return -1;
	}

	if (strlen(s) < 3){
		if(debug)
			printf("@@@@ Bad KHz digits: %s\n", s);
	 	return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')){
		if(debug)
			printf("@@@@ KHz must end in 0 or 5: %c\n", s[2]);
	 	return -1;
	}

	band = rbi_mhztoband(tmp);
	if (band == -1){
		if(debug)
			printf("@@@@ Bad Band: %s\n", tmp);
	 	return -1;
	}

	rxpl = rbi_pltocode(myrpt->rxpl);

	if (rxpl == -1){
		if(debug)
			printf("@@@@ Bad TX PL: %s\n", myrpt->rxpl);
	 	return -1;
	}


	switch(myrpt->offset)
	{
	    case REM_MINUS:
		txoffset = 0;
		break;
	    case REM_PLUS:
		txoffset = 0x10;
		break;
	    case REM_SIMPLEX:
		txoffset = 0x20;
		break;
	}
	switch(myrpt->powerlevel)
	{
	    case REM_LOWPWR:
		txpower = 0;
		break;
	    case REM_MEDPWR:
		txpower = 0x20;
		break;
	    case REM_HIPWR:
		txpower = 0x10;
		break;
	}
	rbicmd[0] = 0;
	rbicmd[1] = band | txpower | 0xc0;
	rbicmd[2] = (*(s - 2) - '0') | txoffset | 0x80;
	if (s[2] == '5') rbicmd[2] |= 0x40;
	rbicmd[3] = ((*s - '0') << 4) + (s[1] - '0');
	rbicmd[4] = rxpl;
	if (myrpt->txplon) rbicmd[4] |= 0x40;
	if (myrpt->rxplon) rbicmd[4] |= 0x80;
	rbi_out(myrpt,rbicmd);
	return 0;
}

int setrtx(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",*s,rigstr[200],pwr,res = 0;
int	band,txoffset = 0,txpower = 0,rxpl,txpl,mysplit;
float ofac;
double txfreq;

	/* must be a remote system */
	if (!myrpt->remoterig) return(0);
	if (!myrpt->remoterig[0]) return(0);
	/* must have rtx hardware */
	if (!ISRIG_RTX(myrpt->remoterig)) return(0);
	/* must be a usbradio interface type */
	if (!IS_XPMR(myrpt)) return(0);
	strncpy(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp,'.');
	/* if no decimal, is invalid */

	if(debug)printf("setrtx() %s %s\n",myrpt->name,myrpt->remoterig);

	if (s == NULL){
		if(debug)
			printf("@@@@ Frequency needs a decimal\n");
		return -1;
	}
	*s++ = 0;
	if (strlen(tmp) < 2){
		if(debug)
			printf("@@@@ Bad MHz digits: %s\n", tmp);
	 	return -1;
	}

	if (strlen(s) < 3){
		if(debug)
			printf("@@@@ Bad KHz digits: %s\n", s);
	 	return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')){
		if(debug)
			printf("@@@@ KHz must end in 0 or 5: %c\n", s[2]);
	 	return -1;
	}

	band = rbi_mhztoband(tmp);
	if (band == -1){
		if(debug)
			printf("@@@@ Bad Band: %s\n", tmp);
	 	return -1;
	}

	rxpl = rbi_pltocode(myrpt->rxpl);

	if (rxpl == -1){
		if(debug)
			printf("@@@@ Bad RX PL: %s\n", myrpt->rxpl);
	 	return -1;
	}

	txpl = rbi_pltocode(myrpt->txpl);

	if (txpl == -1){
		if(debug)
			printf("@@@@ Bad TX PL: %s\n", myrpt->txpl);
	 	return -1;
	}

	switch(myrpt->offset)
	{
	    case REM_MINUS:
		txoffset = 0;
		break;
	    case REM_PLUS:
		txoffset = 0x10;
		break;
	    case REM_SIMPLEX:
		txoffset = 0x20;
		break;
	}
	switch(myrpt->powerlevel)
	{
	    case REM_LOWPWR:
		txpower = 0;
		break;
	    case REM_MEDPWR:
		txpower = 0x20;
		break;
	    case REM_HIPWR:
		txpower = 0x10;
		break;
	}

	res = setrtx_check(myrpt);
	if (res < 0) return res;
	mysplit = myrpt->splitkhz;
	if (!mysplit)
	{
		if (!strcmp(myrpt->remoterig,remote_rig_rtx450))
			mysplit = myrpt->p.default_split_70cm;
		else
			mysplit = myrpt->p.default_split_2m;
	}
	if (myrpt->offset != REM_SIMPLEX)
		ofac = ((float) mysplit) / 1000.0;
	else ofac = 0.0;
	if (myrpt->offset == REM_MINUS) ofac = -ofac;

	txfreq = atof(myrpt->freq) +  ofac;
	pwr = 'L';
	if (myrpt->powerlevel == REM_HIPWR) pwr = 'H';
	if (!res)
	{
		sprintf(rigstr,"SETFREQ %s %f %s %s %c",myrpt->freq,txfreq,
			(myrpt->rxplon) ? myrpt->rxpl : "0.0",
			(myrpt->txplon) ? myrpt->txpl : "0.0",pwr);
		send_usb_txt(myrpt,rigstr);
		rpt_telemetry(myrpt,COMPLETE,NULL);
		res = 0;
	}
	return 0;
}

int setxpmr(struct rpt *myrpt, int dotx)
{
	char rigstr[200];
	int rxpl,txpl;

	/* must be a remote system */
	if (!myrpt->remoterig) return(0);
	if (!myrpt->remoterig[0]) return(0);
	/* must not have rtx hardware */
	if (ISRIG_RTX(myrpt->remoterig)) return(0);
	/* must be a usbradio interface type */
	if (!IS_XPMR(myrpt)) return(0);

	if(debug)printf("setxpmr() %s %s\n",myrpt->name,myrpt->remoterig );

	rxpl = rbi_pltocode(myrpt->rxpl);

	if (rxpl == -1){
		if(debug)
			printf("@@@@ Bad RX PL: %s\n", myrpt->rxpl);
	 	return -1;
	}

	if (dotx)
	{
		txpl = rbi_pltocode(myrpt->txpl);
		if (txpl == -1){
			if(debug)
				printf("@@@@ Bad TX PL: %s\n", myrpt->txpl);
		 	return -1;
		}
		sprintf(rigstr,"SETFREQ 0.0 0.0 %s %s L",
			(myrpt->rxplon) ? myrpt->rxpl : "0.0",
			(myrpt->txplon) ? myrpt->txpl : "0.0");
	}
	else
	{
		sprintf(rigstr,"SETFREQ 0.0 0.0 %s 0.0 L",
			(myrpt->rxplon) ? myrpt->rxpl : "0.0");

	}
	send_usb_txt(myrpt,rigstr);
	return 0;
}


int setrbi_check(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",*s;
int	band,txpl;

	/* must be a remote system */
	if (!myrpt->remote) return(0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig,remote_rig_rbi,3)) return(0);
	strncpy(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp,'.');
	/* if no decimal, is invalid */

	if (s == NULL){
		if(debug)
			printf("@@@@ Frequency needs a decimal\n");
		return -1;
	}

	*s++ = 0;
	if (strlen(tmp) < 2){
		if(debug)
			printf("@@@@ Bad MHz digits: %s\n", tmp);
	 	return -1;
	}

	if (strlen(s) < 3){
		if(debug)
			printf("@@@@ Bad KHz digits: %s\n", s);
	 	return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')){
		if(debug)
			printf("@@@@ KHz must end in 0 or 5: %c\n", s[2]);
	 	return -1;
	}

	band = rbi_mhztoband(tmp);
	if (band == -1){
		if(debug)
			printf("@@@@ Bad Band: %s\n", tmp);
	 	return -1;
	}

	txpl = rbi_pltocode(myrpt->txpl);

	if (txpl == -1){
		if(debug)
			printf("@@@@ Bad TX PL: %s\n", myrpt->txpl);
	 	return -1;
	}
	return 0;
}

int setrtx_check(struct rpt *myrpt)
{
char tmp[MAXREMSTR] = "",*s;
int	band,txpl,rxpl;

	/* must be a remote system */
	if (!myrpt->remote) return(0);
	/* must have rbi hardware */
	if (strncmp(myrpt->remoterig,remote_rig_rbi,3)) return(0);
	strncpy(tmp, myrpt->freq, sizeof(tmp) - 1);
	s = strchr(tmp,'.');
	/* if no decimal, is invalid */

	if (s == NULL){
		if(debug)
			printf("@@@@ Frequency needs a decimal\n");
		return -1;
	}

	*s++ = 0;
	if (strlen(tmp) < 2){
		if(debug)
			printf("@@@@ Bad MHz digits: %s\n", tmp);
	 	return -1;
	}

	if (strlen(s) < 3){
		if(debug)
			printf("@@@@ Bad KHz digits: %s\n", s);
	 	return -1;
	}

	if ((s[2] != '0') && (s[2] != '5')){
		if(debug)
			printf("@@@@ KHz must end in 0 or 5: %c\n", s[2]);
	 	return -1;
	}

	band = rbi_mhztoband(tmp);
	if (band == -1){
		if(debug)
			printf("@@@@ Bad Band: %s\n", tmp);
	 	return -1;
	}

	txpl = rbi_pltocode(myrpt->txpl);

	if (txpl == -1){
		if(debug)
			printf("@@@@ Bad TX PL: %s\n", myrpt->txpl);
	 	return -1;
	}

	rxpl = rbi_pltocode(myrpt->rxpl);

	if (rxpl == -1){
		if(debug)
			printf("@@@@ Bad RX PL: %s\n", myrpt->rxpl);
	 	return -1;
	}
	return 0;
}

int sendrxkenwood(struct rpt *myrpt, char *txstr, char *rxstr,
	char *cmpstr)
{
int	i,j;

	for(i = 0;i < KENWOOD_RETRIES;i++)
	{
		j = sendkenwood(myrpt,txstr,rxstr);
		if (j < 0) return(j);
		if (j == 0) continue;
		if (!strncmp(rxstr,cmpstr,strlen(cmpstr))) return(0);
	}
	return(-1);
}

int setkenwood(struct rpt *myrpt)
{
char rxstr[RAD_SERIAL_BUFLEN],txstr[RAD_SERIAL_BUFLEN],freq[20];
char mhz[MAXREMSTR],offset[20],band,decimals[MAXREMSTR],band1,band2;
int myrxpl,mysplit,step;

int offsets[] = {0,2,1};
int powers[] = {2,1,0};

	if (sendrxkenwood(myrpt,"VMC 0,0\r",rxstr,"VMC") < 0) return -1;
	split_freq(mhz, decimals, myrpt->freq);
	mysplit = myrpt->splitkhz;
	if (atoi(mhz) > 400)
	{
		band = '6';
		band1 = '1';
		band2 = '5';
		if (!mysplit) mysplit = myrpt->p.default_split_70cm;
	}
	else
	{
		band = '2';
		band1 = '0';
		band2 = '2';
		if (!mysplit) mysplit = myrpt->p.default_split_2m;
	}
	sprintf(offset,"%06d000",mysplit);
	strcpy(freq,"000000");
	strncpy(freq,decimals,strlen(decimals));
	myrxpl = myrpt->rxplon;
	if (IS_XPMR(myrpt)) myrxpl = 0;
	step = 0;
	if ((decimals[3] != '0') || (decimals[4] != '0')) step = 1;
	sprintf(txstr,"VW %c,%05d%s,%d,%d,0,%d,%d,,%02d,,%02d,%s\r",
		band,atoi(mhz),freq,step,offsets[(int)myrpt->offset],
		(myrpt->txplon != 0),myrxpl,
		kenwood_pltocode(myrpt->txpl),kenwood_pltocode(myrpt->rxpl),
		offset);
	if (sendrxkenwood(myrpt,txstr,rxstr,"VW") < 0) return -1;
	sprintf(txstr,"RBN %c\r",band2);
	if (sendrxkenwood(myrpt,txstr,rxstr,"RBN") < 0) return -1;
	sprintf(txstr,"PC %c,%d\r",band1,powers[(int)myrpt->powerlevel]);
	if (sendrxkenwood(myrpt,txstr,rxstr,"PC") < 0) return -1;
	return 0;
}

int set_tmd700(struct rpt *myrpt)
{
char rxstr[RAD_SERIAL_BUFLEN],txstr[RAD_SERIAL_BUFLEN],freq[20];
char mhz[MAXREMSTR],offset[20],decimals[MAXREMSTR];
int myrxpl,mysplit,step;

int offsets[] = {0,2,1};
int powers[] = {2,1,0};
int band;

	if (sendrxkenwood(myrpt,"BC 0,0\r",rxstr,"BC") < 0) return -1;
	split_freq(mhz, decimals, myrpt->freq);
	mysplit = myrpt->splitkhz;
	if (atoi(mhz) > 400)
	{
		band = 8;
		if (!mysplit) mysplit = myrpt->p.default_split_70cm;
	}
	else
	{
		band = 2;
		if (!mysplit) mysplit = myrpt->p.default_split_2m;
	}
	sprintf(offset,"%06d000",mysplit);
	strcpy(freq,"000000");
	strncpy(freq,decimals,strlen(decimals));
	step = 0;
	if ((decimals[3] != '0') || (decimals[4] != '0')) step = 1;
	myrxpl = myrpt->rxplon;
	if (IS_XPMR(myrpt)) myrxpl = 0;
	sprintf(txstr,"VW %d,%05d%s,%d,%d,0,%d,%d,0,%02d,0010,%02d,%s,0\r",
		band,atoi(mhz),freq,step,offsets[(int)myrpt->offset],
		(myrpt->txplon != 0),myrxpl,
		kenwood_pltocode(myrpt->txpl),kenwood_pltocode(myrpt->rxpl),
		offset);
	if (sendrxkenwood(myrpt,txstr,rxstr,"VW") < 0) return -1;
	if (sendrxkenwood(myrpt,"VMC 0,0\r",rxstr,"VMC") < 0) return -1;
	sprintf(txstr,"RBN\r");
	if (sendrxkenwood(myrpt,txstr,rxstr,"RBN") < 0) return -1;
	sprintf(txstr,"RBN %d\r",band);
	if (strncmp(rxstr,txstr,5))
	{
		if (sendrxkenwood(myrpt,txstr,rxstr,"RBN") < 0) return -1;
	}
	sprintf(txstr,"PC 0,%d\r",powers[(int)myrpt->powerlevel]);
	if (sendrxkenwood(myrpt,txstr,rxstr,"PC") < 0) return -1;
	return 0;
}

int set_tm271(struct rpt *myrpt)
{
char rxstr[RAD_SERIAL_BUFLEN],txstr[RAD_SERIAL_BUFLEN],freq[20];
char mhz[MAXREMSTR],decimals[MAXREMSTR];
int  mysplit,step;

int offsets[] = {0,2,1};
int powers[] = {2,1,0};

	split_freq(mhz, decimals, myrpt->freq);
	strcpy(freq,"000000");
	strncpy(freq,decimals,strlen(decimals));

	if (!myrpt->splitkhz)
		mysplit = myrpt->p.default_split_2m;
	else
		mysplit = myrpt->splitkhz;

	step = 0;
	if ((decimals[3] != '0') || (decimals[4] != '0')) step = 1;
	sprintf(txstr,"VF %04d%s,%d,%d,0,%d,0,0,%02d,00,000,%05d000,0,0\r",
		atoi(mhz),freq,step,offsets[(int)myrpt->offset],
		(myrpt->txplon != 0),tm271_pltocode(myrpt->txpl),mysplit);

	if (sendrxkenwood(myrpt,"VM 0\r",rxstr,"VM") < 0) return -1;
	if (sendrxkenwood(myrpt,txstr,rxstr,"VF") < 0) return -1;
	sprintf(txstr,"PC %d\r",powers[(int)myrpt->powerlevel]);
	if (sendrxkenwood(myrpt,txstr,rxstr,"PC") < 0) return -1;
	return 0;
}




int sendkenwood(struct rpt *myrpt,char *txstr, char *rxstr)
{
int	i;

	if (debug)  printf("Send to kenwood: %s\n",txstr);
	i = serial_remote_io(myrpt, (unsigned char *)txstr, strlen(txstr),
		(unsigned char *)rxstr,RAD_SERIAL_BUFLEN - 1,3);
	usleep(50000);
	if (i < 0) return -1;
	if ((i > 0) && (rxstr[i - 1] == '\r'))
		rxstr[i-- - 1] = 0;
	if (debug)  printf("Got from kenwood: %s\n",rxstr);
	return(i);
}

/* take a PL frequency and turn it into a code */
int kenwood_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 1;
	    case 719:
		return 3;
	    case 744:
		return 4;
	    case 770:
		return 5;
	    case 797:
		return 6;
	    case 825:
		return 7;
	    case 854:
		return 8;
	    case 885:
		return 9;
	    case 915:
		return 10;
	    case 948:
		return 11;
	    case 974:
		return 12;
	    case 1000:
		return 13;
	    case 1035:
		return 14;
	    case 1072:
		return 15;
	    case 1109:
		return 16;
	    case 1148:
		return 17;
	    case 1188:
		return 18;
	    case 1230:
		return 19;
	    case 1273:
		return 20;
	    case 1318:
		return 21;
	    case 1365:
		return 22;
	    case 1413:
		return 23;
	    case 1462:
		return 24;
	    case 1514:
		return 25;
	    case 1567:
		return 26;
	    case 1622:
		return 27;
	    case 1679:
		return 28;
	    case 1738:
		return 29;
	    case 1799:
		return 30;
	    case 1862:
		return 31;
	    case 1928:
		return 32;
	    case 2035:
		return 33;
	    case 2107:
		return 34;
	    case 2181:
		return 35;
	    case 2257:
		return 36;
	    case 2336:
		return 37;
	    case 2418:
		return 38;
	    case 2503:
		return 39;
	}
	return -1;
}

/* take a PL frequency and turn it into a code */
int tm271_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 0;
	    case 693:
		return 1;
	    case 719:
		return 2;
	    case 744:
		return 3;
	    case 770:
		return 4;
	    case 797:
		return 5;
	    case 825:
		return 6;
	    case 854:
		return 7;
	    case 885:
		return 8;
	    case 915:
		return 9;
	    case 948:
		return 10;
	    case 974:
		return 11;
	    case 1000:
		return 12;
	    case 1035:
		return 13;
	    case 1072:
		return 14;
	    case 1109:
		return 15;
	    case 1148:
		return 16;
	    case 1188:
		return 17;
	    case 1230:
		return 18;
	    case 1273:
		return 19;
	    case 1318:
		return 20;
	    case 1365:
		return 21;
	    case 1413:
		return 22;
	    case 1462:
		return 23;
	    case 1514:
		return 24;
	    case 1567:
		return 25;
	    case 1622:
		return 26;
	    case 1679:
		return 27;
	    case 1738:
		return 28;
	    case 1799:
		return 29;
	    case 1862:
		return 30;
	    case 1928:
		return 31;
	    case 2035:
		return 32;
	    case 2065:
		return 33;
	    case 2107:
		return 34;
	    case 2181:
		return 35;
	    case 2257:
		return 36;
	    case 2291:
		return 37;
	    case 2336:
		return 38;
	    case 2418:
		return 39;
	    case 2503:
		return 40;
	}
	return -1;
}

/* take a PL frequency and turn it into a code */
int ft950_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 0;
	    case 693:
		return 1;
	    case 719:
		return 2;
	    case 744:
		return 3;
	    case 770:
		return 4;
	    case 797:
		return 5;
	    case 825:
		return 6;
	    case 854:
		return 7;
	    case 885:
		return 8;
	    case 915:
		return 9;
	    case 948:
		return 10;
	    case 974:
		return 11;
	    case 1000:
		return 12;
	    case 1035:
		return 13;
	    case 1072:
		return 14;
	    case 1109:
		return 15;
	    case 1148:
		return 16;
	    case 1188:
		return 17;
	    case 1230:
		return 18;
	    case 1273:
		return 19;
	    case 1318:
		return 20;
	    case 1365:
		return 21;
	    case 1413:
		return 22;
	    case 1462:
		return 23;
	    case 1514:
		return 24;
	    case 1567:
		return 25;
	    case 1622:
		return 26;
	    case 1679:
		return 27;
	    case 1738:
		return 28;
	    case 1799:
		return 29;
	    case 1862:
		return 30;
	    case 1928:
		return 31;
	    case 2035:
		return 32;
	    case 2065:
		return 33;
	    case 2107:
		return 34;
	    case 2181:
		return 35;
	    case 2257:
		return 36;
	    case 2291:
		return 37;
	    case 2336:
		return 38;
	    case 2418:
		return 39;
	    case 2503:
		return 40;
	}
	return -1;
}
/* take a PL frequency and turn it into a code */
int ft100_pltocode(char *str)
{
int i;
char *s;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
		return 0;
	    case 693:
		return 1;
	    case 719:
		return 2;
	    case 744:
		return 3;
	    case 770:
		return 4;
	    case 797:
		return 5;
	    case 825:
		return 6;
	    case 854:
		return 7;
	    case 885:
		return 8;
	    case 915:
		return 9;
	    case 948:
		return 10;
	    case 974:
		return 11;
	    case 1000:
		return 12;
	    case 1035:
		return 13;
	    case 1072:
		return 14;
	    case 1109:
		return 15;
	    case 1148:
		return 16;
	    case 1188:
		return 17;
	    case 1230:
		return 18;
	    case 1273:
		return 19;
	    case 1318:
		return 20;
	    case 1365:
		return 21;
	    case 1413:
		return 22;
	    case 1462:
		return 23;
	    case 1514:
		return 24;
	    case 1567:
		return 25;
	    case 1622:
		return 26;
	    case 1679:
		return 27;
	    case 1738:
		return 28;
	    case 1799:
		return 29;
	    case 1862:
		return 30;
	    case 1928:
		return 31;
	    case 2035:
		return 32;
	    case 2107:
		return 33;
	    case 2181:
		return 34;
	    case 2257:
		return 35;
	    case 2336:
		return 36;
	    case 2418:
		return 37;
	    case 2503:
		return 38;
	}
	return -1;
}




int check_freq_kenwood(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 144){ /* 2 meters */
		if(d < 10100)
			return -1;
	}
	else if((m >= 145) && (m < 148)){
		;
	}
	else if((m >= 430) && (m < 450)){ /* 70 centimeters */
		;
	}
	else
		return -1;

	if(defmode)
		*defmode = dflmd;


	return 0;
}


int check_freq_tm271(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 144){ /* 2 meters */
		if(d < 10100)
			return -1;
	}
	else if((m >= 145) && (m < 148)){
		;
	}
	else	return -1;

	if(defmode)
		*defmode = dflmd;


	return 0;
}


/* Check for valid rbi frequency */
/* Hard coded limits now, configurable later, maybe? */

int check_freq_rbi(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if(m == 50){ /* 6 meters */
		if(d < 10100)
			return -1;
	}
	else if((m >= 51) && ( m < 54)){
                ;
	}
	else if(m == 144){ /* 2 meters */
		if(d < 10100)
			return -1;
	}
	else if((m >= 145) && (m < 148)){
		;
	}
 	else if((m >= 222) && (m < 225)){ /* 1.25 meters */
		;
	}
	else if((m >= 430) && (m < 450)){ /* 70 centimeters */
		;
	}
	else if((m >= 1240) && (m < 1300)){ /* 23 centimeters */
		;
	}
	else
		return -1;

	if(defmode)
		*defmode = dflmd;


	return 0;
}

/* Check for valid rtx frequency */
/* Hard coded limits now, configurable later, maybe? */

int check_freq_rtx(int m, int d, int *defmode, struct rpt *myrpt)
{
	int dflmd = REM_MODE_FM;

	if (!strcmp(myrpt->remoterig,remote_rig_rtx150))
	{

		if(m == 144){ /* 2 meters */
			if(d < 10100)
				return -1;
		}
		else if((m >= 145) && (m < 148)){
			;
		}
		else
			return -1;
	}
	else
	{
		if((m >= 430) && (m < 450)){ /* 70 centimeters */
			;
		}
		else
			return -1;
	}
	if(defmode)
		*defmode = dflmd;


	return 0;
}

/*
 * Convert decimals of frequency to int
 */

int decimals2int(char *fraction)
{
	int i;
	char len = strlen(fraction);
	int multiplier = 100000;
	int res = 0;

	if(!len)
		return 0;
	for( i = 0 ; i < len ; i++, multiplier /= 10)
		res += (fraction[i] - '0') * multiplier;
	return res;
}


/*
* Split frequency into mhz and decimals
*/

int split_freq(char *mhz, char *decimals, char *freq)
{
	char freq_copy[MAXREMSTR];
	char *decp;

	decp = strchr(strncpy(freq_copy, freq, MAXREMSTR),'.');
	if(decp){
		*decp++ = 0;
		strncpy(mhz, freq_copy, MAXREMSTR);
		strcpy(decimals, "00000");
		strncpy(decimals, decp, strlen(decp));
		decimals[5] = 0;
		return 0;
	}
	else
		return -1;

}

/*
* Split ctcss frequency into hertz and decimal
*/

int split_ctcss_freq(char *hertz, char *decimal, char *freq)
{
	char freq_copy[MAXREMSTR];
	char *decp;

	decp = strchr(strncpy(freq_copy, freq, MAXREMSTR),'.');
	if(decp){
		*decp++ = 0;
		strncpy(hertz, freq_copy, MAXREMSTR);
		strncpy(decimal, decp, strlen(decp));
		decimal[strlen(decp)] = '\0';
		return 0;
	}
	else
		return -1;
}



/*
* FT-897 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


int check_freq_ft897(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if(m == 1){ /* 160 meters */
		dflmd =	REM_MODE_LSB;
		if(d < 80000)
			return -1;
	}
	else if(m == 3){ /* 80 meters */
		dflmd = REM_MODE_LSB;
		if(d < 50000)
			return -1;
	}
	else if(m == 7){ /* 40 meters */
		dflmd = REM_MODE_LSB;
		if(d > 30000)
			return -1;
	}
	else if(m == 14){ /* 20 meters */
		dflmd = REM_MODE_USB;
		if(d > 35000)
			return -1;
	}
	else if(m == 18){ /* 17 meters */
		dflmd = REM_MODE_USB;
		if((d < 6800) || (d > 16800))
			return -1;
	}
	else if(m == 21){ /* 15 meters */
		dflmd = REM_MODE_USB;
		if((d < 20000) || (d > 45000))
			return -1;
	}
	else if(m == 24){ /* 12 meters */
		dflmd = REM_MODE_USB;
		if((d < 89000) || (d > 99000))
			return -1;
	}
	else if(m == 28){ /* 10 meters */
		dflmd = REM_MODE_USB;
	}
	else if(m == 29){
		if(d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if(d > 70000)
			return -1;
	}
	else if(m == 50){ /* 6 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;

	}
	else if((m >= 51) && ( m < 54)){
		dflmd = REM_MODE_FM;
	}
	else if(m == 144){ /* 2 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	}
	else if((m >= 145) && (m < 148)){
		dflmd = REM_MODE_FM;
	}
	else if((m >= 430) && (m < 450)){ /* 70 centimeters */
		if(m  < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
		;
	}
	else
		return -1;

	if(defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the FT897
*/

int set_freq_ft897(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[5];
	int fd,m,d;
	char mhz[MAXREMSTR];
	char decimals[MAXREMSTR];

	fd = 0;
	if(debug)
		printf("New frequency: %s\n",newfreq);

	if(split_freq(mhz, decimals, newfreq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	/* The FT-897 likes packed BCD frequencies */

	cmdstr[0] = ((m / 100) << 4) + ((m % 100)/10);			/* 100MHz 10Mhz */
	cmdstr[1] = ((m % 10) << 4) + (d / 10000);			/* 1MHz 100KHz */
	cmdstr[2] = (((d % 10000)/1000) << 4) + ((d % 1000)/ 100);	/* 10KHz 1KHz */
	cmdstr[3] = (((d % 100)/10) << 4) + (d % 10);			/* 100Hz 10Hz */
	cmdstr[4] = 0x01;						/* command */

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 simple commands */

int simple_command_ft897(struct rpt *myrpt, char command)
{
	unsigned char cmdstr[5];

	memset(cmdstr, 0, 5);

	cmdstr[4] = command;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 offset */

int set_offset_ft897(struct rpt *myrpt, char offset)
{
	unsigned char cmdstr[5];
	int mysplit,res;
	char mhz[MAXREMSTR],decimal[MAXREMSTR];

	if(split_freq(mhz, decimal, myrpt->freq))
		return -1;

	mysplit = myrpt->splitkhz * 1000;
	if (!mysplit)
	{
		if (atoi(mhz) > 400)
			mysplit = myrpt->p.default_split_70cm * 1000;
		else
			mysplit = myrpt->p.default_split_2m * 1000;
	}

	memset(cmdstr, 0, 5);

	if(debug > 6)
		ast_log(LOG_NOTICE,"split=%i\n",mysplit * 1000);

	cmdstr[0] = (mysplit / 10000000) +  ((mysplit % 10000000) / 1000000);
	cmdstr[1] = (((mysplit % 1000000) / 100000) << 4) + ((mysplit % 100000) / 10000);
	cmdstr[2] = (((mysplit % 10000) / 1000) << 4) + ((mysplit % 1000) / 100);
	cmdstr[3] = ((mysplit % 10) << 4) + ((mysplit % 100) / 10);
	cmdstr[4] = 0xf9;						/* command */
	res = serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
	if (res) return res;

	memset(cmdstr, 0, 5);

	switch(offset){
		case	REM_SIMPLEX:
			cmdstr[0] = 0x89;
			break;

		case	REM_MINUS:
			cmdstr[0] = 0x09;
			break;

		case	REM_PLUS:
			cmdstr[0] = 0x49;
			break;

		default:
			return -1;
	}

	cmdstr[4] = 0x09;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* ft-897 mode */

int set_mode_ft897(struct rpt *myrpt, char newmode)
{
	unsigned char cmdstr[5];

	memset(cmdstr, 0, 5);

	switch(newmode){
		case	REM_MODE_FM:
			cmdstr[0] = 0x08;
			break;

		case	REM_MODE_USB:
			cmdstr[0] = 0x01;
			break;

		case	REM_MODE_LSB:
			cmdstr[0] = 0x00;
			break;

		case	REM_MODE_AM:
			cmdstr[0] = 0x04;
			break;

		default:
			return -1;
	}
	cmdstr[4] = 0x07;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}

/* Set tone encode and decode modes */

int set_ctcss_mode_ft897(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char cmdstr[5];

	memset(cmdstr, 0, 5);

	if(rxplon && txplon)
		cmdstr[0] = 0x2A; /* Encode and Decode */
	else if (!rxplon && txplon)
		cmdstr[0] = 0x4A; /* Encode only */
	else if (rxplon && !txplon)
		cmdstr[0] = 0x3A; /* Encode only */
	else
		cmdstr[0] = 0x8A; /* OFF */

	cmdstr[4] = 0x0A;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}


/* Set transmit and receive ctcss tone frequencies */

int set_ctcss_freq_ft897(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char cmdstr[5];
	char hertz[MAXREMSTR],decimal[MAXREMSTR];
	int h,d;

	memset(cmdstr, 0, 5);

	if(split_ctcss_freq(hertz, decimal, txtone))
		return -1;

	h = atoi(hertz);
	d = atoi(decimal);

	cmdstr[0] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[1] = ((h % 10) << 4) + (d % 10);

	if(rxtone){

		if(split_ctcss_freq(hertz, decimal, rxtone))
			return -1;

		h = atoi(hertz);
		d = atoi(decimal);

		cmdstr[2] = ((h / 100) << 4) + (h % 100)/ 10;
		cmdstr[3] = ((h % 10) << 4) + (d % 10);
	}
	cmdstr[4] = 0x0B;

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);
}



int set_ft897(struct rpt *myrpt)
{
	int res;

	if(debug > 2)
		printf("@@@@ lock on\n");
	res = simple_command_ft897(myrpt, 0x00);			/* LOCK on */

	if(debug > 2)
		printf("@@@@ ptt off\n");
	if(!res){
		res = simple_command_ft897(myrpt, 0x88);		/* PTT off */
	}

	if(debug > 2)
		printf("Modulation mode\n");
	if(!res){
		res = set_mode_ft897(myrpt, myrpt->remmode);		/* Modulation mode */
	}

	if(debug > 2)
		printf("Split off\n");
	if(!res){
		simple_command_ft897(myrpt, 0x82);			/* Split off */
	}

	if(debug > 2)
		printf("Frequency\n");
	if(!res){
		res = set_freq_ft897(myrpt, myrpt->freq);		/* Frequency */
		usleep(FT897_SERIAL_DELAY*2);
	}
	if((myrpt->remmode == REM_MODE_FM)){
		if(debug > 2)
			printf("Offset\n");
		if(!res){
			res = set_offset_ft897(myrpt, myrpt->offset);	/* Offset if FM */
			usleep(FT897_SERIAL_DELAY);
		}
		if((!res)&&(myrpt->rxplon || myrpt->txplon)){
			usleep(FT897_SERIAL_DELAY);
			if(debug > 2)
				printf("CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft897(myrpt, myrpt->txpl, myrpt->rxpl); /* CTCSS freqs if CTCSS is enabled */
			usleep(FT897_SERIAL_DELAY);
		}
		if(!res){
			if(debug > 2)
				printf("CTCSS mode\n");
			res = set_ctcss_mode_ft897(myrpt, myrpt->txplon, myrpt->rxplon); /* CTCSS mode */
			usleep(FT897_SERIAL_DELAY);
		}
	}
	if((myrpt->remmode == REM_MODE_USB)||(myrpt->remmode == REM_MODE_LSB)){
		if(debug > 2)
			printf("Clarifier off\n");
		simple_command_ft897(myrpt, 0x85);			/* Clarifier off if LSB or USB */
	}
	return res;
}

int closerem_ft897(struct rpt *myrpt)
{
	simple_command_ft897(myrpt, 0x88); /* PTT off */
	return 0;
}

/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

int multimode_bump_freq_ft897(struct rpt *myrpt, int interval)
{
	int m,d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];

	if(debug)
		printf("Before bump: %s\n", myrpt->freq);

	if(split_freq(mhz, decimals, myrpt->freq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10); /* 10Hz resolution */
	if(d < 0){
		m--;
		d += 100000;
	}
	else if(d >= 100000){
		m++;
		d -= 100000;
	}

	if(check_freq_ft897(m, d, NULL)){
		if(debug)
			printf("Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	if(debug)
		printf("After bump: %s\n", myrpt->freq);

	return set_freq_ft897(myrpt, myrpt->freq);
}
/*
* FT-100 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


int check_freq_ft100(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if(m == 1){ /* 160 meters */
		dflmd =	REM_MODE_LSB;
		if(d < 80000)
			return -1;
	}
	else if(m == 3){ /* 80 meters */
		dflmd = REM_MODE_LSB;
		if(d < 50000)
			return -1;
	}
	else if(m == 7){ /* 40 meters */
		dflmd = REM_MODE_LSB;
		if(d > 30000)
			return -1;
	}
	else if(m == 14){ /* 20 meters */
		dflmd = REM_MODE_USB;
		if(d > 35000)
			return -1;
	}
	else if(m == 18){ /* 17 meters */
		dflmd = REM_MODE_USB;
		if((d < 6800) || (d > 16800))
			return -1;
	}
	else if(m == 21){ /* 15 meters */
		dflmd = REM_MODE_USB;
		if((d < 20000) || (d > 45000))
			return -1;
	}
	else if(m == 24){ /* 12 meters */
		dflmd = REM_MODE_USB;
		if((d < 89000) || (d > 99000))
			return -1;
	}
	else if(m == 28){ /* 10 meters */
		dflmd = REM_MODE_USB;
	}
	else if(m == 29){
		if(d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if(d > 70000)
			return -1;
	}
	else if(m == 50){ /* 6 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;

	}
	else if((m >= 51) && ( m < 54)){
		dflmd = REM_MODE_FM;
	}
	else if(m == 144){ /* 2 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	}
	else if((m >= 145) && (m < 148)){
		dflmd = REM_MODE_FM;
	}
	else if((m >= 430) && (m < 450)){ /* 70 centimeters */
		if(m  < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
		;
	}
	else
		return -1;

	if(defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the ft100
*/

int set_freq_ft100(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[5];
	int fd,m,d;
	char mhz[MAXREMSTR];
	char decimals[MAXREMSTR];

	fd = 0;
	if(debug)
		printf("New frequency: %s\n",newfreq);

	if(split_freq(mhz, decimals, newfreq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	/* The FT-100 likes packed BCD frequencies */

	cmdstr[0] = (((d % 100)/10) << 4) + (d % 10);			/* 100Hz 10Hz */
	cmdstr[1] = (((d % 10000)/1000) << 4) + ((d % 1000)/ 100);	/* 10KHz 1KHz */
	cmdstr[2] = ((m % 10) << 4) + (d / 10000);			/* 1MHz 100KHz */
	cmdstr[3] = ((m / 100) << 4) + ((m % 100)/10);			/* 100MHz 10Mhz */
	cmdstr[4] = 0x0a;						/* command */

	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 simple commands */

int simple_command_ft100(struct rpt *myrpt, unsigned char command, unsigned char p1)
{
	unsigned char cmdstr[5];

	memset(cmdstr, 0, 5);
	cmdstr[3] = p1;
	cmdstr[4] = command;


	return serial_remote_io(myrpt, cmdstr, 5, NULL, 0, 0);

}

/* ft-897 offset */

int set_offset_ft100(struct rpt *myrpt, char offset)
{
	unsigned char p1;

	switch(offset){
		case	REM_SIMPLEX:
			p1 = 0;
			break;

		case	REM_MINUS:
			p1 = 1;
			break;

		case	REM_PLUS:
			p1 = 2;
			break;

		default:
			return -1;
	}

	return simple_command_ft100(myrpt,0x84,p1);
}

/* ft-897 mode */

int set_mode_ft100(struct rpt *myrpt, char newmode)
{
	unsigned char p1;

	switch(newmode){
		case	REM_MODE_FM:
			p1 = 6;
			break;

		case	REM_MODE_USB:
			p1 = 1;
			break;

		case	REM_MODE_LSB:
			p1 = 0;
			break;

		case	REM_MODE_AM:
			p1 = 4;
			break;

		default:
			return -1;
	}
	return simple_command_ft100(myrpt,0x0c,p1);
}

/* Set tone encode and decode modes */

int set_ctcss_mode_ft100(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char p1;

	if(rxplon)
		p1 = 2; /* Encode and Decode */
	else if (!rxplon && txplon)
		p1 = 1; /* Encode only */
	else
		p1 = 0; /* OFF */

	return simple_command_ft100(myrpt,0x92,p1);
}


/* Set transmit and receive ctcss tone frequencies */

int set_ctcss_freq_ft100(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char p1;

	p1 = ft100_pltocode(rxtone);
	return simple_command_ft100(myrpt,0x90,p1);
}

int set_ft100(struct rpt *myrpt)
{
	int res;


	if(debug > 2)
		printf("Modulation mode\n");
	res = set_mode_ft100(myrpt, myrpt->remmode);		/* Modulation mode */

	if(debug > 2)
		printf("Split off\n");
	if(!res){
		simple_command_ft100(myrpt, 0x01,0);			/* Split off */
	}

	if(debug > 2)
		printf("Frequency\n");
	if(!res){
		res = set_freq_ft100(myrpt, myrpt->freq);		/* Frequency */
		usleep(FT100_SERIAL_DELAY*2);
	}
	if((myrpt->remmode == REM_MODE_FM)){
		if(debug > 2)
			printf("Offset\n");
		if(!res){
			res = set_offset_ft100(myrpt, myrpt->offset);	/* Offset if FM */
			usleep(FT100_SERIAL_DELAY);
		}
		if((!res)&&(myrpt->rxplon || myrpt->txplon)){
			usleep(FT100_SERIAL_DELAY);
			if(debug > 2)
				printf("CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft100(myrpt, myrpt->txpl, myrpt->rxpl); /* CTCSS freqs if CTCSS is enabled */
			usleep(FT100_SERIAL_DELAY);
		}
		if(!res){
			if(debug > 2)
				printf("CTCSS mode\n");
			res = set_ctcss_mode_ft100(myrpt, myrpt->txplon, myrpt->rxplon); /* CTCSS mode */
			usleep(FT100_SERIAL_DELAY);
		}
	}
	return res;
}

int closerem_ft100(struct rpt *myrpt)
{
	simple_command_ft100(myrpt, 0x0f,0); /* PTT off */
	return 0;
}

/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

int multimode_bump_freq_ft100(struct rpt *myrpt, int interval)
{
	int m,d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];

	if(debug)
		printf("Before bump: %s\n", myrpt->freq);

	if(split_freq(mhz, decimals, myrpt->freq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10); /* 10Hz resolution */
	if(d < 0){
		m--;
		d += 100000;
	}
	else if(d >= 100000){
		m++;
		d -= 100000;
	}

	if(check_freq_ft100(m, d, NULL)){
		if(debug)
			printf("Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	if(debug)
		printf("After bump: %s\n", myrpt->freq);

	return set_freq_ft100(myrpt, myrpt->freq);
}



/*
* FT-950 I/O handlers
*/

/* Check to see that the frequency is valid */
/* Hard coded limits now, configurable later, maybe? */


int check_freq_ft950(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if(m == 1){ /* 160 meters */
		dflmd =	REM_MODE_LSB;
		if(d < 80000)
			return -1;
	}
	else if(m == 3){ /* 80 meters */
		dflmd = REM_MODE_LSB;
		if(d < 50000)
			return -1;
	}
	else if(m == 7){ /* 40 meters */
		dflmd = REM_MODE_LSB;
		if(d > 30000)
			return -1;
	}
	else if(m == 14){ /* 20 meters */
		dflmd = REM_MODE_USB;
		if(d > 35000)
			return -1;
	}
	else if(m == 18){ /* 17 meters */
		dflmd = REM_MODE_USB;
		if((d < 6800) || (d > 16800))
			return -1;
	}
	else if(m == 21){ /* 15 meters */
		dflmd = REM_MODE_USB;
		if((d < 20000) || (d > 45000))
			return -1;
	}
	else if(m == 24){ /* 12 meters */
		dflmd = REM_MODE_USB;
		if((d < 89000) || (d > 99000))
			return -1;
	}
	else if(m == 28){ /* 10 meters */
		dflmd = REM_MODE_USB;
	}
	else if(m == 29){
		if(d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if(d > 70000)
			return -1;
	}
	else if(m == 50){ /* 6 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;

	}
	else if((m >= 51) && ( m < 54)){
		dflmd = REM_MODE_FM;
	}
	else
		return -1;

	if(defmode)
		*defmode = dflmd;

	return 0;
}

/*
* Set a new frequency for the ft950
*/

int set_freq_ft950(struct rpt *myrpt, char *newfreq)
{
	char cmdstr[20];
	int fd,m,d;
	char mhz[MAXREMSTR];
	char decimals[MAXREMSTR];

	fd = 0;
	if(debug)
		printf("New frequency: %s\n",newfreq);

	if(split_freq(mhz, decimals, newfreq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);


	sprintf(cmdstr,"FA%d%06d;",m,d * 10);
	return serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0);

}

/* ft-950 offset */

int set_offset_ft950(struct rpt *myrpt, char offset)
{
	char *cmdstr;

	switch(offset){
		case	REM_SIMPLEX:
			cmdstr = "OS00;";
			break;

		case	REM_MINUS:
			cmdstr = "OS02;";
			break;

		case	REM_PLUS:
			cmdstr = "OS01;";
			break;

		default:
			return -1;
	}

	return serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0);
}

/* ft-950 mode */

int set_mode_ft950(struct rpt *myrpt, char newmode)
{
	char *cmdstr;

	switch(newmode){
		case	REM_MODE_FM:
			cmdstr = "MD04;";
			break;

		case	REM_MODE_USB:
			cmdstr = "MD02;";
			break;

		case	REM_MODE_LSB:
			cmdstr = "MD01;";
			break;

		case	REM_MODE_AM:
			cmdstr = "MD05;";
			break;

		default:
			return -1;
	}

	return serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0);
}

/* Set tone encode and decode modes */

int set_ctcss_mode_ft950(struct rpt *myrpt, char txplon, char rxplon)
{
	char *cmdstr;


	if(rxplon && txplon)
		cmdstr = "CT01;";
	else if (!rxplon && txplon)
		cmdstr = "CT02;"; /* Encode only */
	else if (rxplon && !txplon)
		cmdstr = "CT02;"; /* Encode only */
	else
		cmdstr = "CT00;"; /* OFF */

	return serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0);
}


/* Set transmit and receive ctcss tone frequencies */

int set_ctcss_freq_ft950(struct rpt *myrpt, char *txtone, char *rxtone)
{
	char cmdstr[10];
	int c;

	c = ft950_pltocode(txtone);
	if (c < 0) return(-1);

	sprintf(cmdstr,"CN0%02d;",c);

	return serial_remote_io(myrpt, (unsigned char *)cmdstr, 5, NULL, 0, 0);
}



int set_ft950(struct rpt *myrpt)
{
	int res;
	char *cmdstr;

	if(debug)
		printf("ptt off\n");

	cmdstr = "MX0;";
	res = serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0); /* MOX off */

	if(debug)
		printf("select ant. 1\n");

	cmdstr = "AN01;";
	res = serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0); /* MOX off */

	if(debug)
		printf("Modulation mode\n");

	if(!res)
		res = set_mode_ft950(myrpt, myrpt->remmode);		/* Modulation mode */

	if(debug)
		printf("Split off\n");

	cmdstr = "OS00;";
	if(!res)
		res = serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0); /* Split off */

	if(debug)
		printf("VFO Modes\n");

	if (!res)
		res = serial_remote_io(myrpt, (unsigned char *)"FR0;", 4, NULL, 0, 0);
	if (!res)
		res = serial_remote_io(myrpt, (unsigned char *)"FT2;", 4, NULL, 0, 0);

	if(debug)
		printf("Frequency\n");

	if(!res)
		res = set_freq_ft950(myrpt, myrpt->freq);		/* Frequency */
	if((myrpt->remmode == REM_MODE_FM)){
		if(debug)
			printf("Offset\n");
		if(!res)
			res = set_offset_ft950(myrpt, myrpt->offset);	/* Offset if FM */
		if((!res)&&(myrpt->rxplon || myrpt->txplon)){
			if(debug)
				printf("CTCSS tone freqs.\n");
			res = set_ctcss_freq_ft950(myrpt, myrpt->txpl, myrpt->rxpl); /* CTCSS freqs if CTCSS is enabled */
		}
		if(!res){
			if(debug)
				printf("CTCSS mode\n");
			res = set_ctcss_mode_ft950(myrpt, myrpt->txplon, myrpt->rxplon); /* CTCSS mode */
		}
	}
	if((myrpt->remmode == REM_MODE_USB)||(myrpt->remmode == REM_MODE_LSB)){
		if(debug)
			printf("Clarifier off\n");
		cmdstr = "RT0;";
		serial_remote_io(myrpt, (unsigned char *)cmdstr, strlen(cmdstr), NULL, 0, 0); /* Clarifier off if LSB or USB */
	}
	return res;
}

/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

int multimode_bump_freq_ft950(struct rpt *myrpt, int interval)
{
	int m,d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];

	if(debug)
		printf("Before bump: %s\n", myrpt->freq);

	if(split_freq(mhz, decimals, myrpt->freq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10); /* 10Hz resolution */
	if(d < 0){
		m--;
		d += 100000;
	}
	else if(d >= 100000){
		m++;
		d -= 100000;
	}

	if(check_freq_ft950(m, d, NULL)){
		if(debug)
			printf("Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	if(debug)
		printf("After bump: %s\n", myrpt->freq);

	return set_freq_ft950(myrpt, myrpt->freq);
}



/*
* IC-706 I/O handlers
*/

/* Check to see that the frequency is valid */
/* returns 0 if frequency is valid          */

int check_freq_ic706(int m, int d, int *defmode, char mars)
{
	int dflmd = REM_MODE_FM;
	int rv=0;

	if(debug > 6)
		ast_log(LOG_NOTICE,"(%i,%i,%i,%i)\n",m,d,*defmode,mars);

	/* first test for standard amateur radio bands */

	if(m == 1){ 					/* 160 meters */
		dflmd =	REM_MODE_LSB;
		if(d < 80000)rv=-1;
	}
	else if(m == 3){ 				/* 80 meters */
		dflmd = REM_MODE_LSB;
		if(d < 50000)rv=-1;
	}
	else if(m == 7){ 				/* 40 meters */
		dflmd = REM_MODE_LSB;
		if(d > 30000)rv=-1;
	}
	else if(m == 14){ 				/* 20 meters */
		dflmd = REM_MODE_USB;
		if(d > 35000)rv=-1;
	}
	else if(m == 18){ 							/* 17 meters */
		dflmd = REM_MODE_USB;
		if((d < 6800) || (d > 16800))rv=-1;
	}
	else if(m == 21){ /* 15 meters */
		dflmd = REM_MODE_USB;
		if((d < 20000) || (d > 45000))rv=-1;
	}
	else if(m == 24){ /* 12 meters */
		dflmd = REM_MODE_USB;
		if((d < 89000) || (d > 99000))rv=-1;
	}
	else if(m == 28){ 							/* 10 meters */
		dflmd = REM_MODE_USB;
	}
	else if(m == 29){
		if(d >= 51000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
		if(d > 70000)rv=-1;
	}
	else if(m == 50){ 							/* 6 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	}
	else if((m >= 51) && ( m < 54)){
		dflmd = REM_MODE_FM;
	}
	else if(m == 144){ /* 2 meters */
		if(d >= 30000)
			dflmd = REM_MODE_FM;
		else
			dflmd = REM_MODE_USB;
	}
	else if((m >= 145) && (m < 148)){
		dflmd = REM_MODE_FM;
	}
	else if((m >= 430) && (m < 450)){ 			/* 70 centimeters */
		if(m  < 438)
			dflmd = REM_MODE_USB;
		else
			dflmd = REM_MODE_FM;
	}

	/* check expanded coverage */
	if(mars && rv<0){
		if((m >= 450) && (m < 470)){ 			/* LMR */
			dflmd = REM_MODE_FM;
			rv=0;
		}
		else if((m >= 148) && (m < 174)){ 		/* LMR */
			dflmd = REM_MODE_FM;
			rv=0;
		}
		else if((m >= 138) && (m < 144)){ 		/* VHF-AM AIRCRAFT */
			dflmd = REM_MODE_AM;
			rv=0;
		}
		else if((m >= 108) && (m < 138)){ 		/* VHF-AM AIRCRAFT */
			dflmd = REM_MODE_AM;
			rv=0;
		}
		else if( (m==0 && d>=55000) || (m==1 && d<=75000) ){ 	/* AM BCB*/
			dflmd = REM_MODE_AM;
			rv=0;
		}
  		else if( (m == 1 && d>75000) || (m>1 && m<30) ){ 		/* HF SWL*/
			dflmd = REM_MODE_AM;
			rv=0;
		}
	}

	if(defmode)
		*defmode = dflmd;

	if(debug > 1)
		ast_log(LOG_NOTICE,"(%i,%i,%i,%i) returning %i\n",m,d,*defmode,mars,rv);

	return rv;
}

/* take a PL frequency and turn it into a code */
int ic706_pltocode(char *str)
{
	int i;
	char *s;
	int rv=-1;

	s = strchr(str,'.');
	i = 0;
	if (s) i = atoi(s + 1);
	i += atoi(str) * 10;
	switch(i)
	{
	    case 670:
			rv=0;
			break;
	    case 693:
			rv=1;
			break;
	    case 719:
			rv=2;
			break;
	    case 744:
			rv=3;
			break;
	    case 770:
			rv=4;
			break;
	    case 797:
			rv=5;
			break;
	    case 825:
			rv=6;
			break;
	    case 854:
			rv=7;
			break;
	    case 885:
			rv=8;
			break;
	    case 915:
			rv=9;
			break;
	    case 948:
			rv=10;
			break;
	    case 974:
			rv=11;
			break;
	    case 1000:
			rv=12;
			break;
	    case 1035:
			rv=13;
			break;
	    case 1072:
			rv=14;
			break;
	    case 1109:
			rv=15;
			break;
	    case 1148:
			rv=16;
			break;
	    case 1188:
			rv=17;
			break;
	    case 1230:
			rv=18;
			break;
	    case 1273:
			rv=19;
			break;
	    case 1318:
			rv=20;
			break;
	    case 1365:
			rv=21;
			break;
	    case 1413:
			rv=22;
			break;
	    case 1462:
			rv=23;
			break;
	    case 1514:
			rv=24;
			break;
	    case 1567:
			rv=25;
			break;
	    case 1598:
			rv=26;
			break;
	    case 1622:
			rv=27;
			break;
	    case 1655:
			rv=28;
			break;
	    case 1679:
			rv=29;
			break;
	    case 1713:
			rv=30;
			break;
	    case 1738:
			rv=31;
			break;
	    case 1773:
			rv=32;
			break;
	    case 1799:
			rv=33;
			break;
	    case 1835:
			rv=34;
			break;
	    case 1862:
			rv=35;
			break;
	    case 1899:
			rv=36;
			break;
	    case 1928:
			rv=37;
			break;
	    case 1966:
			rv=38;
			break;
	    case 1995:
			rv=39;
			break;
	    case 2035:
			rv=40;
			break;
	    case 2065:
			rv=41;
			break;
	    case 2107:
			rv=42;
			break;
	    case 2181:
			rv=43;
			break;
	    case 2257:
			rv=44;
			break;
	    case 2291:
			rv=45;
			break;
	    case 2336:
			rv=46;
			break;
	    case 2418:
			rv=47;
			break;
	    case 2503:
			rv=48;
			break;
	    case 2541:
			rv=49;
			break;
	}
	if(debug > 1)
		ast_log(LOG_NOTICE,"%i  rv=%i\n",i, rv);

	return rv;
}


static int civ_cmd(struct rpt *myrpt,unsigned char *cmd, int cmdlen)
{
unsigned char rxbuf[100];
int	i,rv ;

	rv = serial_remote_io(myrpt,cmd,cmdlen,rxbuf,(myrpt->p.dusbabek) ? 6 : cmdlen + 6,0);
	if (rv == -1) return(-1);
	if (myrpt->p.dusbabek)
	{
		if (rxbuf[0] != 0xfe) return(1);
		if (rxbuf[1] != 0xfe) return(1);
		if (rxbuf[4] != 0xfb) return(1);
		if (rxbuf[5] != 0xfd) return(1);
		return(0);
	}
	if (rv != (cmdlen + 6)) return(1);
	for(i = 0; i < 6; i++)
		if (rxbuf[i] != cmd[i]) return(1);
	if (rxbuf[cmdlen] != 0xfe) return(1);
	if (rxbuf[cmdlen + 1] != 0xfe) return(1);
	if (rxbuf[cmdlen + 4] != 0xfb) return(1);
	if (rxbuf[cmdlen + 5] != 0xfd) return(1);
	return(0);
}




/* ic-706 simple commands */

int simple_command_ic706(struct rpt *myrpt, char command, char subcommand)
{
	unsigned char cmdstr[10];

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = command;
	cmdstr[5] = subcommand;
	cmdstr[6] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,7));
}

/*
* Set a new frequency for the ic706
*/

int set_freq_ic706(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[20];
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	int fd,m,d;

	fd = 0;
	if(debug)
		ast_log(LOG_NOTICE,"newfreq:%s\n",newfreq);

	if(split_freq(mhz, decimals, newfreq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	/* The ic-706 likes packed BCD frequencies */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 5;
	cmdstr[5] = ((d % 10) << 4);
	cmdstr[6] = (((d % 1000)/ 100) << 4) + ((d % 100)/10);
	cmdstr[7] = ((d / 10000) << 4) + ((d % 10000)/1000);
	cmdstr[8] = (((m % 100)/10) << 4) + (m % 10);
	cmdstr[9] = (m / 100);
	cmdstr[10] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,11));
}

/* ic-706 offset */

int set_offset_ic706(struct rpt *myrpt, char offset)
{
	unsigned char c;
	int mysplit,res;
	char mhz[MAXREMSTR],decimal[MAXREMSTR];
	unsigned char cmdstr[10];

	if(split_freq(mhz, decimal, myrpt->freq))
		return -1;

	mysplit = myrpt->splitkhz * 10;
	if (!mysplit)
	{
		if (atoi(mhz) > 400)
			mysplit = myrpt->p.default_split_70cm * 10;
		else
			mysplit = myrpt->p.default_split_2m * 10;
	}

	if(debug > 6)
		ast_log(LOG_NOTICE,"split=%i\n",mysplit * 100);

	/* The ic-706 likes packed BCD data */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x0d;
	cmdstr[5] = ((mysplit % 10) << 4) + ((mysplit % 100) / 10);
	cmdstr[6] = (((mysplit % 10000) / 1000) << 4) + ((mysplit % 1000) / 100);
	cmdstr[7] = ((mysplit / 100000) << 4) + ((mysplit % 100000) / 10000);
	cmdstr[8] = 0xfd;

	res = civ_cmd(myrpt,cmdstr,9);
	if (res) return res;

	if(debug > 6)
		ast_log(LOG_NOTICE,"offset=%i\n",offset);

	switch(offset){
		case	REM_SIMPLEX:
			c = 0x10;
			break;

		case	REM_MINUS:
			c = 0x11;
			break;

		case	REM_PLUS:
			c = 0x12;
			break;

		default:
			return -1;
	}

	return simple_command_ic706(myrpt,0x0f,c);

}

/* ic-706 mode */

int set_mode_ic706(struct rpt *myrpt, char newmode)
{
	unsigned char c;

	if(debug > 6)
		ast_log(LOG_NOTICE,"newmode=%i\n",newmode);

	switch(newmode){
		case	REM_MODE_FM:
			c = 5;
			break;

		case	REM_MODE_USB:
			c = 1;
			break;

		case	REM_MODE_LSB:
			c = 0;
			break;

		case	REM_MODE_AM:
			c = 2;
			break;

		default:
			return -1;
	}
	return simple_command_ic706(myrpt,6,c);
}

/* Set tone encode and decode modes */

int set_ctcss_mode_ic706(struct rpt *myrpt, char txplon, char rxplon)
{
	unsigned char cmdstr[10];
	int rv;

	if(debug > 6)
		ast_log(LOG_NOTICE,"txplon=%i  rxplon=%i \n",txplon,rxplon);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x16;
	cmdstr[5] = 0x42;
	cmdstr[6] = (txplon != 0);
	cmdstr[7] = 0xfd;

	rv = civ_cmd(myrpt,cmdstr,8);
	if (rv) return(-1);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x16;
	cmdstr[5] = 0x43;
	cmdstr[6] = (rxplon != 0);
	cmdstr[7] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,8));
}

#if 0
/* Set transmit and receive ctcss tone frequencies */

int set_ctcss_freq_ic706(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char cmdstr[10];
	char hertz[MAXREMSTR],decimal[MAXREMSTR];
	int h,d,rv;

	memset(cmdstr, 0, 5);

	if(debug > 6)
		ast_log(LOG_NOTICE,"txtone=%s  rxtone=%s \n",txtone,rxtone);

	if(split_ctcss_freq(hertz, decimal, txtone))
		return -1;

	h = atoi(hertz);
	d = atoi(decimal);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x1b;
	cmdstr[5] = 0;
	cmdstr[6] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[7] = ((h % 10) << 4) + (d % 10);
	cmdstr[8] = 0xfd;

	rv = civ_cmd(myrpt,cmdstr,9);
	if (rv) return(-1);

	if (!rxtone) return(0);

	if(split_ctcss_freq(hertz, decimal, rxtone))
		return -1;

	h = atoi(hertz);
	d = atoi(decimal);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x1b;
	cmdstr[5] = 1;
	cmdstr[6] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[7] = ((h % 10) << 4) + (d % 10);
	cmdstr[8] = 0xfd;
	return(civ_cmd(myrpt,cmdstr,9));
}
#endif

int vfo_ic706(struct rpt *myrpt)
{
	unsigned char cmdstr[10];

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 7;
	cmdstr[5] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,6));
}

int mem2vfo_ic706(struct rpt *myrpt)
{
	unsigned char cmdstr[10];

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x0a;
	cmdstr[5] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,6));
}

int select_mem_ic706(struct rpt *myrpt, int slot)
{
	unsigned char cmdstr[10];

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 8;
	cmdstr[5] = 0;
	cmdstr[6] = ((slot / 10) << 4) + (slot % 10);
	cmdstr[7] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,8));
}

int set_ic706(struct rpt *myrpt)
{
	int res = 0,i;

	if(debug)ast_log(LOG_NOTICE, "Set to VFO A iobase=%i\n",myrpt->p.iobase);

	if (!res)
		res = simple_command_ic706(myrpt,7,0);

	if((myrpt->remmode == REM_MODE_FM))
	{
		i = ic706_pltocode(myrpt->rxpl);
		if (i == -1) return -1;
		if(debug)
			printf("Select memory number\n");
		if (!res)
			res = select_mem_ic706(myrpt,i + IC706_PL_MEMORY_OFFSET);
		if(debug)
			printf("Transfer memory to VFO\n");
		if (!res)
			res = mem2vfo_ic706(myrpt);
	}

	if(debug)
		printf("Set to VFO\n");

	if (!res)
		res = vfo_ic706(myrpt);

	if(debug)
		printf("Modulation mode\n");

	if (!res)
		res = set_mode_ic706(myrpt, myrpt->remmode);		/* Modulation mode */

	if(debug)
		printf("Split off\n");

	if(!res)
		simple_command_ic706(myrpt, 0x82,0);			/* Split off */

	if(debug)
		printf("Frequency\n");

	if(!res)
		res = set_freq_ic706(myrpt, myrpt->freq);		/* Frequency */
	if((myrpt->remmode == REM_MODE_FM)){
		if(debug)
			printf("Offset\n");
		if(!res)
			res = set_offset_ic706(myrpt, myrpt->offset);	/* Offset if FM */
		if(!res){
			if(debug)
				printf("CTCSS mode\n");
			res = set_ctcss_mode_ic706(myrpt, myrpt->txplon, myrpt->rxplon); /* CTCSS mode */
		}
	}
	return res;
}

/*
* Bump frequency up or down by a small amount
* Return 0 if the new frequnecy is valid, or -1 if invalid
* Interval is in Hz, resolution is 10Hz
*/

int multimode_bump_freq_ic706(struct rpt *myrpt, int interval)
{
	int m,d;
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	unsigned char cmdstr[20];

	if(debug)
		printf("Before bump: %s\n", myrpt->freq);

	if(split_freq(mhz, decimals, myrpt->freq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	d += (interval / 10); /* 10Hz resolution */
	if(d < 0){
		m--;
		d += 100000;
	}
	else if(d >= 100000){
		m++;
		d -= 100000;
	}

	if(check_freq_ic706(m, d, NULL,myrpt->p.remote_mars)){
		if(debug)
			printf("Bump freq invalid\n");
		return -1;
	}

	snprintf(myrpt->freq, MAXREMSTR, "%d.%05d", m, d);

	if(debug)
		printf("After bump: %s\n", myrpt->freq);

	/* The ic-706 likes packed BCD frequencies */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0;
	cmdstr[5] = ((d % 10) << 4);
	cmdstr[6] = (((d % 1000)/ 100) << 4) + ((d % 100)/10);
	cmdstr[7] = ((d / 10000) << 4) + ((d % 10000)/1000);
	cmdstr[8] = (((m % 100)/10) << 4) + (m % 10);
	cmdstr[9] = (m / 100);
	cmdstr[10] = 0xfd;

	return(serial_remote_io(myrpt,cmdstr,11,NULL,0,0));
}

/*
* XCAT I/O handlers
*/

/* Check to see that the frequency is valid */
/* returns 0 if frequency is valid          */


int check_freq_xcat(int m, int d, int *defmode)
{
	int dflmd = REM_MODE_FM;

	if (m == 144){ /* 2 meters */
		if(d < 10100)
			return -1;
	}
	if (m == 29){ /* 10 meters */
		if(d > 70000)
			return -1;
	}
	else if((m >= 28) && (m < 30)){
		;
	}
	else if((m >= 50) && (m < 54)){
		;
	}
	else if((m >= 144) && (m < 148)){
		;
	}
	else if((m >= 420) && (m < 450)){ /* 70 centimeters */
		;
	}
	else
		return -1;

	if(defmode)
		*defmode = dflmd;


	return 0;
}

int simple_command_xcat(struct rpt *myrpt, char command, char subcommand)
{
	unsigned char cmdstr[10];

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = command;
	cmdstr[5] = subcommand;
	cmdstr[6] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,7));
}

/*
* Set a new frequency for the xcat
*/

int set_freq_xcat(struct rpt *myrpt, char *newfreq)
{
	unsigned char cmdstr[20];
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	int fd,m,d;

	fd = 0;
	if(debug)
		ast_log(LOG_NOTICE,"newfreq:%s\n",newfreq);

	if(split_freq(mhz, decimals, newfreq))
		return -1;

	m = atoi(mhz);
	d = atoi(decimals);

	/* The ic-706 likes packed BCD frequencies */

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 5;
	cmdstr[5] = ((d % 10) << 4);
	cmdstr[6] = (((d % 1000)/ 100) << 4) + ((d % 100)/10);
	cmdstr[7] = ((d / 10000) << 4) + ((d % 10000)/1000);
	cmdstr[8] = (((m % 100)/10) << 4) + (m % 10);
	cmdstr[9] = (m / 100);
	cmdstr[10] = 0xfd;

	return(civ_cmd(myrpt,cmdstr,11));
}

int set_offset_xcat(struct rpt *myrpt, char offset)
{
	unsigned char c,cmdstr[20];
        int mysplit;
        char mhz[MAXREMSTR],decimal[MAXREMSTR];

        if(split_freq(mhz, decimal, myrpt->freq))
                return -1;

        mysplit = myrpt->splitkhz * 1000;
        if (!mysplit)
        {
                if (atoi(mhz) > 400)
                        mysplit = myrpt->p.default_split_70cm * 1000;
                else
                        mysplit = myrpt->p.default_split_2m * 1000;
        }

        cmdstr[0] = cmdstr[1] = 0xfe;
        cmdstr[2] = myrpt->p.civaddr;
        cmdstr[3] = 0xe0;
        cmdstr[4] = 0xaa;
        cmdstr[5] = 0x06;
        cmdstr[6] = mysplit & 0xff;
        cmdstr[7] = (mysplit >> 8) & 0xff;
        cmdstr[8] = (mysplit >> 16) & 0xff;
        cmdstr[9] = (mysplit >> 24) & 0xff;
        cmdstr[10] = 0xfd;

        if (civ_cmd(myrpt,cmdstr,11) < 0) return -1;

	switch(offset){
		case	REM_SIMPLEX:
			c = 0x10;
			break;

		case	REM_MINUS:
			c = 0x11;
			break;

		case	REM_PLUS:
			c = 0x12;
			break;

		default:
			return -1;
	}

	return simple_command_xcat(myrpt,0x0f,c);

}

/* Set transmit and receive ctcss tone frequencies */

int set_ctcss_freq_xcat(struct rpt *myrpt, char *txtone, char *rxtone)
{
	unsigned char cmdstr[10];
	char hertz[MAXREMSTR],decimal[MAXREMSTR];
	int h,d,rv;

	memset(cmdstr, 0, 5);

	if(debug > 6)
		ast_log(LOG_NOTICE,"txtone=%s  rxtone=%s \n",txtone,rxtone);

	if(split_ctcss_freq(hertz, decimal, txtone))
		return -1;

	h = atoi(hertz);
	d = atoi(decimal);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x1b;
	cmdstr[5] = 0;
	cmdstr[6] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[7] = ((h % 10) << 4) + (d % 10);
	cmdstr[8] = 0xfd;

	rv = civ_cmd(myrpt,cmdstr,9);
	if (rv) return(-1);

	if (!rxtone) return(0);

	if(split_ctcss_freq(hertz, decimal, rxtone))
		return -1;

	h = atoi(hertz);
	d = atoi(decimal);

	cmdstr[0] = cmdstr[1] = 0xfe;
	cmdstr[2] = myrpt->p.civaddr;
	cmdstr[3] = 0xe0;
	cmdstr[4] = 0x1b;
	cmdstr[5] = 1;
	cmdstr[6] = ((h / 100) << 4) + (h % 100)/ 10;
	cmdstr[7] = ((h % 10) << 4) + (d % 10);
	cmdstr[8] = 0xfd;
	return(civ_cmd(myrpt,cmdstr,9));
}

int set_xcat(struct rpt *myrpt)
{
	int res = 0;

	/* set Mode */
	if(debug)
		printf("Mode\n");
	if (!res)
		res = simple_command_xcat(myrpt,8,1);
        if(debug)
                printf("Offset Initial/Simplex\n");
        if(!res)
                res = set_offset_xcat(myrpt, REM_SIMPLEX);      /* Offset */
	/* set Freq */
	if(debug)
		printf("Frequency\n");
	if(!res)
		res = set_freq_xcat(myrpt, myrpt->freq);		/* Frequency */
	if(debug)
		printf("Offset\n");
	if(!res)
		res = set_offset_xcat(myrpt, myrpt->offset);	/* Offset */
	if(debug)
		printf("CTCSS\n");
	if (!res)
		res = set_ctcss_freq_xcat(myrpt, myrpt->txplon ? myrpt->txpl : "0.0",
			myrpt->rxplon ? myrpt->rxpl : "0.0"); /* Tx/Rx CTCSS */
	/* set Freq */
	if(debug)
		printf("Frequency\n");
	if(!res)
		res = set_freq_xcat(myrpt, myrpt->freq);		/* Frequency */
	return res;
}


/*
* Dispatch to correct I/O handler
*/
int setrem(struct rpt *myrpt)
{
char	str[300];
char	*offsets[] = {"SIMPLEX","MINUS","PLUS"};
char	*powerlevels[] = {"LOW","MEDIUM","HIGH"};
char	*modes[] = {"FM","USB","LSB","AM"};
int	i,res = -1;

#if	0
printf("FREQ,%s,%s,%s,%s,%s,%s,%d,%d\n",myrpt->freq,
	modes[(int)myrpt->remmode],
	myrpt->txpl,myrpt->rxpl,offsets[(int)myrpt->offset],
	powerlevels[(int)myrpt->powerlevel],myrpt->txplon,
	myrpt->rxplon);
#endif
	if (myrpt->p.archivedir)
	{
		sprintf(str,"FREQ,%s,%s,%s,%s,%s,%s,%d,%d",myrpt->freq,
			modes[(int)myrpt->remmode],
			myrpt->txpl,myrpt->rxpl,offsets[(int)myrpt->offset],
			powerlevels[(int)myrpt->powerlevel],myrpt->txplon,
			myrpt->rxplon);
		donodelog(myrpt,str);
	}
	if (myrpt->remote && myrpt->remote_webtransceiver)
	{
		if (myrpt->remmode == REM_MODE_FM)
		{
			char myfreq[MAXREMSTR],*cp;
			strcpy(myfreq,myrpt->freq);
			cp = strchr(myfreq,'.');
			for(i = strlen(myfreq) - 1; i; i--)
			{
				if (myfreq[i] != '0') break;
				myfreq[i] = 0;
			}
			if (myfreq[0] && (myfreq[strlen(myfreq) - 1] == '.')) strcat(myfreq,"0");
			sprintf(str,"J Remote Frequency\n%s FM\n%s Offset\n",
				(cp) ? myfreq : myrpt->freq,offsets[(int)myrpt->offset]);
			sprintf(str + strlen(str),"%s Power\nTX PL %s\nRX PL %s\n",
				powerlevels[(int)myrpt->powerlevel],
				(myrpt->txplon) ? myrpt->txpl : "Off",
				(myrpt->rxplon) ? myrpt->rxpl : "Off");
		}
		else
		{
			sprintf(str,"J Remote Frequency %s %s\n%s Power\n",
				myrpt->freq,modes[(int)myrpt->remmode],
				powerlevels[(int)myrpt->powerlevel]);
		}
		ast_sendtext(myrpt->remote_webtransceiver,str);
	}
	if(!strcmp(myrpt->remoterig, remote_rig_ft897))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_ft100))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_ft950))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_ic706))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_xcat))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_tm271))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	if(!strcmp(myrpt->remoterig, remote_rig_tmd700))
	{
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	else if(!strcmp(myrpt->remoterig, remote_rig_rbi))
	{
		res = setrbi_check(myrpt);
		if (!res)
		{
			rpt_telemetry(myrpt,SETREMOTE,NULL);
			res = 0;
		}
	}
	else if(ISRIG_RTX(myrpt->remoterig))
	{
		setrtx(myrpt);
		res = 0;
	}
	else if(!strcmp(myrpt->remoterig, remote_rig_kenwood)) {
		rpt_telemetry(myrpt,SETREMOTE,NULL);
		res = 0;
	}
	else
		res = 0;

	if (res < 0) ast_log(LOG_ERROR,"Unable to send remote command on node %s\n",myrpt->name);

	return res;
}

int closerem(struct rpt *myrpt)
{
	if(!strcmp(myrpt->remoterig, remote_rig_ft897))
		return closerem_ft897(myrpt);
	else if(!strcmp(myrpt->remoterig, remote_rig_ft100))
		return closerem_ft100(myrpt);
	else
		return 0;
}

/*
* Dispatch to correct RX frequency checker
*/

int check_freq(struct rpt *myrpt, int m, int d, int *defmode)
{
	if(!strcmp(myrpt->remoterig, remote_rig_ft897))
		return check_freq_ft897(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_ft100))
		return check_freq_ft100(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_ft950))
		return check_freq_ft950(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_ic706))
		return check_freq_ic706(m, d, defmode,myrpt->p.remote_mars);
	else if(!strcmp(myrpt->remoterig, remote_rig_xcat))
		return check_freq_xcat(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_rbi))
		return check_freq_rbi(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_kenwood))
		return check_freq_kenwood(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_tmd700))
		return check_freq_kenwood(m, d, defmode);
	else if(!strcmp(myrpt->remoterig, remote_rig_tm271))
		return check_freq_tm271(m, d, defmode);
	else if(ISRIG_RTX(myrpt->remoterig))
		return check_freq_rtx(m, d, defmode, myrpt);
	else
		return -1;
}

/*
 * Check TX frequency before transmitting
   rv=1 if tx frequency in ok.
*/

char check_tx_freq(struct rpt *myrpt)
{
	int i,rv=0;
	int radio_mhz, radio_decimals, ulimit_mhz, ulimit_decimals, llimit_mhz, llimit_decimals;
	char radio_mhz_char[MAXREMSTR];
	char radio_decimals_char[MAXREMSTR];
	char limit_mhz_char[MAXREMSTR];
	char limit_decimals_char[MAXREMSTR];
	char limits[256];
	char *limit_ranges[40];
	struct ast_variable *limitlist;

	if(debug > 3){
		ast_log(LOG_NOTICE, "myrpt->freq = %s\n", myrpt->freq);
	}

	/* Must have user logged in and tx_limits defined */

	if(!myrpt->p.txlimitsstanzaname || !myrpt->loginuser[0] || !myrpt->loginlevel[0]){
		if(debug > 3){
			ast_log(LOG_NOTICE, "No tx band table defined, or no user logged in. rv=1\n");
		}
		rv=1;
		return 1; /* Assume it's ok otherwise */
	}

	/* Retrieve the band table for the loginlevel */
	limitlist = ast_variable_browse(myrpt->cfg, myrpt->p.txlimitsstanzaname);

	if(!limitlist){
		ast_log(LOG_WARNING, "No entries in %s band table stanza. rv=0\n", myrpt->p.txlimitsstanzaname);
		rv=0;
		return 0;
	}

	split_freq(radio_mhz_char, radio_decimals_char, myrpt->freq);
	radio_mhz = atoi(radio_mhz_char);
	radio_decimals = decimals2int(radio_decimals_char);

	if(debug > 3){
		ast_log(LOG_NOTICE, "Login User = %s, login level = %s\n", myrpt->loginuser, myrpt->loginlevel);
	}

	/* Find our entry */

	for(;limitlist; limitlist=limitlist->next){
		if(!strcmp(limitlist->name, myrpt->loginlevel))
			break;
	}

	if(!limitlist){
		ast_log(LOG_WARNING, "Can't find %s entry in band table stanza %s. rv=0\n", myrpt->loginlevel, myrpt->p.txlimitsstanzaname);
		rv=0;
	    return 0;
	}

	if(debug > 3){
		ast_log(LOG_NOTICE, "Auth: %s = %s\n", limitlist->name, limitlist->value);
	}

	/* Parse the limits */

	strncpy(limits, limitlist->value, 256);
	limits[255] = 0;
	finddelim(limits, limit_ranges, 40);
	for(i = 0; i < 40 && limit_ranges[i] ; i++){
		char range[40];
		char *r,*s;
		strncpy(range, limit_ranges[i], 40);
		range[39] = 0;
        if(debug > 3)
        	ast_log(LOG_NOTICE, "Check %s within %s\n", myrpt->freq, range);

		r = strchr(range, '-');
		if(!r){
			ast_log(LOG_WARNING, "Malformed range in %s tx band table entry. rv=0\n", limitlist->name);
			rv=0;
			break;
		}
		*r++ = 0;
		s = eatwhite(range);
		r = eatwhite(r);
		split_freq(limit_mhz_char, limit_decimals_char, s);
		llimit_mhz = atoi(limit_mhz_char);
		llimit_decimals = decimals2int(limit_decimals_char);
		split_freq(limit_mhz_char, limit_decimals_char, r);
		ulimit_mhz = atoi(limit_mhz_char);
		ulimit_decimals = decimals2int(limit_decimals_char);

		if((radio_mhz >= llimit_mhz) && (radio_mhz <= ulimit_mhz)){
			if(radio_mhz == llimit_mhz){ /* CASE 1: TX freq is in llimit mhz portion of band */
				if(radio_decimals >= llimit_decimals){ /* Cannot be below llimit decimals */
					if(llimit_mhz == ulimit_mhz){ /* If bandwidth < 1Mhz, check ulimit decimals */
						if(radio_decimals <= ulimit_decimals){
							rv=1;
							break;
						}
						else{
							if(debug > 3)
								ast_log(LOG_NOTICE, "Invalid TX frequency, debug msg 1\n");
							rv=0;
							break;
						}
					}
					else{
						rv=1;
						break;
					}
				}
				else{ /* Is below llimit decimals */
					if(debug > 3)
						ast_log(LOG_NOTICE, "Invalid TX frequency, debug msg 2\n");
					rv=0;
					break;
				}
			}
			else if(radio_mhz == ulimit_mhz){ /* CASE 2: TX freq not in llimit mhz portion of band */
				if(radio_decimals <= ulimit_decimals){
					if(debug > 3)
						ast_log(LOG_NOTICE, "radio_decimals <= ulimit_decimals\n");
					rv=1;
					break;
				}
				else{ /* Is above ulimit decimals */
					if(debug > 3)
						ast_log(LOG_NOTICE, "Invalid TX frequency, debug msg 3\n");
					rv=0;
					break;
				}
			}
			else /* CASE 3: TX freq within a multi-Mhz band and ok */
				if(debug > 3)
						ast_log(LOG_NOTICE, "Valid TX freq within a multi-Mhz band and ok.\n");
				rv=1;
				break;
		}
	}
	if(debug > 3)
		ast_log(LOG_NOTICE, "rv=%i\n",rv);

	return rv;
}


/*
* Dispatch to correct frequency bumping function
*/

int multimode_bump_freq(struct rpt *myrpt, int interval)
{
	if(!strcmp(myrpt->remoterig, remote_rig_ft897))
		return multimode_bump_freq_ft897(myrpt, interval);
	else if(!strcmp(myrpt->remoterig, remote_rig_ft950))
		return multimode_bump_freq_ft950(myrpt, interval);
	else if(!strcmp(myrpt->remoterig, remote_rig_ic706))
		return multimode_bump_freq_ic706(myrpt, interval);
	else if(!strcmp(myrpt->remoterig, remote_rig_ft100))
		return multimode_bump_freq_ft100(myrpt, interval);
	else
		return -1;
}


/*
* Queue announcment that scan has been stopped
*/

void stop_scan(struct rpt *myrpt)
{
	myrpt->hfscanstop = 1;
	rpt_telemetry(myrpt,SCAN,0);
}

/*
* This is called periodically when in scan mode
*/


int service_scan(struct rpt *myrpt)
{
	int res, interval;
	char mhz[MAXREMSTR], decimals[MAXREMSTR], k10=0i, k100=0;

	switch(myrpt->hfscanmode){

		case HF_SCAN_DOWN_SLOW:
			interval = -10; /* 100Hz /sec */
			break;

		case HF_SCAN_DOWN_QUICK:
			interval = -50; /* 500Hz /sec */
			break;

		case HF_SCAN_DOWN_FAST:
			interval = -200; /* 2KHz /sec */
			break;

		case HF_SCAN_UP_SLOW:
			interval = 10; /* 100Hz /sec */
			break;

		case HF_SCAN_UP_QUICK:
			interval = 50; /* 500 Hz/sec */
			break;

		case HF_SCAN_UP_FAST:
			interval = 200; /* 2KHz /sec */
			break;

		default:
			myrpt->hfscanmode = 0; /* Huh? */
			return -1;
	}

	res = split_freq(mhz, decimals, myrpt->freq);

	if(!res){
		k100 =decimals[0];
		k10 = decimals[1];
		res = multimode_bump_freq(myrpt, interval);
	}

	if(!res)
		res = split_freq(mhz, decimals, myrpt->freq);


	if(res){
		myrpt->hfscanmode = 0;
		myrpt->hfscanstatus = -2;
		return -1;
	}

	/* Announce 10KHz boundaries */
	if(k10 != decimals[1]){
		int myhund = (interval < 0) ? k100 : decimals[0];
		int myten = (interval < 0) ? k10 : decimals[1];
		myrpt->hfscanstatus = (myten == '0') ? (myhund - '0') * 100 : (myten - '0') * 10;
	} else myrpt->hfscanstatus = 0;
	return res;

}
/*
	retrieve memory setting and set radio
*/
int get_mem_set(struct rpt *myrpt, char *digitbuf)
{
	int res=0;
	if(debug)ast_log(LOG_NOTICE," digitbuf=%s\n", digitbuf);
	res = retrieve_memory(myrpt, digitbuf);
	if(!res)res=setrem(myrpt);
	if(debug)ast_log(LOG_NOTICE," freq=%s  res=%i\n", myrpt->freq, res);
	return res;
}
/*
	steer the radio selected channel to either one programmed into the radio
	or if the radio is VFO agile, to an rpt.conf memory location.
*/
int channel_steer(struct rpt *myrpt, char *data)
{
	int res=0;

	if(debug)ast_log(LOG_NOTICE,"remoterig=%s, data=%s\n",myrpt->remoterig,data);
	if (!myrpt->remoterig) return(0);
	if(data<=0)
	{
		res=-1;
	}
	else
	{
		myrpt->nowchan=strtod(data,NULL);
		if(!strcmp(myrpt->remoterig, remote_rig_ppp16))
		{
			char string[16];
			sprintf(string,"SETCHAN %d ",myrpt->nowchan);
			send_usb_txt(myrpt,string);
		}
		else
		{
			if(get_mem_set(myrpt, data))res=-1;
		}
	}
	if(debug)ast_log(LOG_NOTICE,"nowchan=%i  res=%i\n",myrpt->nowchan, res);
	return res;
}
/*
*/
int channel_revert(struct rpt *myrpt)
{
	int res=0;
	if(debug)ast_log(LOG_NOTICE,"remoterig=%s, nowchan=%02d, waschan=%02d\n",myrpt->remoterig,myrpt->nowchan,myrpt->waschan);
	if (!myrpt->remoterig) return(0);
	if(myrpt->nowchan!=myrpt->waschan)
	{
		char data[8];
        if(debug)ast_log(LOG_NOTICE,"reverting.\n");
		sprintf(data,"%02d",myrpt->waschan);
		myrpt->nowchan=myrpt->waschan;
		channel_steer(myrpt,data);
		res=1;
	}
	return(res);
}






/*
* Remote base function
*/

int function_remote(struct rpt *myrpt, char *param, char *digitbuf, int command_source, struct rpt_link *mylink)
{
	char *s,*s1,*s2;
	int i,j,p,r,ht,k,l,ls2,m,d,offset,offsave, modesave, defmode;
	char multimode = 0;
	char oc,*cp,*cp1,*cp2;
	char tmp[20], freq[20] = "", savestr[20] = "";
	char mhz[MAXREMSTR], decimals[MAXREMSTR];
	union {
		int i;
		void *p;
		char _filler[8];
	} pu;


    if(debug > 6) {
    	ast_log(LOG_NOTICE,"%s param=%s digitbuf=%s source=%i\n",myrpt->name,param,digitbuf,command_source);
	}

	if((!param) || (command_source == SOURCE_RPT) || (command_source == SOURCE_LNK))
		return DC_ERROR;

	p = myatoi(param);
	pu.i = p;

	if ((p != 99) && (p != 5) && (p != 140) && myrpt->p.authlevel &&
		(!myrpt->loginlevel[0])) return DC_ERROR;
	multimode = multimode_capable(myrpt);

	switch(p){

		case 1:  /* retrieve memory */
			if(strlen(digitbuf) < 2) /* needs 2 digits */
				break;

			for(i = 0 ; i < 2 ; i++){
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
			}
		    	r=get_mem_set(myrpt, digitbuf);
			if (r < 0){
				rpt_telemetry(myrpt,MEMNOTFOUND,NULL);
				return DC_COMPLETE;
			}
			else if (r > 0){
				return DC_ERROR;
			}
			return DC_COMPLETE;

		case 2:  /* set freq and offset */


	    		for(i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++){ /* look for M+*K+*O or M+*H+* depending on mode */
				if(digitbuf[i] == '*'){
					j++;
					continue;
				}
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					goto invalid_freq;
				else{
					if(j == 0)
						l++; /* # of digits before first * */
					if(j == 1)
						k++; /* # of digits after first * */
				}
			}

			i = strlen(digitbuf) - 1;
			if(multimode){
				if((j > 2) || (l > 3) || (k > 6))
					goto invalid_freq; /* &^@#! */
 			}
			else{
				if((j > 2) || (l > 4) || (k > 5))
					goto invalid_freq; /* &^@#! */
				if ((!narrow_capable(myrpt)) &&
					(k > 3)) goto invalid_freq;
			}

			/* Wait for M+*K+* */

			if(j < 2)
				break; /* Not yet */

			/* We have a frequency */

			strncpy(tmp, digitbuf ,sizeof(tmp) - 1);

			s = tmp;
			s1 = strsep(&s, "*"); /* Pick off MHz */
			s2 = strsep(&s,"*"); /* Pick off KHz and Hz */
			ls2 = strlen(s2);

			switch(ls2){ /* Allow partial entry of khz and hz digits for laziness support */
				case 1:
					ht = 0;
					k = 100 * atoi(s2);
					break;

				case 2:
					ht = 0;
					k = 10 * atoi(s2);
					break;

				case 3:
					if((!narrow_capable(myrpt)) &&
					  (!multimode))
					{
						if((s2[2] != '0')&&(s2[2] != '5'))
							goto invalid_freq;
					}
					ht = 0;
					k = atoi(s2);
						break;
				case 4:
					k = atoi(s2)/10;
					ht = 10 * (atoi(s2+(ls2-1)));
					break;

				case 5:
					k = atoi(s2)/100;
					ht = (atoi(s2+(ls2-2)));
					break;

				default:
					goto invalid_freq;
			}

			/* Check frequency for validity and establish a default mode */

			snprintf(freq, sizeof(freq), "%s.%03d%02d",s1, k, ht);

 			if(debug)
				ast_log(LOG_NOTICE, "New frequency: %s\n", freq);

			split_freq(mhz, decimals, freq);
			m = atoi(mhz);
			d = atoi(decimals);

			if(check_freq(myrpt, m, d, &defmode)) /* Check to see if frequency entered is legit */
			        goto invalid_freq;

 			if((defmode == REM_MODE_FM) && (digitbuf[i] == '*')) /* If FM, user must enter and additional offset digit */
				break; /* Not yet */


			offset = REM_SIMPLEX; /* Assume simplex */

			if(defmode == REM_MODE_FM){
				oc = *s; /* Pick off offset */

				if (oc){
					switch(oc){
						case '1':
							offset = REM_MINUS;
							break;

						case '2':
							offset = REM_SIMPLEX;
						break;

						case '3':
							offset = REM_PLUS;
							break;

						default:
							goto invalid_freq;
					}
				}
			}
			offsave = myrpt->offset;
			modesave = myrpt->remmode;
			strncpy(savestr, myrpt->freq, sizeof(savestr) - 1);
			strncpy(myrpt->freq, freq, sizeof(myrpt->freq) - 1);
			myrpt->offset = offset;
			myrpt->remmode = defmode;

			if (setrem(myrpt) == -1){
				myrpt->offset = offsave;
				myrpt->remmode = modesave;
				strncpy(myrpt->freq, savestr, sizeof(myrpt->freq) - 1);
				goto invalid_freq;
			}
			if (strcmp(myrpt->remoterig, remote_rig_tm271) &&
			   strcmp(myrpt->remoterig, remote_rig_kenwood))
				rpt_telemetry(myrpt,COMPLETE,NULL);
			return DC_COMPLETE;

invalid_freq:
			rpt_telemetry(myrpt,INVFREQ,NULL);
			return DC_ERROR;

		case 3: /* set rx PL tone */
	    		for(i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++){ /* look for N+*N */
				if(digitbuf[i] == '*'){
					j++;
					continue;
				}
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
				else{
					if(j)
						l++;
					else
						k++;
				}
			}
			if((j > 1) || (k > 3) || (l > 1))
				return DC_ERROR; /* &$@^! */
			i = strlen(digitbuf) - 1;
			if((j != 1) || (k < 2)|| (l != 1))
				break; /* Not yet */
			if(debug)
				printf("PL digits entered %s\n", digitbuf);

			strncpy(tmp, digitbuf, sizeof(tmp) - 1);
			/* see if we have at least 1 */
			s = strchr(tmp,'*');
			if(s)
				*s = '.';
			strncpy(savestr, myrpt->rxpl, sizeof(savestr) - 1);
			strncpy(myrpt->rxpl, tmp, sizeof(myrpt->rxpl) - 1);
			if ((!strcmp(myrpt->remoterig, remote_rig_rbi)) ||
			  (!strcmp(myrpt->remoterig, remote_rig_ft100)))
			{
				strncpy(myrpt->txpl, tmp, sizeof(myrpt->txpl) - 1);
			}
			if (setrem(myrpt) == -1){
				strncpy(myrpt->rxpl, savestr, sizeof(myrpt->rxpl) - 1);
				return DC_ERROR;
			}
			return DC_COMPLETE;

		case 4: /* set tx PL tone */
			/* cant set tx tone on RBI (rx tone does both) */
			if(!strcmp(myrpt->remoterig, remote_rig_rbi))
				return DC_ERROR;
			/* cant set tx tone on ft100 (rx tone does both) */
			if(!strcmp(myrpt->remoterig, remote_rig_ft100))
				return DC_ERROR;
			/*  eventually for the ic706 instead of just throwing the exception
				we can check if we are in encode only mode and allow the tx
				ctcss code to be changed. but at least the warning message is
				issued for now.
			*/
			if(!strcmp(myrpt->remoterig, remote_rig_ic706))
			{
				if(debug)
					ast_log(LOG_WARNING,"Setting IC706 Tx CTCSS Code Not Supported. Set Rx Code for both.\n");
				return DC_ERROR;
			}
	    	for(i = 0, j = 0, k = 0, l = 0 ; digitbuf[i] ; i++){ /* look for N+*N */
				if(digitbuf[i] == '*'){
					j++;
					continue;
				}
				if((digitbuf[i] < '0') || (digitbuf[i] > '9'))
					return DC_ERROR;
				else{
					if(j)
						l++;
					else
						k++;
				}
			}
			if((j > 1) || (k > 3) || (l > 1))
				return DC_ERROR; /* &$@^! */
			i = strlen(digitbuf) - 1;
			if((j != 1) || (k < 2)|| (l != 1))
				break; /* Not yet */
			if(debug)
				printf("PL digits entered %s\n", digitbuf);

			strncpy(tmp, digitbuf, sizeof(tmp) - 1);
			/* see if we have at least 1 */
			s = strchr(tmp,'*');
			if(s)
				*s = '.';
			strncpy(savestr, myrpt->txpl, sizeof(savestr) - 1);
			strncpy(myrpt->txpl, tmp, sizeof(myrpt->txpl) - 1);

			if (setrem(myrpt) == -1){
				strncpy(myrpt->txpl, savestr, sizeof(myrpt->txpl) - 1);
				return DC_ERROR;
			}
			return DC_COMPLETE;


		case 6: /* MODE (FM,USB,LSB,AM) */
			if(strlen(digitbuf) < 1)
				break;

			if(!multimode)
				return DC_ERROR; /* Multimode radios only */

			switch(*digitbuf){
				case '1':
					split_freq(mhz, decimals, myrpt->freq);
					m=atoi(mhz);
					if(m < 29) /* No FM allowed below 29MHz! */
						return DC_ERROR;
					myrpt->remmode = REM_MODE_FM;

					rpt_telemetry(myrpt,REMMODE,NULL);
					break;

				case '2':
					myrpt->remmode = REM_MODE_USB;
					rpt_telemetry(myrpt,REMMODE,NULL);
					break;

				case '3':
					myrpt->remmode = REM_MODE_LSB;
					rpt_telemetry(myrpt,REMMODE,NULL);
					break;

				case '4':
					myrpt->remmode = REM_MODE_AM;
					rpt_telemetry(myrpt,REMMODE,NULL);
					break;

				default:
					return DC_ERROR;
			}

			if(setrem(myrpt))
				return DC_ERROR;
			return DC_COMPLETEQUIET;
		case 99:
			/* cant log in when logged in */
			if (myrpt->loginlevel[0])
				return DC_ERROR;
			*myrpt->loginuser = 0;
			myrpt->loginlevel[0] = 0;
			cp = ast_strdup(param);
			cp1 = strchr(cp,',');
			ast_mutex_lock(&myrpt->lock);
			if (cp1)
			{
				*cp1 = 0;
				cp2 = strchr(cp1 + 1,',');
				if (cp2)
				{
					*cp2 = 0;
					strncpy(myrpt->loginlevel,cp2 + 1,
						sizeof(myrpt->loginlevel) - 1);
				}
				strncpy(myrpt->loginuser,cp1 + 1,sizeof(myrpt->loginuser));
				ast_mutex_unlock(&myrpt->lock);
				if (myrpt->p.archivedir)
				{
					char str[100];

					sprintf(str,"LOGIN,%s,%s",
					    myrpt->loginuser,myrpt->loginlevel);
					donodelog(myrpt,str);
				}
				if (debug)
					printf("loginuser %s level %s\n",myrpt->loginuser,myrpt->loginlevel);
				rpt_telemetry(myrpt,REMLOGIN,NULL);
			}
			ast_free(cp);
			return DC_COMPLETEQUIET;
		case 100: /* RX PL Off */
			myrpt->rxplon = 0;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 101: /* RX PL On */
			myrpt->rxplon = 1;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 102: /* TX PL Off */
			myrpt->txplon = 0;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 103: /* TX PL On */
			myrpt->txplon = 1;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 104: /* Low Power */
			if(!strcmp(myrpt->remoterig, remote_rig_ic706))
				return DC_ERROR;
			myrpt->powerlevel = REM_LOWPWR;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 105: /* Medium Power */
			if(!strcmp(myrpt->remoterig, remote_rig_ic706))
				return DC_ERROR;
			if (ISRIG_RTX(myrpt->remoterig)) return DC_ERROR;
			myrpt->powerlevel = REM_MEDPWR;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 106: /* Hi Power */
			if(!strcmp(myrpt->remoterig, remote_rig_ic706))
				return DC_ERROR;
			myrpt->powerlevel = REM_HIPWR;
			setrem(myrpt);
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 107: /* Bump down 20Hz */
			multimode_bump_freq(myrpt, -20);
			return DC_COMPLETE;
		case 108: /* Bump down 100Hz */
			multimode_bump_freq(myrpt, -100);
			return DC_COMPLETE;
		case 109: /* Bump down 500Hz */
			multimode_bump_freq(myrpt, -500);
			return DC_COMPLETE;
		case 110: /* Bump up 20Hz */
			multimode_bump_freq(myrpt, 20);
			return DC_COMPLETE;
		case 111: /* Bump up 100Hz */
			multimode_bump_freq(myrpt, 100);
			return DC_COMPLETE;
		case 112: /* Bump up 500Hz */
			multimode_bump_freq(myrpt, 500);
			return DC_COMPLETE;
		case 113: /* Scan down slow */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_DOWN_SLOW;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 114: /* Scan down quick */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_DOWN_QUICK;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 115: /* Scan down fast */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_DOWN_FAST;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 116: /* Scan up slow */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_UP_SLOW;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 117: /* Scan up quick */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_UP_QUICK;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 118: /* Scan up fast */
			myrpt->scantimer = REM_SCANTIME;
			myrpt->hfscanmode = HF_SCAN_UP_FAST;
			rpt_telemetry(myrpt,REMXXX,pu.p);
			return DC_COMPLETEQUIET;
		case 119: /* Tune Request */
			if(debug > 3)
				ast_log(LOG_NOTICE,"TUNE REQUEST\n");
			/* if not currently going, and valid to do */
			if((!myrpt->tunerequest) &&
			    ((!strcmp(myrpt->remoterig, remote_rig_ft897)) ||
			    (!strcmp(myrpt->remoterig, remote_rig_ft100)) ||
			    (!strcmp(myrpt->remoterig, remote_rig_ft950)) ||
				(!strcmp(myrpt->remoterig, remote_rig_ic706)) )) {
				myrpt->remotetx = 0;
				if (strncasecmp(myrpt->txchannel->name,
					"Zap/Pseudo",10))
				{
					ast_indicate(myrpt->txchannel,
						AST_CONTROL_RADIO_UNKEY);
				}
				myrpt->tunetx = 0;
				myrpt->tunerequest = 1;
				rpt_telemetry(myrpt,TUNE,NULL);
				return DC_COMPLETEQUIET;
			}
			return DC_ERROR;
		case 5: /* Long Status */
			rpt_telemetry(myrpt,REMLONGSTATUS,NULL);
			return DC_COMPLETEQUIET;
		case 140: /* Short Status */
			rpt_telemetry(myrpt,REMSHORTSTATUS,NULL);
			return DC_COMPLETEQUIET;
		case 200:
		case 201:
		case 202:
		case 203:
		case 204:
		case 205:
		case 206:
		case 207:
		case 208:
		case 209:
		case 210:
		case 211:
		case 212:
		case 213:
		case 214:
		case 215:
			do_dtmf_local(myrpt,remdtmfstr[p - 200]);
			return DC_COMPLETEQUIET;
		default:
			break;
	}
	return DC_INDETERMINATE;
}

