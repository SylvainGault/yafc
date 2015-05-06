/*
 * put.c -- upload files
 *
 * Yet Another FTP Client
 * Copyright (C) 1998-2001, Martin Hedenfalk <mhe@stacken.kth.se>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. See COPYING for more details.
 */

#include "syshdr.h"
#include "ftp.h"
#include "shortpath.h"
#include "gvars.h"
#include "strq.h"
#include "transfer.h"
#include "input.h"
#include "commands.h"
#include "lglob.h"
#include "utils.h"

#ifdef HAVE_REGEX_H
# include <regex.h>
#endif

#define PUT_INTERACTIVE (1 << 0)
#define PUT_APPEND (1 << 1)
#define PUT_PRESERVE (1 << 2)
#define PUT_PARENTS (1 << 3)
#define PUT_RECURSIVE (1 << 4)
#define PUT_VERBOSE (1 << 5)
#define PUT_FORCE (1 << 6)
#define PUT_FORCE_NEWER (1 << 7)
#define PUT_OUTPUT_FILE (1 << 8)  /* --output=FILE (else --output=DIR) */
#define PUT_UNIQUE (1 << 9)
#define PUT_DELETE_AFTER (1 << 10)
#define PUT_SKIP_EXISTING (1 << 11)
#define PUT_NOHUP (1 << 12)
#define PUT_RESUME (1 << 13)
#define PUT_NEWER (1 << 14)
#define PUT_TAGGED (1 << 15)
#define PUT_ASCII (1 << 16)
#define PUT_BINARY (1 << 17)
#define PUT_SKIP_EMPTY (1 << 18)

static bool put_batch = false;
static bool put_owbatch = false;
static bool put_delbatch = false;
static bool put_quit = false;
static bool put_skip_empty = false;

static char *put_glob_mask = 0;
static char *put_dir_glob_mask = 0;
#ifdef HAVE_REGEX
static regex_t put_rx_mask;
static bool put_rx_mask_set = false;
static regex_t put_dir_rx_mask;
static bool put_dir_rx_mask_set = false;
#endif

static void print_put_syntax(void)
{
	show_help(_("Send files to remote."), "put [options] file(s) (can include wildcards)",
	  _("  -a, --append         append if destination file exists\n"
			"  -D, --delete-after   delete local file after successful transfer\n"
			"      --dir-mask=GLOB  enter only directories matching GLOB pattern\n"
			"      --dir-rx-mask=REGEXP\n"
			"                       enter only directories matching REGEXP pattern\n"
			"  -e, --skip-empty     skip empty files\n"
			"  -f, --force          overwrite existing destinations, never prompt\n"
      "  -F, --force-newer    do not use cached information with --newer\n"
			"  -H, --nohup          transfer files in background (nohup mode), quits yafc\n"
			"  -i, --interactive    prompt before transferring each file\n"
			"  -L, --logfile=FILE   specify other logfile used by --nohup\n"
			"  -m, --mask=GLOB      put only files matching GLOB pattern\n"
			"  -M, --rx-mask=REGEXP put only files matching REGEXP pattern\n"
			"  -n, --newer          transfer file if local is newer than remote file\n"
			"  -o, --output=DEST    store in remote file/directory DEST\n"
			"  -p, --preserve       try to preserve file attributes\n"
			"  -P, --parents        append source path to destination\n"
			"  -q, --quiet          overrides --verbose\n"
			"  -r, --recursive      upload directories recursively\n"
			"  -R, --resume         resume broken transfer (restart at EOF)\n"
			"  -s, --skip-existing  always skip existing files\n"
			"  -S, --stats[=NUM]    set stats transfer threshold; default is always\n"
			"  -t, --tagged         transfer (locally) tagged file(s)\n"
			"      --type=TYPE      specify transfer type, 'ascii' or 'binary'\n"
			"  -v, --verbose        explain what is being done\n"
			"  -u, --unique         store in unique filename (if server supports STOU)\n"));
}

static bool put_exclude_func(char *path)
{
	if(lglob_exclude_dotdirs(path))
		return true;
	if(put_skip_empty == true) {
		struct stat stbuf;
		stat(path, &stbuf);
		if(!S_ISDIR(stbuf.st_mode) && stbuf.st_size == 0)
			return true;
	}
	return false;
}

