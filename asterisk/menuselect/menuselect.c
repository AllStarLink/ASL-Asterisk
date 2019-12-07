/*
 * Asterisk -- An open source telephony toolkit.
 *
 * Copyright (C) 2005 - 2006, Russell Bryant
 *
 * Russell Bryant <russell@digium.com>
 *
 * See http://www.asterisk.org for more information about
 * the Asterisk project. Please do not directly contact
 * any of the maintainers of this project for assistance;
 * the project provides a web site, mailing lists and IRC
 * channels for your use.
 *
 * This program is free software, distributed under the terms of
 * the GNU General Public License Version 2. See the LICENSE file
 * at the top of the source tree.
 */

/*!
 * \file
 *
 * \author Russell Bryant <russell@digium.com>
 * 
 * \brief A menu-driven system for Asterisk module selection
 */

#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <stdarg.h>

#include "mxml/mxml.h"
#include "linkedlists.h"
#include "menuselect.h"

#undef MENUSELECT_DEBUG
#ifdef MENUSELECT_DEBUG
static FILE *debug;
#endif

/*! The list of categories */
struct categories categories = AST_LIST_HEAD_NOLOCK_INIT_VALUE;

/*!
   We have to maintain a pointer to the root of the trees generated from reading
   the build options XML files so that we can free it when we're done.  We don't
   copy any of the information over from these trees. Our list is just a 
   convenient mapping to the information contained in these lists with one
   additional piece of information - whether the build option is enabled or not.
*/
struct tree {
	/*! the root of the tree */
	mxml_node_t *root;
	/*! for linking */
	AST_LIST_ENTRY(tree) list;
};

/*! The list of trees from menuselect-tree files */
static AST_LIST_HEAD_NOLOCK_STATIC(trees, tree);

static const char * const tree_files[] = {
	"menuselect-tree"
};

static char *output_makeopts = OUTPUT_MAKEOPTS_DEFAULT;
static char *output_makedeps = OUTPUT_MAKEDEPS_DEFAULT;

/*! This is set to 1 if menuselect.makeopts pre-existed the execution of this app */
static int existing_config = 0;

/*! This is set when the --check-deps argument is provided. */
static int check_deps = 0;

/*! This variable is non-zero when any changes are made */
int changes_made = 0;

/*! Menu name */
const char *menu_name = "Menuselect";

/*! Global list of dependencies that are external to the tree */
struct dep_file {
	char name[32];
	int met;
	AST_LIST_ENTRY(dep_file) list;
} *dep_file;
AST_LIST_HEAD_NOLOCK_STATIC(deps_file, dep_file);

#if !defined(ast_strdupa) && defined(__GNUC__)
#define ast_strdupa(s)                                                    \
	(__extension__                                                    \
	({                                                                \
		const char *__old = (s);                                  \
		size_t __len = strlen(__old) + 1;                         \
		char *__new = __builtin_alloca(__len);                    \
		memcpy (__new, __old, __len);                             \
		__new;                                                    \
	}))
#endif

/*! \brief return a pointer to the first non-whitespace character */
static inline char *skip_blanks(char *str)
{
	if (!str)
		return NULL;

	while (*str && *str < 33)
		str++;

	return str;
}

static void print_debug(const char *format, ...)
{
#ifdef MENUSELECT_DEBUG
	va_list ap;

	va_start(ap, format);
	vfprintf(debug, format, ap);
	va_end(ap);

	fflush(debug);
#endif
}

/*! \brief Add a category to the category list, ensuring that there are no duplicates */
static struct category *add_category(struct category *cat)
{
	struct category *tmp;

	AST_LIST_TRAVERSE(&categories, tmp, list) {
		if (!strcmp(tmp->name, cat->name)) {
			return tmp;
		}
	}
	AST_LIST_INSERT_TAIL(&categories, cat, list);

	return cat;
}

/*! \brief Add a member to the member list of a category, ensuring that there are no duplicates */
static int add_member(struct member *mem, struct category *cat)
{
	struct member *tmp;

	AST_LIST_TRAVERSE(&cat->members, tmp, list) {
		if (!strcmp(tmp->name, mem->name)) {
			fprintf(stderr, "Member '%s' already exists in category '%s', ignoring.\n", mem->name, cat->name);
			return -1;
		}
	}
	AST_LIST_INSERT_TAIL(&cat->members, mem, list);

	return 0;
}

