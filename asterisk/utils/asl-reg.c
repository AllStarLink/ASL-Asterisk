/*
 * Allstar Registration API Client
 *
 * Copyright (C) 2019, AllStarLink, Inc
 *
 * Author: Adam Paul, KC1KCC <adamjpaul@gmail.com>
 *
 * Based on asl-reg.php by Tim Sawyer, WD6AWP
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

#define MAX_RESPONSE_LENGTH 8192
#define MAX_REQUEST_LENGTH 4096
#define MAX_MESSAGES 50
#define DEFAULT_BIND_PORT "4596"

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <curl/curl.h>
#include <syslog.h>

struct node {
	char num[10];
	char pass[256];
	int rbase;
};

struct MemoryStruct {
	char *memory;
	size_t size;
};

static char server[256] = "";
static int usesyslog = 0;
static struct node nodelist[10];
static int nodecount = 0;
static char bindport[6] = DEFAULT_BIND_PORT;


static int load_config(char* config)
{
	/* config may be in the following formats: 
	 * setting=value
	 * setting value
	 * setting = value
	 * [section]
	 * node:pass,?
	 */

	FILE *f;
	char buf[1024];
	char *val;
	char *val2;
	char section = '\0';
	int lineno=0;
	f = fopen(config, "r");
	if (!f) {
		fprintf(stderr, "Unable to open config file '%s': %s\n", config, strerror(errno));
		return -1;
	}
	while(!feof(f)) {
		fgets(buf, sizeof(buf), f);
		if (!feof(f)) {
			lineno++;
			val = strchr(buf, '#');
			val2 = strchr(buf, ';');
			// find first occurrence of comment character
			if(val == 0 || val2 < val) val = val2;
			// terminate string at comment character
			if (val) *val = '\0';
			// terminate string before any trailing whitespace
			while(strlen(buf) && (buf[strlen(buf) - 1] < 33))
				buf[strlen(buf) - 1] = '\0';
			// if there's nothing left in the buffer, go to the next line
			if (!strlen(buf))
				continue;
			val = buf;
			// step up to first whitespace or =
			while(*val) {
				if (*val < 33 || *val == '=')
					break;
				val++;
			}
			// terminate buf and swallow whitespace
			if (*val) {
				*val = '\0';
				val++;
				//step past whitespace
				while(*val && (*val < 33)) val++;
			}
			// step past =
			if (*val == '=') {
				val++;
				//step past whitespace
				while(*val && (*val < 33)) val++;
			} /*else
				*val = '\0';*/
			if (!strcasecmp(buf, "[general]")) {
				section = 'G';
			} else if (!strcasecmp(buf, "[nodes]")) {
				section = 'N';
			} else if (*buf == '[') {
				section = '\0';
			} else if (section == 'G' && !strcasecmp(buf, "server")) {
				if (val && strlen(val))
					strncpy(server, val, sizeof(server) - 1);
				else
					fprintf(stderr, "server value missing at line %d of %s\n", lineno, config);
			} else if (section == 'G' && !strcasecmp(buf, "syslog")) {
				if (val && strlen(val)) {
					if (*val == 'y')  usesyslog = 1;
				}
				else
					fprintf(stderr, "syslog value missing (y/n) at line %d of %s\n", lineno, config);
			} else if (section == 'N') {
				if( nodecount < sizeof(nodelist)/sizeof(nodelist[0]) ) {
					val = strchr(buf, ':');
					if (val) {
						*val = '\0';
						val++;
						val2 = strchr(val, ',');
						if (val2) {
							*val2 = '\0';
							val2++;
							if (*val2 == 'y') nodelist[nodecount].rbase = 1;
						}
						strncpy(nodelist[nodecount].pass, val, sizeof(((struct node*)0)->pass) - 1);
					}
					if( !val || !strlen(val) ) {
						fprintf(stderr, "Error: node needs a password at line %d of %s\n", lineno, config);
						exit(3);
					}
					strncpy(nodelist[nodecount++].num, buf, sizeof(((struct node*)0)->num) - 1);
				} else
					fprintf(stderr, "nodelist too long in %s. ignoring line %d\n", config, lineno);
			} else if (!strcasecmp(buf, "bindport")) {
				if (val && strlen(val))
					strncpy(bindport, val, sizeof(bindport) - 1);
			}
		}
	}
	fclose(f);
	return 0;
}

