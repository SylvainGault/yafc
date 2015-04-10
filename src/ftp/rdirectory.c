/*
 * rdirectory.c -- representation of a remote directory
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
#include "strq.h"

rdirectory *rdir_create(void)
{
  rdirectory* rdir = xmalloc(sizeof(rdirectory));
  rdir->files = list_new((listfunc)rfile_destroy);
  rdir->timestamp = time(0);

  return rdir;
}

void rdir_destroy(rdirectory *rdir)
{
  if (!rdir)
    return;

  list_free(rdir->files);
  free(rdir->path);
  free(rdir);
}

unsigned long int rdir_size(rdirectory *rdir)
{
  return rglob_size(rdir->files);
}

int rdir_parse(rdirectory *rdir, FILE *fp, const char *path, bool is_mlsd)
{
	rfile *f;
	int r;
	bool failed = false;

	free(rdir->path);
	rdir->path = NULL;
	list_clear(rdir->files);
	rdir->timestamp = time(0);

	f = rfile_create();

	ftp_trace("*** start parsing directory listing of '%s' ***\n", path);

	while(!feof(fp)) {
		char tmp[512];
		if(fgets(tmp, sizeof(tmp), fp) == 0)
			break;
		strip_trailing_chars(tmp, "\r\n");
		if(!tmp[0])
			break;
		ftp_trace("%s\n", tmp);

		rfile_clear(f);
		r = rfile_parse(f, tmp, path, is_mlsd);
		if(r == -1) {
			ftp_err("parsing failed on '%s'\n", tmp);
			list_clear(rdir->files);
			failed = true;
		} else if(r == 0)
			list_additem(rdir->files, (void *)rfile_clone(f));
		/* else r == 1, ie a 'total ###' line, which isn't an error */
	}
  rfile_destroy(f);
	ftp_trace("*** end parsing directory listing ***\n");
	if(failed) {
		if(list_numitem(rdir->files) == 0) {
			ftp_err("directory parsing failed completely\n");
			return -1;
		}
	}
	rdir->path = xstrdup(path);
	return 0;
}

rfile *rdir_get_file(rdirectory *rdir, const char *filename)
{
  listitem* li = list_search(rdir->files, (listsearchfunc)rfile_search_filename,
      filename);
  if (li)
    return (rfile*)li->data;
  return NULL;
}

static int compare_files(const void* A, const void* B)
{
  const rfile* a = A;
  const rfile* b = B;

  if (a == b)
    return 0;

  return strcmp(a->path, b->path);
}

void rdir_sort(rdirectory* dir)
{
  if (!dir)
    return;

  list_sort(dir->files, compare_files, false);
}