/*! \brief Free a member structure and all of its members */
static void free_member(struct member *mem)
{
	struct depend *dep;
	struct conflict *cnf;
	struct use *use;

	while ((dep = AST_LIST_REMOVE_HEAD(&mem->deps, list)))
		free(dep);
	while ((cnf = AST_LIST_REMOVE_HEAD(&mem->conflicts, list)))
		free(cnf);
	while ((use = AST_LIST_REMOVE_HEAD(&mem->uses, list)))
		free(use);
	free(mem);
}

/*! \brief Parse an input makeopts file */
static int parse_tree(const char *tree_file)
{
	FILE *f;
	struct tree *tree;
	struct member *mem;
	struct depend *dep;
	struct conflict *cnf;
	struct use *use;
	mxml_node_t *cur;
	mxml_node_t *cur2;
	mxml_node_t *cur3;
	mxml_node_t *menu;
	const char *tmp;

	if (!(f = fopen(tree_file, "r"))) {
		fprintf(stderr, "Unable to open '%s' for reading!\n", tree_file);
		return -1;
	}

	if (!(tree = calloc(1, sizeof(*tree)))) {
		fclose(f);
		return -1;
	}

	if (!(tree->root = mxmlLoadFile(NULL, f, MXML_OPAQUE_CALLBACK))) {
		fclose(f);
		free(tree);
		return -1;
	}

	AST_LIST_INSERT_HEAD(&trees, tree, list);

	menu = mxmlFindElement(tree->root, tree->root, "menu", NULL, NULL, MXML_DESCEND);
	if ((tmp = mxmlElementGetAttr(menu, "name")))
		menu_name = tmp;
	for (cur = mxmlFindElement(menu, menu, "category", NULL, NULL, MXML_DESCEND);
	     cur;
	     cur = mxmlFindElement(cur, menu, "category", NULL, NULL, MXML_DESCEND))
	{
		struct category *cat;
		struct category *newcat;

		if (!(cat = calloc(1, sizeof(*cat))))
			return -1;

		cat->name = mxmlElementGetAttr(cur, "name");

		newcat = add_category(cat);

		if (newcat != cat) {
			/* want to append members, and potentially update the category. */
			free(cat);
			cat = newcat;
		}

		if ((tmp = mxmlElementGetAttr(cur, "displayname")))
			cat->displayname = tmp;
		if ((tmp = mxmlElementGetAttr(cur, "positive_output")))
			cat->positive_output = !strcasecmp(tmp, "yes");
		if ((tmp = mxmlElementGetAttr(cur, "exclusive")))
			cat->exclusive = !strcasecmp(tmp, "yes");
		if ((tmp = mxmlElementGetAttr(cur, "remove_on_change")))
			cat->remove_on_change = tmp;

		for (cur2 = mxmlFindElement(cur, cur, "member", NULL, NULL, MXML_DESCEND);
		     cur2;
		     cur2 = mxmlFindElement(cur2, cur, "member", NULL, NULL, MXML_DESCEND))
		{
			if (!(mem = calloc(1, sizeof(*mem))))
				return -1;
			
			mem->name = mxmlElementGetAttr(cur2, "name");
			mem->displayname = mxmlElementGetAttr(cur2, "displayname");
		
			mem->remove_on_change = mxmlElementGetAttr(cur2, "remove_on_change");

			if (!cat->positive_output)
				mem->was_enabled = mem->enabled = 1;
	
			cur3 = mxmlFindElement(cur2, cur2, "defaultenabled", NULL, NULL, MXML_DESCEND);
			if (cur3 && cur3->child)
				mem->defaultenabled = cur3->child->value.opaque;
			
			for (cur3 = mxmlFindElement(cur2, cur2, "depend", NULL, NULL, MXML_DESCEND);
			     cur3 && cur3->child;
			     cur3 = mxmlFindElement(cur3, cur2, "depend", NULL, NULL, MXML_DESCEND))
			{
				if (!(dep = calloc(1, sizeof(*dep)))) {
					free_member(mem);
					return -1;
				}
				if (!strlen_zero(cur3->child->value.opaque)) {
					dep->name = cur3->child->value.opaque;
					AST_LIST_INSERT_TAIL(&mem->deps, dep, list);
				} else
					free(dep);
			}

			for (cur3 = mxmlFindElement(cur2, cur2, "conflict", NULL, NULL, MXML_DESCEND);
			     cur3 && cur3->child;
			     cur3 = mxmlFindElement(cur3, cur2, "conflict", NULL, NULL, MXML_DESCEND))
			{
				if (!(cnf = calloc(1, sizeof(*cnf)))) {
					free_member(mem);
					return -1;
				}
				if (!strlen_zero(cur3->child->value.opaque)) {
					cnf->name = cur3->child->value.opaque;
					AST_LIST_INSERT_TAIL(&mem->conflicts, cnf, list);
				} else
					free(cnf);
			}

			for (cur3 = mxmlFindElement(cur2, cur2, "use", NULL, NULL, MXML_DESCEND);
			     cur3 && cur3->child;
			     cur3 = mxmlFindElement(cur3, cur2, "use", NULL, NULL, MXML_DESCEND))
			{
				if (!(use = calloc(1, sizeof(*use)))) {
					free_member(mem);
					return -1;
				}
				if (!strlen_zero(cur3->child->value.opaque)) {
					use->name = cur3->child->value.opaque;
					AST_LIST_INSERT_TAIL(&mem->uses, use, list);
				} else
					free(use);
			}

			if (add_member(mem, cat))
				free_member(mem);
		}
	}

	fclose(f);

	return 0;
}