static void generatePost( char **post ) {
	strncpy(*post, "data={\"nodes\":{", MAX_REQUEST_LENGTH - 1);
	int i = 0;
	while( i<nodecount ) {
		if( i>0 ) strcat(*post, ",");
		strncat(*post, "\"", MAX_REQUEST_LENGTH - strlen(*post) - 1);
		strncat(*post, nodelist[i].num, MAX_REQUEST_LENGTH - strlen(*post) - 1);
		strncat(*post, "\": {\"node\":\"", MAX_REQUEST_LENGTH - strlen(*post) - 1);
		strncat(*post, nodelist[i].num, MAX_REQUEST_LENGTH - strlen(*post) - 1);
		strncat(*post, "\",\"passwd\":\"", MAX_REQUEST_LENGTH - strlen(*post) - 1);
		strncat(*post, nodelist[i].pass, MAX_REQUEST_LENGTH - strlen(*post) - 1);
		strncat(*post, "\",\"remote\":", MAX_REQUEST_LENGTH - strlen(*post) - 1);
		strncat(*post, nodelist[i].rbase ? "1" : "0", MAX_REQUEST_LENGTH - strlen(*post) - 1);
		strncat(*post, "}", MAX_REQUEST_LENGTH - strlen(*post) - 1);
		i++;
	}
	if( strlen(*post) > MAX_REQUEST_LENGTH - 2 ) {
	       fprintf(stderr, "buffer overflow while generating request\n");
       	       exit(6);
	}
	strncat(*post,"}}", - strlen(*post) - 1);
}

static size_t WriteMemoryCallback(void *contents, size_t size, size_t nmemb, void *userp) {
	size_t realsize = size * nmemb;
	struct MemoryStruct *mem = (struct MemoryStruct *)userp;
 
	char *ptr = realloc(mem->memory, mem->size + realsize + 1);
	if(!ptr) {
		/* out of memory! */ 
		if(usesyslog) syslog(LOG_INFO, "non enough memory (realloc returned NULL)\n");
		fprintf(stderr,"not enough memory (realloc returned NULL)\n");
		return 0;
	}
 
	mem->memory = ptr;
	memcpy(&(mem->memory[mem->size]), contents, realsize);
	mem->size += realsize;
	mem->memory[mem->size] = 0;
 
	return realsize;
}

static void registerNodes(char **response, int *rescode) {
	char *request = (char*) malloc(MAX_REQUEST_LENGTH);
	generatePost(&request);
	printf( "request: %s\n", request);
	CURL *curl;
	struct MemoryStruct chunk;
	
	curl = curl_easy_init();
	if(curl) {
		chunk.memory = malloc(1);  /* will be grown as needed by realloc above */
		chunk.size = 0;    /* no data at this point */
		curl_easy_setopt(curl, CURLOPT_URL, server);
		curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, WriteMemoryCallback);
		curl_easy_setopt(curl, CURLOPT_WRITEDATA, (void *)&chunk);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDS, request);
		curl_easy_setopt(curl, CURLOPT_POSTFIELDSIZE, -1L);
		curl_easy_setopt(curl, CURLOPT_HEADER, 0L);
		curl_easy_setopt(curl, CURLOPT_NOBODY, 0L);
		curl_easy_setopt(curl, CURLOPT_POST, 1L);
		//curl_easy_setopt(curl, CURLOPT_VERBOSE, 1L);
		curl_easy_perform(curl);
		curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, rescode);
		
		//printf( "curl rescode: %d\n", *rescode );
		//printf( "curl: %s\n", chunk.memory);
		if(strlen(chunk.memory)>MAX_RESPONSE_LENGTH) {
			fprintf(stderr, "buffer overflow while retrieving response from server\n");
			exit(7);
		}	
		strcpy(*response,chunk.memory);
		curl_easy_cleanup(curl);
		curl_global_cleanup();
		free(chunk.memory);
		free(request);
	}
	
}

