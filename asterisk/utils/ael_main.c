#include "asterisk.h"

#include <sys/types.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <locale.h>
#include <ctype.h>
#include <errno.h>
#include <regex.h>
#include <limits.h>

#include "asterisk/ast_expr.h"
#include "asterisk/channel.h"
#include "asterisk/module.h"
#include "asterisk/app.h"
#include "asterisk/ael_structs.h"

/*** MODULEINFO
  	<depend>pbx_ael</depend>
 ***/

struct namelist
{
	char name[100];
	char name2[100];
	struct namelist *next;
};

struct ast_context 
{
	int extension_count;
	char name[100];
	char registrar[100];
	struct namelist *includes;
	struct namelist *ignorepats;
	struct namelist *switches;
	struct namelist *eswitches;

	struct namelist *includes_last;
	struct namelist *ignorepats_last;
	struct namelist *switches_last;
	struct namelist *eswitches_last;

	struct ast_context *next;
};

#define ADD_LAST(headptr,memptr) if(!headptr){ headptr=(memptr); (headptr##_last)=(memptr);} else {(headptr##_last)->next = (memptr); (headptr##_last) = (memptr);}

void destroy_namelist(struct namelist *x);
void destroy_namelist(struct namelist *x)
{
	struct namelist *z,*z2;
	for(z=x; z; z = z2)
	{
		z2 = z->next;
		z->next = 0;
		free(z);
	}
}

struct namelist *create_name(const char *name);
struct namelist *create_name(const char *name)
{
	struct namelist *x = calloc(1, sizeof(*x));
	if (!x)
		return NULL;
	strncpy(x->name, name, sizeof(x->name) - 1);
	return x;
}

struct ast_context *context_list;
struct ast_context *last_context;
struct namelist *globalvars;
struct namelist *globalvars_last;

int conts=0, extens=0, priors=0;
char last_exten[18000];
char ast_config_AST_CONFIG_DIR[PATH_MAX];
char ast_config_AST_VAR_DIR[PATH_MAX];

void ast_cli_register_multiple(void);
int ast_add_extension2(struct ast_context *con,
					   int replace, const char *extension, int priority, const char *label, const char *callerid,
						const char *application, void *data, void (*datad)(void *),
					   const char *registrar);
void pbx_builtin_setvar(void *chan, void *data);
struct ast_context * ast_context_create(void **extcontexts, const char *name, const char *registrar);
struct ast_context * ast_context_find_or_create(void **extcontexts, const char *name, const char *registrar);
void ast_context_add_ignorepat2(struct ast_context *con, const char *value, const char *registrar);
void ast_context_add_include2(struct ast_context *con, const char *value, const char *registrar);
void ast_context_add_switch2(struct ast_context *con, const char *value, const char *data, int eval, const char *registrar);
void ast_merge_contexts_and_delete(void);
void ast_context_verify_includes(void);
struct ast_context * ast_walk_contexts(void);
void ast_cli_unregister_multiple(void);
void ast_context_destroy(void);
void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...);
char *ast_process_quotes_and_slashes(char *start, char find, char replace_with);
void ast_verbose(const char *fmt, ...);
struct ast_app *pbx_findapp(const char *app);
void filter_leading_space_from_exprs(char *str);
void filter_newlines(char *str);
static int quiet = 0;
static int no_comp = 0;
static int use_curr_dir = 0;
static int dump_extensions = 0;
static int FIRST_TIME = 0;
static FILE *dumpfile;

struct ast_app *pbx_findapp(const char *app)
{
	return (struct ast_app*)1; /* so as not to trigger an error */
}

int ast_loader_register(int (*updater)(void))
{
	return 1;
}

int ast_loader_unregister(int (*updater)(void))
{
	return 1;
}
void ast_module_register(const struct ast_module_info *x)
{
}

void ast_module_unregister(const struct ast_module_info *x)
{
}


void ast_cli_register_multiple(void)
{
	if(!no_comp)
        	printf("Executed ast_cli_register_multiple();\n");
}

void pbx_substitute_variables_helper(struct ast_channel *c,const char *cp1,char *cp2,int count);
void pbx_substitute_variables_helper(struct ast_channel *c,const char *cp1,char *cp2,int count)
{
	if (cp1 && *cp1)
		strncpy(cp2,cp1,AST_MAX_EXTENSION); /* Right now, this routine is ONLY being called for 
											   a possible var substitution on extension names,
											   so....! */
	else
		*cp2 = 0;
}