static int do_the_put(const char *src, const char *dest,
					  putmode_t how, unsigned opt)
{
	int r;
	transfer_mode_t type;

	if(test(opt, PUT_NOHUP))
		fprintf(stderr, "%s\n", src);

	type = ascii_transfer(src) ? tmAscii : gvDefaultType;
	if(test(opt, PUT_ASCII))
		type = tmAscii;
	else if(test(opt, PUT_BINARY))
		type = tmBinary;

#if 0 && (defined(HAVE_SETPROCTITLE) || defined(linux))
	if(gvUseEnvString && ftp_connected())
		setproctitle("%s, put %s", ftp->url->hostname, src);
#endif
	r = ftp_putfile(src, dest, how, type,
					test(opt, PUT_VERBOSE) ? transfer : 0);
#if 0 && (defined(HAVE_SETPROCTITLE) || defined(linux))
	if(gvUseEnvString && ftp_connected())
		setproctitle("%s", ftp->url->hostname);
#endif

	if(test(opt, PUT_NOHUP)) {
		if(r == 0)
			transfer_mail_msg(_("sent %s\n"), src);
		else
			transfer_mail_msg(_("failed to send %s: %s\n"),
							  src, ftp_getreply(false));
	}

	return r;
}

static void putfile(const char *path, struct stat *sb,
					unsigned opt, const char *output)
{
	putmode_t how = putNormal;
	bool file_exists = false;
	char *dest, *dpath;
	int r;
	bool dir_created;
	char *dest_dir, *q_dest_dir;

	if((put_glob_mask && fnmatch(put_glob_mask, base_name_ptr(path),
								 FNM_EXTMATCH) == FNM_NOMATCH)
#ifdef HAVE_REGEX
	   || (put_rx_mask_set && regexec(&put_rx_mask, base_name_ptr(path),
									  0, 0, 0) == REG_NOMATCH)
#endif
		)
		return;

	if(!output)
		output = ".";

	if(test(opt, PUT_PARENTS)) {
		char *p = base_dir_xptr(path);
		if (asprintf(&dest, "%s/%s/%s", output, p, base_name_ptr(path)) == -1)
    {
      fprintf(stderr, _("Failed to allocate memory.\n"));
      free(p);
      return;
    }
		free(p);
	} else if(test(opt, PUT_OUTPUT_FILE))
		dest = xstrdup(output);
	else
		if (asprintf(&dest, "%s/%s", output, base_name_ptr(path)) == -1)
    {
      fprintf(stderr, _("Failed to allocate memory.\n"));
      return;
    }

	path_collapse(dest);

	/* make sure destination directory exists */
	dpath = base_dir_xptr(dest);
	dest_dir = ftp_path_absolute(dpath);
	q_dest_dir = backslash_quote(dest_dir);
	r = ftp_mkpath(q_dest_dir);
	free(q_dest_dir);
	free(dest_dir);
	if(r == -1) {
		transfer_mail_msg(_("Couldn't create directory: %s\n"), dest_dir);
		free(dpath);
		free(dest);
		return;
	}
	dir_created = (r == 1);

	if(!dir_created && !test(opt, PUT_UNIQUE) && !test(opt, PUT_FORCE)) {
		rfile *f;
		f = ftp_get_file(dest);
		file_exists = (f != 0);
		if(f && risdir(f)) {
			/* can't overwrite a directory */
			printf(_("%s: is a directory\n"), dest);
			free(dest);
			return;
		}
	}

	if(test(opt, PUT_APPEND)) {
		how = putAppend;
	} else if(file_exists) {
		if(test(opt, PUT_SKIP_EXISTING)) {
			char* sp = shortpath(dest, 42, ftp->homedir);
			printf(_("Remote file '%s' exists, skipping...\n"), sp);
			stats_file(STATS_SKIP, 0);
			free(sp);
			free(dest);
			return;
		}
		else if(test(opt, PUT_NEWER)) {
			time_t ft = ftp_filetime(dest, test(opt, PUT_FORCE_NEWER));
      ftp_trace("put -n: remote file: %s", ctime(&ft));
      ftp_trace("put -n: local file: %s\n", ctime(&sb->st_mtime));
			if(ft != (time_t)-1 && ft >= sb->st_mtime) {
				char* sp = shortpath(dest, 42, ftp->homedir);
				printf(_("Remote file '%s' is newer than local, skipping...\n"), sp);
				stats_file(STATS_SKIP, 0);
				free(sp);
				free(dest);
				return;
			}
		}
		else if(!test(opt, PUT_RESUME)) {
			if(!put_owbatch) {
				struct tm *fan = gmtime(&sb->st_mtime);
				time_t ft;
				int a;
				rfile *f;
				char *e;

				f = ftp_get_file(dest);
				ft = ftp_filetime(f->path, test(opt, PUT_FORCE_NEWER));
				sb->st_mtime = gmt_mktime(fan);
				e = xstrdup(ctime(&sb->st_mtime));
				char* sp = shortpath(dest, 42, ftp->homedir);
				a = ask(ASKYES|ASKNO|ASKUNIQUE|ASKCANCEL|ASKALL|ASKRESUME,
						ASKRESUME,
						_("Remote file '%s' exists\nLocal: %lld bytes, %sRemote: %lld bytes, %sOverwrite?"),
						sp,
						(unsigned long long) sb->st_size, e ? e : "unknown size",
						ftp_filesize(f->path), ctime(&ft));
				free(sp);
				free(e);
				if(a == ASKCANCEL) {
					put_quit = true;
					free(dest);
					return;
				}
				else if(a == ASKNO) {
					free(dest);
					return;
				}
				else if(a == ASKUNIQUE)
					opt |= PUT_UNIQUE; /* for this file only */
				else if(a == ASKALL)
					put_owbatch = true;
				else if(a == ASKRESUME)
					opt |= PUT_RESUME; /* for this file only */
				/* else a == ASKYES */
			}
		}
	}

	if(test(opt, PUT_RESUME))
		how = putResume;
	if(test(opt, PUT_UNIQUE))
		how = putUnique;

	r = do_the_put(path, dest, how, opt);
	free(dest);

	if(r != 0) {
		stats_file(STATS_FAIL, 0);
		return;
	} else {
		stats_file(STATS_SUCCESS, ftp->ti.total_size);
	}

	if(test(opt, PUT_PRESERVE)) {
		if(ftp->has_site_chmod_command)
			ftp_chmod(ftp->ti.local_name, get_mode_string(sb->st_mode));
	}

	if(test(opt, PUT_DELETE_AFTER)) {
		bool dodel = false;

		char* sp = shortpath(path, 42, gvLocalHomeDir);
		if(!test(opt, PUT_FORCE) && !put_delbatch) {
			int a = ask(ASKYES|ASKNO|ASKCANCEL|ASKALL, ASKYES,
						_("Delete local file '%s'?"), sp);
			if(a == ASKALL) {
				put_delbatch = true;
				dodel = true;
			}
			else if(a == ASKCANCEL)
				put_quit = true;
			else if(a != ASKNO)
				dodel = true;
		} else
			dodel = true;

		if(dodel) {
			if(unlink(path) == 0)
				printf(_("%s: deleted\n"), sp);
			else
				printf(_("error deleting '%s': %s\n"), sp, strerror(errno));
		}
		free(sp);
	}
}