/*!
 * \arg interactive Set to non-zero if being called while user is making changes
 */
static unsigned int calc_dep_failures(int interactive)
{
	unsigned int result = 0;
	struct category *cat;
	struct member *mem;
	struct depend *dep;
	unsigned int changed, old_failure;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			old_failure = mem->depsfailed;
			AST_LIST_TRAVERSE(&mem->deps, dep, list) {
				if (dep->member)
					continue;

				mem->depsfailed = HARD_FAILURE;
				AST_LIST_TRAVERSE(&deps_file, dep_file, list) {
					if (!strcasecmp(dep_file->name, dep->name)) {
						if (dep_file->met)
							mem->depsfailed = NO_FAILURE;
						break;
					}
				}
				if (mem->depsfailed != NO_FAILURE) {
					break; /* This dependency is not met, so we can stop now */
				}
			}
			if (old_failure == SOFT_FAILURE && mem->depsfailed != HARD_FAILURE)
				mem->depsfailed = SOFT_FAILURE;
		}
	}

	do {
		changed = 0;

		AST_LIST_TRAVERSE(&categories, cat, list) {
			AST_LIST_TRAVERSE(&cat->members, mem, list) {
				old_failure = mem->depsfailed;

				if (mem->depsfailed == HARD_FAILURE)
					continue;

				mem->depsfailed = NO_FAILURE;

				AST_LIST_TRAVERSE(&mem->deps, dep, list) {
					if (!dep->member)
						continue;
					if (dep->member->depsfailed == HARD_FAILURE) {
						mem->depsfailed = HARD_FAILURE;
						break;
					} else if (dep->member->depsfailed == SOFT_FAILURE) {
						mem->depsfailed = SOFT_FAILURE;
					} else if (!dep->member->enabled) {
						mem->depsfailed = SOFT_FAILURE;
					}
				}
				
				if (mem->depsfailed != old_failure) {
					if ((mem->depsfailed == NO_FAILURE) && mem->was_defaulted) {
						mem->enabled = !strcasecmp(mem->defaultenabled, "yes");
					} else {
						mem->enabled = interactive ? 0 : mem->was_enabled;
					}
					changed = 1;
					break; /* This dependency is not met, so we can stop now */
				}
			}
			if (changed)
				break;
		}

		if (changed)
			result = 1;

	} while (changed);

	return result;
}

static unsigned int calc_conflict_failures(int interactive)
{
	unsigned int result = 0;
	struct category *cat;
	struct member *mem;
	struct conflict *cnf;
	unsigned int changed, old_failure;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			old_failure = mem->conflictsfailed;
			AST_LIST_TRAVERSE(&mem->conflicts, cnf, list) {
				if (cnf->member)
					continue;

				mem->conflictsfailed = NO_FAILURE;
				AST_LIST_TRAVERSE(&deps_file, dep_file, list) {
					if (!strcasecmp(dep_file->name, cnf->name)) {
						if (dep_file->met)
							mem->conflictsfailed = HARD_FAILURE;
						break;
					}
				}

				if (mem->conflictsfailed != NO_FAILURE)
					break; /* This conflict was found, so we can stop now */
			}
			if (old_failure == SOFT_FAILURE && mem->conflictsfailed != HARD_FAILURE)
				mem->conflictsfailed = SOFT_FAILURE;
		}
	}

	do {
		changed = 0;

		AST_LIST_TRAVERSE(&categories, cat, list) {
			AST_LIST_TRAVERSE(&cat->members, mem, list) {
				old_failure = mem->conflictsfailed;

				if (mem->conflictsfailed == HARD_FAILURE)
					continue;

				mem->conflictsfailed = NO_FAILURE;

				AST_LIST_TRAVERSE(&mem->conflicts, cnf, list) {
					if (!cnf->member)
						continue;
						
					if (cnf->member->enabled) {
						mem->conflictsfailed = SOFT_FAILURE;
						break;
					}
				}
				
				if (mem->conflictsfailed != old_failure && mem->conflictsfailed != NO_FAILURE) {
					mem->enabled = 0;
					changed = 1;
					break; /* This conflict has been found, so we can stop now */
				}
			}
			if (changed)
				break;
		}

		if (changed)
			result = 1;

	} while (changed);

	return result;
}