int ast_add_extension2(struct ast_context *con,
			int replace, const char *extension, int priority, const char *label, const char *callerid,
			const char *application, void *data, void (*datad)(void *),
			const char *registrar)
{
	priors++;
	con->extension_count++;
	if (strcmp(extension,last_exten) != 0) {
		extens++;
		strcpy(last_exten, extension);
	}
	if (!label) {
		label = "(null)";
	}
	if (!callerid) {
		callerid = "(null)";
	}
	if (!application) {
		application = "(null)";
	}

	if(!no_comp)
		printf("Executed ast_add_extension2(context=%s, rep=%d, exten=%s, priority=%d, label=%s, callerid=%s, appl=%s, data=%s, FREE, registrar=%s);\n",
			   con->name, replace, extension, priority, label, callerid, application, (data?(char*)data:"(null)"), registrar);

	if( dump_extensions && dumpfile ) {
		struct namelist *n;
		char *data2,*data3=0;
		int commacount = 0;

		if( FIRST_TIME ) {
			FIRST_TIME = 0;
			
			if( globalvars )
				fprintf(dumpfile,"[globals]\n");
			
			for(n=globalvars;n;n=n->next) {
				fprintf(dumpfile, "%s\n", n->name);
			}
		}
		
		/* print out each extension , possibly the context header also */
		if( con != last_context ) {
			fprintf(dumpfile,"\n\n[%s]\n", con->name);
			last_context = con;
			for(n=con->ignorepats;n;n=n->next) {
				fprintf(dumpfile, "ignorepat => %s\n", n->name);
			}
			for(n=con->includes;n;n=n->next) {
				fprintf(dumpfile, "include => %s\n", n->name);
			}
			for(n=con->switches;n;n=n->next) {
				fprintf(dumpfile, "switch => %s/%s\n", n->name, n->name2);
			}
			for(n=con->eswitches;n;n=n->next) {
				fprintf(dumpfile, "eswitch => %s/%s\n", n->name, n->name2);
			}
			
		}
		if( data ) {
			filter_newlines((char*)data);
			filter_leading_space_from_exprs((char*)data);

			/* compiling turns commas into vertical bars in the app data, and also removes the backslash from before escaped commas;
			   we have to restore the escaping backslash in front of any commas; the vertical bars are OK to leave as-is */
			for (data2 = data; *data2; data2++) {
				if (*data2 == ',')
					commacount++;  /* we need to know how much bigger the string will grow-- one backslash for each comma  */
			}
			if (commacount) 
			{
				char *d3,*d4;
				
				data2 = (char*)malloc(strlen(data)+commacount+1);
				data3 = data;
				d3 = data;
				d4 = data2;
				while (*d3) {
					if (*d3 == ',') {
						*d4++ = '\\'; /* put a backslash in front of each comma */
						*d4++ = *d3++;
					} else
						*d4++ = *d3++;  /* or just copy the char */
				}
				*d4++ = 0;  /* cap off the new string */
				data = data2;
			} else
				data2 = 0;
			
			if( strcmp(label,"(null)") != 0  )
				fprintf(dumpfile,"exten => %s,%d(%s),%s(%s)\n", extension, priority, label, application, (char*)data);
			else
				fprintf(dumpfile,"exten => %s,%d,%s(%s)\n", extension, priority, application, (char*)data);

			if (data2) {
				free(data2);
				data2 = 0;
				data = data3; /* restore data to pre-messedup state */
			}

		} else {

			if( strcmp(label,"(null)") != 0  )
				fprintf(dumpfile,"exten => %s,%d(%s),%s\n", extension, priority, label, application);
			else
				fprintf(dumpfile,"exten => %s,%d,%s\n", extension, priority, application);
		}
	}
	
	/* since add_extension2 is responsible for the malloc'd data stuff */
	if( data )
		free(data);
	return 0;
}

void pbx_builtin_setvar(void *chan, void *data)
{
	struct namelist *x = create_name(data);
	if(!no_comp)
		printf("Executed pbx_builtin_setvar(chan, data=%s);\n", (char*)data);

	if( dump_extensions ) {
		x = create_name(data);
		ADD_LAST(globalvars,x);
	}
}
	