static int getMessages(char *response, char **messages) {
	//{"data":["Node number is has non digits.","Node number is has non digits."]}
	char *ptr;
	char *ptr2;
	// remove leading whitepace
	ptr = response;
	while(*ptr < 33) ptr++; 
	ptr2 = strchr(ptr,':');
	if(ptr2) *ptr2 = '\0';
	if(strcasecmp(ptr, "{\"data\""))
		return -1;
	ptr2++;
	// remove leading whitespace
	while(*ptr2 < 33) ptr2++;
	if(!*ptr2 == '[') return -1;
	ptr2++;
	int i = 0;
	while(1) {
		// remove leading whitespace
		while(*ptr2 < 33) ptr2++;
		if(!*ptr2 == '"') return -1;
		ptr2++;
		ptr = strchr(ptr2,'"');
		if(ptr) {
		       	*ptr = '\0';
			//printf("ptr2: %s\n", ptr2);
			//printf("i: %d\n", i);
			messages[i] = malloc(ptr-ptr2);
			strcpy(messages[i], ptr2);
			//printf("ptr2: %s\n", ptr2);
			//printf("messages[%d]: %s\n", i, ptr2);
			i++;
		}
		ptr++;
		ptr2 = strchr(ptr, ',');
		if(!ptr2) break;
		ptr2++;
		if(i>MAX_MESSAGES) return -1;
	}
	return i;
}


int main(int argc, char *argv[])
{
	int delay = 240;
	time_t t;
	srand((unsigned) time(&t));
	int i = 0;
	while( i< argc ){
		if (!strcasecmp(argv[i], "-d"))
			delay = 0;
		i++;
	}
	load_config("/etc/asterisk/iax.conf");
	load_config("/etc/asterisk/asl-reg.conf");
	/*
	printf("server: '%s'\n", server);
	printf("syslog: %d\n\n", usesyslog);
	int i = 0;
	while( i<nodecount ) {
		printf("nodenum: '%s'\n", nodelist[i].num);
		printf("nodepass: '%s'\n", nodelist[i].pass);
		printf("noderbase: %d\n\n", nodelist[i].rbase);
		i++;
	}
	printf("bindport: '%s'\n", bindport);
	*/

	if( !strlen(server) ) {
		if(usesyslog) syslog(LOG_INFO, "Error: no server url provided\n");
		fprintf(stderr, "Error: no server url provided\n");
		exit(1);
	}
	if( !nodecount ) {
		if(usesyslog) syslog(LOG_INFO, "Error: no nodes defined\n");
		fprintf(stderr, "Error: no nodes defined\n");
		exit(2);
	}

	// sleep for a random period up to delay seconds
	if( delay ) {
		int d = rand() % delay;
		fprintf(stdout, "Waiting for %d seconds...\n", d);
		sleep(d);
	}

	//check if asterisk is running
	if( access( "/var/run/asterisk.ctl", F_OK ) == -1 ) {
		if(usesyslog) syslog(LOG_INFO, "Error: asterisk is not running\n");
		fprintf(stderr, "Error: asterisk is not running\n");
		exit(4);
	}
	char *response = (char*) malloc(MAX_RESPONSE_LENGTH);
	int rescode = 0;
	registerNodes(&response, &rescode);
	//printf("fullresponse: %s\n", response);
	if(usesyslog) syslog(LOG_INFO, "Registration sent\n");

	if( rescode != 200 ) {
		if(usesyslog) syslog(LOG_INFO,"Aborted with http code: %d\n", rescode);
		fprintf(stderr,"Aborted with http code: %d\n", rescode);
		exit(5);
	}

	char *messages[MAX_MESSAGES]; 

	int m = getMessages(response, messages);

	free(response);

	//printf("rescode: %d\n", rescode);
	
	if( m<0 )
	{		
		if(usesyslog) syslog(LOG_INFO,"Error retrieving messages from server.  Some or all messages may have been lost\n");
		fprintf(stderr,"Error retrieving messages from server.  Some or all messages may have been lost\n");
	} else {
		int i = 0;
		while( i<m ) {
			if(usesyslog) syslog(LOG_INFO,"response: %s\n", messages[i]);
			fprintf(stdout, "response: %s\n", messages[i]);
			i++;
		}
	}
	
	exit(0);
}