/*! \brief Process dependencies against the input dependencies file */
static int process_deps(void)
{
	FILE *f;
	char buf[80];
	char *p;
	int res = 0;

	if (!(f = fopen(MENUSELECT_DEPS, "r"))) {
		fprintf(stderr, "Unable to open '%s' for reading!  Did you run ./configure ?\n", MENUSELECT_DEPS);
		return -1;
	}

	/* Build a dependency list from the file generated by configure */	
	while (memset(buf, 0, sizeof(buf)), fgets(buf, sizeof(buf), f)) {
		p = buf;
		strsep(&p, "=");
		if (!p)
			continue;
		if (!(dep_file = calloc(1, sizeof(*dep_file))))
			break;
		strncpy(dep_file->name, buf, sizeof(dep_file->name) - 1);
		dep_file->met = atoi(p);
		AST_LIST_INSERT_TAIL(&deps_file, dep_file, list);
	}

	fclose(f);

	return res;
}

static void free_deps_file(void)
{
	/* Free the dependency list we built from the file */
	while ((dep_file = AST_LIST_REMOVE_HEAD(&deps_file, list)))
		free(dep_file);
}

static int match_member_relations(void)
{
	struct category *cat, *cat2;
	struct member *mem, *mem2;
	struct depend *dep;
	struct conflict *cnf;
	struct use *use;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			AST_LIST_TRAVERSE(&mem->deps, dep, list) {
				AST_LIST_TRAVERSE(&cat->members, mem2, list) {
					if (strcasecmp(mem2->name, dep->name))
						continue;

					dep->member = mem2;
					break;
				}
				if (dep->member)
					break;

				AST_LIST_TRAVERSE(&categories, cat2, list) {
					AST_LIST_TRAVERSE(&cat2->members, mem2, list) {
						if (strcasecmp(mem2->name, dep->name))
							continue;
						
						dep->member = mem2;
						break;
					}
					if (dep->member)
						break;
				}
			}
		}
	}

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			AST_LIST_TRAVERSE(&mem->uses, use, list) {
				AST_LIST_TRAVERSE(&cat->members, mem2, list) {
					if (strcasecmp(mem2->name, use->name))
						continue;

					use->member = mem2;
					break;
				}
				if (use->member)
					break;

				AST_LIST_TRAVERSE(&categories, cat2, list) {
					AST_LIST_TRAVERSE(&cat2->members, mem2, list) {
						if (strcasecmp(mem2->name, use->name))
							continue;
						
						use->member = mem2;
						break;
					}
					if (use->member)
						break;
				}
			}
		}
	}

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (!cat->exclusive)
			continue;

		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			AST_LIST_TRAVERSE(&cat->members, mem2, list) {
				if (mem2 == mem)
					continue;

				if (!(cnf = calloc(1, sizeof(*cnf))))
					return -1;

				cnf->name = mem2->name;
				cnf->member = mem2;
				AST_LIST_INSERT_TAIL(&mem->conflicts, cnf, list);
			}
		}
	}

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			AST_LIST_TRAVERSE(&mem->conflicts, cnf, list) {
				AST_LIST_TRAVERSE(&cat->members, mem2, list) {
					if (strcasecmp(mem2->name, cnf->name))
						continue;

					cnf->member = mem2;
					break;
				}
				if (cnf->member)
					break;

				AST_LIST_TRAVERSE(&categories, cat2, list) {
					AST_LIST_TRAVERSE(&cat2->members, mem2, list) {
						if (strcasecmp(mem2->name, cnf->name))
							continue;
						
						cnf->member = mem2;
						break;
					}
					if (cnf->member)
						break;
				}
			}
		}
	}

	return 0;
}