struct ast_context * ast_context_create(void **extcontexts, const char *name, const char *registrar)
{
	struct ast_context *x = calloc(1, sizeof(*x));
	if (!x)
		return NULL;
	x->next = context_list;
	context_list = x;
	if (!no_comp)
		printf("Executed ast_context_create(conts, name=%s, registrar=%s);\n", name, registrar);
	conts++;
	strncpy(x->name, name, sizeof(x->name) - 1);
	strncpy(x->registrar, registrar, sizeof(x->registrar) - 1);
	return x;
}

struct ast_context * ast_context_find_or_create(void **extcontexts, const char *name, const char *registrar)
{
	struct ast_context *x = calloc(1, sizeof(*x));
	if (!x)
		return NULL;
	x->next = context_list;
	context_list = x;
	if (!no_comp)
		printf("Executed ast_context_find_or_create(conts, name=%s, registrar=%s);\n", name, registrar);
	conts++;
	strncpy(x->name, name, sizeof(x->name) - 1);
	strncpy(x->registrar, registrar, sizeof(x->registrar) - 1);
	return x;
}

void ast_context_add_ignorepat2(struct ast_context *con, const char *value, const char *registrar)
{
	if(!no_comp)
		printf("Executed ast_context_add_ignorepat2(con, value=%s, registrar=%s);\n", value, registrar);
	if( dump_extensions ) {
		struct namelist *x;
		x = create_name(value);
		ADD_LAST(con->ignorepats,x);
	}
}

void ast_context_add_include2(struct ast_context *con, const char *value, const char *registrar)
{
	if(!no_comp)
		printf("Executed ast_context_add_include2(con, value=%s, registrar=%s);\n", value, registrar);
	if( dump_extensions ) {
		struct namelist *x;
		x = create_name((char*)value);
		ADD_LAST(con->includes,x);
	}
}

void ast_context_add_switch2(struct ast_context *con, const char *value, const char *data, int eval, const char *registrar)
{
	if(!no_comp)
		printf("Executed ast_context_add_switch2(con, value=%s, data=%s, eval=%d, registrar=%s);\n", value, data, eval, registrar);
	if( dump_extensions ) {
		struct namelist *x;
		x = create_name((char*)value);
		strncpy(x->name2,data,100);
		if( eval ) {

			ADD_LAST(con->switches,x);

		} else {

			ADD_LAST(con->eswitches,x);
		}
	}
}

void ast_merge_contexts_and_delete(void)
{
	if(!no_comp)
		printf("Executed ast_merge_contexts_and_delete();\n");
}

void ast_context_verify_includes(void)
{
	if(!no_comp)
		printf("Executed ast_context_verify_includes();\n");
}

struct ast_context * ast_walk_contexts(void)
{
	if(!no_comp)
		printf("Executed ast_walk_contexts();\n");
	return 0;
}

void ast_cli_unregister_multiple(void)
{
	if(!no_comp)
		printf("Executed ast_cli_unregister_multiple();\n");
}

void ast_context_destroy(void)
{
	if( !no_comp)
		printf("Executed ast_context_destroy();\n");
}

void ast_log(int level, const char *file, int line, const char *function, const char *fmt, ...)
{
        va_list vars;
        va_start(vars,fmt);
	if( !quiet || level > 2 ) {
    	    printf("LOG: lev:%d file:%s  line:%d func: %s  ",
                   level, file, line, function);
            vprintf(fmt, vars);
            fflush(stdout);
            va_end(vars);
	}
}

void ast_verbose(const char *fmt, ...)
{
        va_list vars;
        va_start(vars,fmt);

        printf("VERBOSE: ");
        vprintf(fmt, vars);
        fflush(stdout);
        va_end(vars);
}

char *ast_process_quotes_and_slashes(char *start, char find, char replace_with)
{
        char *dataPut = start;
        int inEscape = 0;
        int inQuotes = 0;

        for (; *start; start++) {
                if (inEscape) {
                        *dataPut++ = *start;       /* Always goes verbatim */
                        inEscape = 0;
                } else {
                        if (*start == '\\') {
                                inEscape = 1;      /* Do not copy \ into the data */
                        } else if (*start == '\'') {
                                inQuotes = 1-inQuotes;   /* Do not copy ' into the data */
                        } else {
                                /* Replace , with |, unless in quotes */
                                *dataPut++ = inQuotes ? *start : ((*start==find) ? replace_with : *start);
                        }
                }
        }
        if (start != dataPut)
                *dataPut = 0;
        return dataPut;
}