static int put_sort_func(const void *a, const void *b)
{
   bool tfa = transfer_first((char *)a);
   bool tfb = transfer_first((char *)b);

   if(tfa)
	  return tfb ? 0 : -1;
   return tfb ? 1 : 0;
}

static void putfiles(list *gl, unsigned opt, const char *output)
{
	struct stat sb;
	char *path = 0;
	const char *file;
	listitem *li;

	list_sort(gl, put_sort_func, false);

	for(li=gl->first; li && !put_quit; li=li->next) {

		if(!ftp_connected())
			return;

		if(gvSighupReceived) {
			if(!test(opt, PUT_RESUME))
				opt |= PUT_UNIQUE;
			opt |= PUT_FORCE;
		}

		path = (char *)li->data;
		file = base_name_ptr(path);

		if(strcmp(file, ".") == 0 || strcmp(file, "..") == 0)
			continue;

		if(ignore(file))
			continue;

		if(test(opt, PUT_INTERACTIVE) && !put_batch) {
			char* sp = shortpath(path, 42, gvLocalHomeDir);
			int a = ask(ASKYES|ASKNO|ASKCANCEL|ASKALL, ASKYES,
						_("Put '%s'?"), sp);
			free(sp);
			if(a == ASKNO)
				continue;
			if(a == ASKCANCEL) {
				put_quit = true;
				break;
			}
			if(a == ASKALL)
				put_batch = true;
			/* else a==ASKYES */
		}

		if(stat(path, &sb) != 0) {
			perror(path);
			continue;
		}

		if(S_ISDIR(sb.st_mode)) {
			if(test(opt, PUT_RECURSIVE)) {
				char *recurs_output;
				char *recurs_mask;
				list *rgl;

				if((put_dir_glob_mask
					&& fnmatch(put_dir_glob_mask,
							   base_name_ptr(path),
							   FNM_EXTMATCH) == FNM_NOMATCH)
#ifdef HAVE_REGEX
				   || (put_dir_rx_mask_set
					   && regexec(&put_dir_rx_mask,
								  base_name_ptr(path),
								  0, 0, 0) == REG_NOMATCH)
#endif
					)
					{
						/*printf("skipping %s\n", path);*/
					} else {
						if(!test(opt, PUT_PARENTS))
            {
							if (asprintf(&recurs_output, "%s/%s",
									 output ? output : ".", file) == -1)
              {
                fprintf(stderr, _("Failed to allocate memory.\n"));
                continue;
              }
						} else
							recurs_output = xstrdup(output ? output : ".");

						if (asprintf(&recurs_mask, "%s/*", path) == -1)
            {
              fprintf(stderr, _("Failed to allocate memory.\n"));
              continue;
            }
						rgl = lglob_create();
						lglob_glob(rgl, recurs_mask, true, put_exclude_func);
						free(recurs_mask);

						if(list_numitem(rgl) > 0)
							putfiles(rgl, opt, recurs_output);
						free(recurs_output);
					}
			} else {
				char* sp = shortpath(path, 42, gvLocalHomeDir);
				fprintf(stderr, _("%s: omitting directory\n"), sp);
				free(sp);
			}
			continue;
		}
		if(!S_ISREG(sb.st_mode)) {
			char* sp = shortpath(path, 42, gvLocalHomeDir);
			fprintf(stderr, _("%s: not a regular file\n"), sp);
			free(sp);
			continue;
		}
		putfile(path, &sb, opt, output);

		if(gvInterrupted) {
			gvInterrupted = false;
			if(li->next && !put_quit && ftp_connected() && !gvSighupReceived)
			{
				int a = ask(ASKYES|ASKNO, ASKYES,
							_("Continue transfer?"));
				if(a == ASKNO) {
					put_quit = true;
					break;
				}
				/* else a == ASKYES */
				fprintf(stderr, _("Excellent!!!\n"));
			}
		}
	}
}