/*! \brief Iterate through all of the input tree files and call the parse function on them */
static int build_member_list(void)
{
	int i;
	int res = -1;

	for (i = 0; i < (sizeof(tree_files) / sizeof(tree_files[0])); i++) {
		if ((res = parse_tree(tree_files[i]))) {
			fprintf(stderr, "Error parsing '%s'!\n", tree_files[i]);
			break;
		}
	}

	if (!res)
		res = match_member_relations();

	return res;
}

/*! \brief Given the string representation of a member and category, mark it as present in a given input file */
static void mark_as_present(const char *member, const char *category)
{
	struct category *cat;
	struct member *mem;
	char negate = 0;

	if (*member == '-') {
		member++;
		negate = 1;
	}

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (strcmp(category, cat->name))
			continue;
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (!strcmp(member, mem->name)) {
				mem->was_enabled = mem->enabled = (negate ? !cat->positive_output : cat->positive_output);
				break;
			}
		}
		if (!mem)
			fprintf(stderr, "member '%s' in category '%s' not found, ignoring.\n", member, category);
		break;
	}

	if (!cat)
		fprintf(stderr, "category '%s' not found! Can't mark '%s' as disabled.\n", category, member);
}

unsigned int enable_member(struct member *mem)
{
	struct depend *dep;
	unsigned int can_enable = 1;

	AST_LIST_TRAVERSE(&mem->deps, dep, list) {
		if (!dep->member)
			continue;

		if (!dep->member->enabled) {
			if (dep->member->conflictsfailed != NO_FAILURE) {
				can_enable = 0;
				break;
			}

			if (dep->member->depsfailed == HARD_FAILURE) {
				can_enable = 0;
				break;
			}

			if (!(can_enable = enable_member(dep->member)))
				break;
		}
	}

	if ((mem->enabled = can_enable))
		while (calc_dep_failures(1) || calc_conflict_failures(1));

	return can_enable;
}

void toggle_enabled(struct member *mem)
{
	if ((mem->depsfailed == HARD_FAILURE) || (mem->conflictsfailed == HARD_FAILURE))
		return;

	if (!mem->enabled)
		enable_member(mem);
	else
		mem->enabled = 0;

					fprintf(stderr, "3- changed %s to %d\n", mem->name, mem->enabled);
	mem->was_defaulted = 0;
	changes_made++;

	while (calc_dep_failures(1) || calc_conflict_failures(1));
}

/*! \brief Toggle a member of a category at the specified index to enabled/disabled */
void toggle_enabled_index(struct category *cat, int index)
{
	struct member *mem;
	int i = 0;

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (i++ == index)
			break;
	}

	if (!mem)
		return;

	toggle_enabled(mem);
}

void set_enabled(struct category *cat, int index)
{
	struct member *mem;
	int i = 0;

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (i++ == index)
			break;
	}

	if (!mem)
		return;

	if ((mem->depsfailed == HARD_FAILURE) || (mem->conflictsfailed == HARD_FAILURE))
		return;

	if (mem->enabled)
		return;

	enable_member(mem);
	mem->was_defaulted = 0;
	changes_made++;

	while (calc_dep_failures(1) || calc_conflict_failures(1));
}

void clear_enabled(struct category *cat, int index)
{
	struct member *mem;
	int i = 0;

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (i++ == index)
			break;
	}

	if (!mem)
		return;

	if (!mem->enabled)
		return;

	mem->enabled = 0;
	mem->was_defaulted = 0;
	changes_made++;

	while (calc_dep_failures(1) || calc_conflict_failures(1));
}

/*! \brief Process a previously failed dependency
 *
 * If a module was previously disabled because of a failed dependency
 * or a conflict, and not because the user selected it to be that way,
 * then it needs to be re-enabled by default if the problem is no longer present.
 */
static void process_prev_failed_deps(char *buf)
{
	const char *cat_name, *mem_name;
	struct category *cat;
	struct member *mem;

	cat_name = strsep(&buf, "=");
	mem_name = strsep(&buf, "\n");

	if (!cat_name || !mem_name)
		return;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		if (strcasecmp(cat->name, cat_name))
			continue;
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (strcasecmp(mem->name, mem_name))
				continue;

			if (!mem->depsfailed && !mem->conflictsfailed) {
				mem->enabled = 1;			
				mem->was_defaulted = 0;
			}
	
			break;
		}
		break;	
	}

	if (!cat || !mem)
		fprintf(stderr, "Unable to find '%s' in category '%s'\n", mem_name, cat_name);
}