void filter_leading_space_from_exprs(char *str)
{
	/*  Mainly for aesthetics */
	char *t, *v, *u = str;
	
	while ( u && *u ) {

		if( *u == '$' && *(u+1) == '[' ) {
			t = u+2;
			while( *t == '\n' || *t == '\r' || *t == '\t' || *t == ' ' ) {
				v = t;
				while ( *v ) {
					*v = *(v+1);
					v++;
				}
			}
		}
		
		u++;
	}
}

void filter_newlines(char *str)
{
	/* remove all newlines, returns  */
	char *t=str;
	while( t && *t ) {
		if( *t == '\n' || *t == '\r' ) {
			*t = ' '; /* just replace newlines and returns with spaces; they act as
						 token separators, and just blindly removing them could be
						 harmful. */
		}
		t++;
	}
}


extern struct module_symbols mod_data;
extern int ael_external_load_module(void);

int main(int argc, char **argv)
{
	int i;
	struct namelist *n;
	struct ast_context *lp,*lp2;
	
	for(i=1;i<argc;i++) {
		if( argv[i][0] == '-' && argv[i][1] == 'n' )
			no_comp =1;
		if( argv[i][0] == '-' && argv[i][1] == 'q' ) {
			quiet = 1;
			no_comp =1;
		}
		if( argv[i][0] == '-' && argv[i][1] == 'd' )
			use_curr_dir =1;
		if( argv[i][0] == '-' && argv[i][1] == 'w' )
			dump_extensions =1;
	}
	
	if( !quiet ) {
		printf("\n(If you find progress and other non-error messages irritating, you can use -q to suppress them)\n");
		if( !no_comp )
			printf("\n(You can use the -n option if you aren't interested in seeing all the instructions generated by the compiler)\n\n");
		if( !use_curr_dir )
			printf("\n(You can use the -d option if you want to use the current working directory as the CONFIG_DIR. I will look in this dir for extensions.ael* and its included files)\n\n");
		if( !dump_extensions )
			printf("\n(You can use the -w option to dump extensions.conf format to extensions.conf.aeldump)\n");
	}

	if( use_curr_dir ) {
		strcpy(ast_config_AST_CONFIG_DIR, ".");
	}
	else {
		strcpy(ast_config_AST_CONFIG_DIR, "/etc/asterisk");
	}
	strcpy(ast_config_AST_VAR_DIR, "/var/lib/asterisk");
	
	if( dump_extensions ) {
		dumpfile = fopen("extensions.conf.aeldump","w");
		if( !dumpfile ) {
			printf("\n\nSorry, cannot open extensions.conf.aeldump for writing! Correct the situation and try again!\n\n");
			exit(10);
		}
		
	}

	FIRST_TIME = 1;
	
	ael_external_load_module();
	
	ast_log(4, "ael2_parse", __LINE__, "main", "%d contexts, %d extensions, %d priorities\n", conts, extens, priors);

	if( dump_extensions && dumpfile ) {
	
		for( lp = context_list; lp; lp = lp->next ) { /* print out any contexts that didn't have any
														 extensions in them */
			if( lp->extension_count == 0 ) {
				
				fprintf(dumpfile,"\n\n[%s]\n", lp->name);
				
				for(n=lp->ignorepats;n;n=n->next) {
					fprintf(dumpfile, "ignorepat => %s\n", n->name);
				}
				for(n=lp->includes;n;n=n->next) {
					fprintf(dumpfile, "include => %s\n", n->name);
				}
				for(n=lp->switches;n;n=n->next) {
					fprintf(dumpfile, "switch => %s/%s\n", n->name, n->name2);
				}
				for(n=lp->eswitches;n;n=n->next) {
					fprintf(dumpfile, "eswitch => %s/%s\n", n->name, n->name2);
				}
			}
		}
	}
	
	if( dump_extensions && dumpfile )
		fclose(dumpfile);
	
	for( lp = context_list; lp; lp = lp2 ) { /* free the ast_context structs */
		lp2 = lp->next;
		lp->next = 0;

		destroy_namelist(lp->includes);
		destroy_namelist(lp->ignorepats);
		destroy_namelist(lp->switches);
		destroy_namelist(lp->eswitches);

		free(lp);
	}
	
    return 0;
}