/* store a local file on remote server */
void cmd_put(int argc, char **argv)
{
	int c, opt=PUT_VERBOSE;
	list *gl;
	char *put_output = 0;
	char *logfile = 0;
	pid_t pid;
	int stat_thresh = gvStatsThreshold;
#ifdef HAVE_REGEX
	int ret;
	char put_rx_errbuf[129];
#endif
	struct option longopts[] = {
		{"append", no_argument, 0, 'a'},
		{"delete-after", no_argument, 0, 'D'},
		{"dir-mask", required_argument, 0, '3'},
#ifdef HAVE_REGEX
		{"dir-rx-mask", required_argument, 0, '4'},
#endif
		{"skip-empty", no_argument, 0, 'e'},
		{"force", no_argument, 0, 'f'},
    {"force-newer", no_argument, 0, 'F'},
		{"nohup", no_argument, 0, 'H'},
		{"interactive", no_argument, 0, 'i'},
		{"logfile", required_argument, 0, 'L'},
		{"mask", required_argument, 0, 'm'},
#ifdef HAVE_REGEX
		{"rx-mask", required_argument, 0, 'M'},
#endif
		{"newer", no_argument, 0, 'n'},
		{"output", required_argument, 0, 'o'},
		{"preserve", no_argument, 0, 'p'},
		{"parents", no_argument, 0, 'P'},
		{"quiet", no_argument, 0, 'q'},
		{"recursive", no_argument, 0, 'r'},
		{"resume", no_argument, 0, 'R'},
		{"skip-existing", no_argument, 0, 's'},
		{"stats", optional_argument, 0, 'S'},
		{"tagged", no_argument, 0, 't'},
		{"type", required_argument, 0, '1'},
		{"verbose", no_argument, 0, 'v'},
		{"unique", no_argument, 0, 'u'},
		{"help", no_argument, 0, 'h'},
		{0, 0, 0, 0},
	};

	if(put_glob_mask) {
		free(put_glob_mask);
		put_glob_mask = 0;
	}
	if(put_dir_glob_mask) {
		free(put_dir_glob_mask);
		put_dir_glob_mask = 0;
	}
#ifdef HAVE_REGEX
	if(put_rx_mask_set) {
		regfree(&put_rx_mask);
		put_rx_mask_set = 0;
	}
	if(put_dir_rx_mask_set) {
		regfree(&put_dir_rx_mask);
		put_dir_rx_mask_set = 0;
	}
#endif

	put_skip_empty = false;

  optind = 0; /* force getopt() to re-initialize */
  while((c = getopt_long(argc, argv,
               "aDefFHiL:no:pPqrRsStvum:M:", longopts, 0)) != EOF)
  {
    switch(c) {
    case 'i':
      opt |= PUT_INTERACTIVE;
      break;
    case 'f':
      opt |= PUT_FORCE;
      break;
    case 'F':
      opt |= PUT_FORCE_NEWER;
      break;
    case 'e':
      opt |= PUT_SKIP_EMPTY;
      put_skip_empty = true;
      break;
    case '3': /* --dir-mask=GLOB */
      free(put_dir_glob_mask);
      put_dir_glob_mask = xstrdup(optarg);
      unquote(put_dir_glob_mask);
      break;
#ifdef HAVE_REGEX
    case '4': /* --dir-rx-mask=REGEXP */
      if(put_dir_rx_mask_set) {
        regfree(&put_dir_rx_mask);
        put_dir_rx_mask_set = false;
      }
      unquote(optarg);
      ret = regcomp(&put_dir_rx_mask, optarg, REG_EXTENDED);
      if(ret != 0) {
        regerror(ret, &put_dir_rx_mask, put_rx_errbuf, sizeof(put_rx_errbuf) - 1);
        ftp_err(_("Regexp '%s' failed: %s\n"), optarg, put_rx_errbuf);
        return;
      } else
        put_dir_rx_mask_set = true;
      break;
#endif
    case 'o':
      put_output = tilde_expand_home(optarg, ftp->homedir);
      path_collapse(put_output);
      stripslash(put_output);
      break;
    case 'H':
      opt |= PUT_NOHUP;
      break;
    case 'L':
      free(logfile);
      logfile = xstrdup(optarg);
      unquote(logfile);
      break;
    case 'm': /* --mask */
      free(put_glob_mask);
      put_glob_mask = xstrdup(optarg);
      break;
#ifdef HAVE_REGEX
    case 'M': /* --rx-mask */
      if(put_rx_mask_set) {
        regfree(&put_rx_mask);
        put_rx_mask_set = false;
      }

      ret = regcomp(&put_rx_mask, optarg, REG_EXTENDED);
      if(ret != 0) {
        regerror(ret, &put_rx_mask, put_rx_errbuf, sizeof(put_rx_errbuf) - 1);
        ftp_err(_("Regexp '%s' failed: %s\n"), optarg, put_rx_errbuf);
        return;
      } else
        put_rx_mask_set = true;
      break;
#endif
    case 'n':
      opt |= PUT_NEWER;
      break;
    case 'v':
      opt |= PUT_VERBOSE;
      break;
    case 'q':
      opt &= ~PUT_VERBOSE;
      break;
    case 'a':
      opt |= PUT_APPEND;
      break;
    case 'D':
      opt |= PUT_DELETE_AFTER;
      break;
    case 'u':
      opt |= PUT_UNIQUE;
      if(!ftp->has_stou_command) {
        fprintf(stderr, _("Remote doesn't support the STOU"
                  " (store unique) command\n"));
        return;
      }
      break;
    case 'r':
      opt |= PUT_RECURSIVE;
      break;
    case 's':
      opt |= PUT_SKIP_EXISTING;
      break;
    case 'S':
      stat_thresh = optarg ? atoi(optarg) : 0;
      break;
    case 'R':
      opt |= PUT_RESUME;
      break;
    case 't':
      opt |= PUT_TAGGED;
      break;
    case '1':
      if(strncmp(optarg, "ascii", strlen(optarg)) == 0)
        opt |= PUT_ASCII;
      else if(strncmp(optarg, "binary", strlen(optarg)) == 0)
        opt |= PUT_BINARY;
      else {
        printf(_("Invalid option argument --type=%s\n"), optarg);
        return;
      }
      break;
    case 'p':
      opt |= PUT_PRESERVE;
      break;
    case 'P':
      opt |= PUT_PARENTS;
      break;
    case 'h':
      print_put_syntax();
      return;
    case '?':
      return;
    }
  }
	if(optind>=argc && !test(opt, PUT_TAGGED)) {
/*		fprintf(stderr, _("missing argument, try 'put --help'"*/
/*						  " for more information\n"));*/
		minargs(optind);
		return;
	}

	if(test(opt, PUT_APPEND) && test(opt, PUT_SKIP_EXISTING)) {
		printf("Can't use --append and --skip-existing simultaneously\n");
		return;
	}

	need_connected();
	need_loggedin();

	gl = lglob_create();
	while(optind < argc) {
		char* f = tilde_expand_home(argv[optind], gvLocalHomeDir);
		stripslash(f);
		lglob_glob(gl, f, true, put_exclude_func);
		optind++;
    free(f);
	}

	if(list_numitem(gl) == 0) {
		if(!test(opt, PUT_TAGGED)) {
			list_free(gl);
			return;
		} else if(list_numitem(gvLocalTagList) == 0) {
			printf(_("no tagged files\n"));
			list_free(gl);
			return;
		}
	}

	free(ftp->last_mkpath);
	ftp->last_mkpath = 0;

	put_quit = false;
	put_batch = put_owbatch = put_delbatch = test(opt, PUT_FORCE);
	if(test(opt, PUT_FORCE))
		opt &= ~PUT_INTERACTIVE;

	if(put_output && !test(opt, PUT_RECURSIVE) && list_numitem(gl) +
	   (test(opt, PUT_TAGGED) ? list_numitem(gvLocalTagList) : 0) == 1)
		{
			opt |= PUT_OUTPUT_FILE;
		}

	stats_reset(gvStatsTransfer);

	gvInTransfer = true;
	gvInterrupted = false;

	if(test(opt, PUT_NOHUP)) {
		int r = 0;
		pid = fork();

		if(pid == 0) {
			r = transfer_init_nohup(logfile);
			if(r != 0)
				exit(0);
		}

		if(r != 0)
			return;

		if(pid == 0) { /* child process */
			transfer_begin_nohup(argc, argv);

			if(!test(opt, PUT_FORCE) && !test(opt, PUT_RESUME))
				opt |= PUT_UNIQUE;
			opt |= PUT_FORCE;

			putfiles(gl, opt, put_output);
			list_free(gl);
			if(test(opt, PUT_TAGGED)) {
				putfiles(gvLocalTagList, opt, put_output);
				list_clear(gvLocalTagList);
			}
			free(put_output);

			transfer_end_nohup();
		}
		if(pid == -1) {
			perror("fork()");
			return;
		}
		/* parent process */
		sleep(1);
		printf("%d\n", pid);
		input_save_history();
		gvars_destroy();
		reset_xterm_title();
		exit(0);
	}

	putfiles(gl, opt, put_output);
	list_free(gl);
	if(test(opt, PUT_TAGGED)) {
		putfiles(gvLocalTagList, opt, put_output);
		list_clear(gvLocalTagList);
	}
	free(put_output);
	gvInTransfer = false;

	stats_display(gvStatsTransfer, stat_thresh);
}