/*! \brief Parse an existing output makeopts file and enable members previously selected */
static int parse_existing_config(const char *infile)
{
	FILE *f;
	char buf[2048];
	char *category, *parse, *member;
	int lineno = 0;

	if (!(f = fopen(infile, "r"))) {
#ifdef MENUSELECT_DEBUG
		/* This isn't really an error, so only print the message in debug mode */
		fprintf(stderr, "Unable to open '%s' for reading existing config.\n", infile);
#endif	
		return -1;
	}

	while (fgets(buf, sizeof(buf), f)) {
		lineno++;

		if (strlen_zero(buf))
			continue;

		/* skip lines that are not for this tool */
		if (strncasecmp(buf, "MENUSELECT_", strlen("MENUSELECT_")))
			continue;

		if (!strncasecmp(buf, "MENUSELECT_DEPENDS_", strlen("MENUSELECT_DEPENDS_")))
			continue;

		if (!strncasecmp(buf, "MENUSELECT_BUILD_DEPS", strlen("MENUSELECT_BUILD_DEPS")))
			continue;

		parse = buf;
		parse = skip_blanks(parse);
		if (strlen_zero(parse))
			continue;

		/* Grab the category name */	
		category = strsep(&parse, "=");
		if (!parse) {
			fprintf(stderr, "Invalid string in '%s' at line '%d'!\n", output_makeopts, lineno);
			continue;
		}
		
		parse = skip_blanks(parse);
	
		if (!strcasecmp(category, "MENUSELECT_DEPSFAILED")) {
			process_prev_failed_deps(parse);
			continue;
		}
	
		while ((member = strsep(&parse, " \n"))) {
			member = skip_blanks(member);
			if (strlen_zero(member))
				continue;
			mark_as_present(member, category);
		}
	}

	fclose(f);

	return 0;
}

/*! \brief Create the output dependencies file */
static int generate_makedeps_file(void)
{
	FILE *f;
	struct category *cat;
	struct member *mem;
	struct depend *dep;
	struct use *use;

	if (!(f = fopen(output_makedeps, "w"))) {
		fprintf(stderr, "Unable to open dependencies file (%s) for writing!\n", output_makedeps);
		return -1;
	}

	/* Traverse all categories and members and output dependencies for each member */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (AST_LIST_EMPTY(&mem->deps) && AST_LIST_EMPTY(&mem->uses))
				continue;

			fprintf(f, "MENUSELECT_DEPENDS_%s=", mem->name);
			AST_LIST_TRAVERSE(&mem->deps, dep, list) {
				const char *c;

				for (c = dep->name; *c; c++)
					fputc(toupper(*c), f);
				fputc(' ', f);
			}
			AST_LIST_TRAVERSE(&mem->uses, use, list) {
				const char *c;

				for (c = use->name; *c; c++)
					fputc(toupper(*c), f);
				fputc(' ', f);
			}
			fprintf(f, "\n");
		}
	}

	fclose(f);

	return 0;
}

/*! \brief Create the output makeopts file that results from the user's selections */
static int generate_makeopts_file(void)
{
	FILE *f;
	struct category *cat;
	struct member *mem;
	struct depend *dep;
	struct use *use;

	if (!(f = fopen(output_makeopts, "w"))) {
		fprintf(stderr, "Unable to open build configuration file (%s) for writing!\n", output_makeopts);
		return -1;
	}

	/* Traverse all categories and members and output them as var/val pairs */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		fprintf(f, "%s=", cat->name);
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if ((!cat->positive_output && (!mem->enabled || mem->depsfailed || mem->conflictsfailed)) ||
			    (cat->positive_output && mem->enabled && !mem->depsfailed && !mem->conflictsfailed))
				fprintf(f, "%s ", mem->name);
		}
		fprintf(f, "\n");
	}

	/* Traverse all categories and members, and for every member that is not disabled,
	   if it has internal dependencies (other members), list those members one time only
	   in a special variable */
	fprintf(f, "MENUSELECT_BUILD_DEPS=");
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if ((!cat->positive_output && (!mem->enabled || mem->depsfailed || mem->conflictsfailed)) ||
			    (cat->positive_output && mem->enabled && !mem->depsfailed && !mem->conflictsfailed))
				continue;

			AST_LIST_TRAVERSE(&mem->deps, dep, list) {
				/* we only care about dependencies between members (internal, not external) */
				if (!dep->member)
					continue;
				/* if this has already been output, continue */
				if (dep->member->build_deps_output)
					continue;
				fprintf(f, "%s ", dep->member->name);
				dep->member->build_deps_output = 1;
			}
			AST_LIST_TRAVERSE(&mem->uses, use, list) {
				/* we only care about dependencies between members (internal, not external) */
				if (!use->member)
					continue;
				/* if the dependency module is not going to be built, don't list it */
				if (!use->member->enabled)
					continue;
				/* if this has already been output, continue */
				if (use->member->build_deps_output)
					continue;
				fprintf(f, "%s ", use->member->name);
				use->member->build_deps_output = 1;
			}
		}
	}
	fprintf(f, "\n");

	/* Output which members were disabled because of failed dependencies or conflicts */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (mem->depsfailed != HARD_FAILURE && mem->conflictsfailed != HARD_FAILURE)
				continue;

			if (!mem->defaultenabled || !strcasecmp(mem->defaultenabled, "yes"))
				fprintf(f, "MENUSELECT_DEPSFAILED=%s=%s\n", cat->name, mem->name);
		}
	}

	fclose(f);

	/* there is no need to process remove_on_change rules if we did not have
	   configuration information to start from
	*/
	if (!existing_config)
		return 0;

	/* Traverse all categories and members and remove any files that are supposed
	   to be removed when an item has been changed */
	AST_LIST_TRAVERSE(&categories, cat, list) {
		unsigned int had_changes = 0;
		char rmcommand[256] = "rm -rf ";
		char *file, *buf;

		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if ((mem->enabled == mem->was_enabled) && !mem->was_defaulted)
				continue;

			had_changes = 1;

			if (mem->remove_on_change) {
				for (buf = ast_strdupa(mem->remove_on_change), file = strsep(&buf, " ");
				     file;
				     file = strsep(&buf, " ")) {
					strcpy(&rmcommand[7], file);
					system(rmcommand);
				}
			}
		}

		if (cat->remove_on_change && had_changes) {
			for (buf = ast_strdupa(cat->remove_on_change), file = strsep(&buf, " ");
			     file;
			     file = strsep(&buf, " ")) {
				strcpy(&rmcommand[7], file);
				system(rmcommand);
			}
		}
	}

	return 0;
}

#ifdef MENUSELECT_DEBUG
/*! \brief Print out all of the information contained in our tree */
static void dump_member_list(void)
{
	struct category *cat;
	struct member *mem;
	struct depend *dep;
	struct conflict *cnf;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		fprintf(stderr, "Category: '%s'\n", cat->name);
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			fprintf(stderr, "   ==>> Member: '%s'  (%s)", mem->name, mem->enabled ? "Enabled" : "Disabled");
			fprintf(stderr, "        Was %s\n", mem->was_enabled ? "Enabled" : "Disabled");
			if (mem->defaultenabled)
				fprintf(stderr, "        Defaults to %s\n", !strcasecmp(mem->defaultenabled, "yes") ? "Enabled" : "Disabled");
			AST_LIST_TRAVERSE(&mem->deps, dep, list)
				fprintf(stderr, "      --> Depends on: '%s'\n", dep->name);
			if (!AST_LIST_EMPTY(&mem->deps))
				fprintf(stderr, "      --> Dependencies Met: %s\n", mem->depsfailed ? "No" : "Yes");	
			AST_LIST_TRAVERSE(&mem->conflicts, cnf, list)
				fprintf(stderr, "      --> Conflicts with: '%s'\n", cnf->name);
			if (!AST_LIST_EMPTY(&mem->conflicts))
				fprintf(stderr, "      --> Conflicts Found: %s\n", mem->conflictsfailed ? "Yes" : "No");
		}
	}
}
#endif

/*! \brief Free all categories and their members */
static void free_member_list(void)
{
	struct category *cat;
	struct member *mem;
	struct depend *dep;
	struct conflict *cnf;

	while ((cat = AST_LIST_REMOVE_HEAD(&categories, list))) {
		while ((mem = AST_LIST_REMOVE_HEAD(&cat->members, list))) {
			while ((dep = AST_LIST_REMOVE_HEAD(&mem->deps, list)))
				free(dep);
			while ((cnf = AST_LIST_REMOVE_HEAD(&mem->conflicts, list)))
				free(cnf);
			free(mem);
		}
		free(cat);
	}
}

/*! \brief Free all of the XML trees */
static void free_trees(void)
{
	struct tree *tree;

	while ((tree = AST_LIST_REMOVE_HEAD(&trees, list))) {
		mxmlDelete(tree->root);
		free(tree);
	}
}

/*! \brief Enable/Disable all members of a category as long as dependencies have been met and no conflicts are found */
void set_all(struct category *cat, int val)
{
	struct member *mem;

	AST_LIST_TRAVERSE(&cat->members, mem, list) {
		if (mem->enabled == val)
			continue;

		if ((mem->depsfailed == HARD_FAILURE) || (mem->conflictsfailed == HARD_FAILURE))
			continue;

		if (val) {
			enable_member(mem);
		} else {
			mem->enabled = 0;
		}

		mem->was_defaulted = 0;
		changes_made++;
	}
}

int count_categories(void)
{
	struct category *cat;
	int count = 0;

	AST_LIST_TRAVERSE(&categories, cat, list)
		count++;

	return count;		
}

int count_members(struct category *cat)
{
	struct member *mem;
	int count = 0;

	AST_LIST_TRAVERSE(&cat->members, mem, list)
		count++;

	return count;		
}

/*! \brief Make sure an existing menuselect.makeopts disabled everything it should have */
static int sanity_check(void)
{
	struct category *cat;
	struct member *mem;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if ((mem->depsfailed || mem->conflictsfailed) && mem->enabled) {
				fprintf(stderr, "\n***********************************************************\n"
				                "  The existing menuselect.makeopts file did not specify    \n"
				                "  that '%s' should not be included.  However, either some  \n"
				                "  dependencies for this module were not found or a         \n"
				                "  conflict exists.                                         \n"
				                "                                                           \n"
				                "  Either run 'make menuselect' or remove the existing      \n"
				                "  menuselect.makeopts file to resolve this issue.          \n"
						"***********************************************************\n\n", mem->name);
				return -1;
			}
		}
	}
	return 0;	/* all good... */
}

/* \brief Set the forced default values if they exist */
static void process_defaults(void)
{
	struct category *cat;
	struct member *mem;

	AST_LIST_TRAVERSE(&categories, cat, list) {
		AST_LIST_TRAVERSE(&cat->members, mem, list) {
			if (!mem->defaultenabled)
				continue;

			if (mem->depsfailed == HARD_FAILURE)
				continue;

			if (mem->conflictsfailed == HARD_FAILURE)
				continue;
			
			if (!strcasecmp(mem->defaultenabled, "yes")) {
				mem->enabled = 1;
				mem->was_defaulted = 1;
			} else if (!strcasecmp(mem->defaultenabled, "no")) {
				mem->enabled = 0;
				mem->was_defaulted = 1;
			} else
				fprintf(stderr, "Invalid defaultenabled value for '%s' in category '%s'\n", mem->name, cat->name);	
		}
	}

}

int main(int argc, char *argv[])
{
	int res = 0;
	unsigned int x;

	/* Make the compiler happy */
	print_debug("");

#ifdef MENUSELECT_DEBUG
	if (!(debug = fopen("menuselect_debug.txt", "w"))) {
		fprintf(stderr, "Failed to open menuselect_debug.txt for debug output.\n");
		exit(1);
	}
#endif

	/* Parse the input XML files to build the list of available options */
	if ((res = build_member_list()))
		exit(res);
	
	/* Process module dependencies */
	if ((res = process_deps()))
		exit(res);

	while (calc_dep_failures(0) || calc_conflict_failures(0));
	
	/* The --check-deps option is used to ask this application to check to
	 * see if that an existing menuselect.makeopts file contains all of the
	 * modules that have dependencies that have not been met.  If this
	 * is not the case, an informative message will be printed to the
	 * user and the build will fail. */
	for (x = 1; x < argc; x++) {
		if (!strcmp(argv[x], "--check-deps"))
			check_deps = 1;
		else {
			res = parse_existing_config(argv[x]);
			if (!res && !strcasecmp(argv[x], OUTPUT_MAKEOPTS_DEFAULT))
				existing_config = 1;
			res = 0;
		}
	}

#ifdef MENUSELECT_DEBUG
	/* Dump the list produced by parsing the various input files */
	dump_member_list();
#endif

	while (calc_dep_failures(0) || calc_conflict_failures(0));

	if (!existing_config)
		process_defaults();
	else if (check_deps)
		res = sanity_check();

	while (calc_dep_failures(0) || calc_conflict_failures(0));
	
	/* Run the menu to let the user enable/disable options */
	if (!check_deps && !res)
		res = run_menu();

	if (!res)
		res = generate_makeopts_file();

	/* Always generate the dependencies file */
	if (!res)
		generate_makedeps_file();
	
	/* free everything we allocated */
	free_deps_file();
	free_trees();
	free_member_list();

#ifdef MENUSELECT_DEBUG
	if (debug)
		fclose(debug);
#endif

	exit(res);
}
