/*
 * input.h -- string input and readline stuff
 *
 * Yet Another FTP Client
 * Copyright (C) 1998-2001, Martin Hedenfalk <mhe@stacken.kth.se>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version. See COPYING for more details.
 */

#ifndef _input_h_included
#define _input_h_included

#include "syshdr.h"
#include "args.h"

#define HISTORY_FILENAME ".yafc_history"

#define ASKYES 1
#define ASKNO 2
#define ASKCANCEL 4
#define ASKALL 8
#define ASKUNIQUE 16
#define ASKRESUME 32

extern bool readline_running;

char *getpass_hook(const char *prompt);
char *getuser_hook(const char *prompt);

void input_init(void);
char *input_read_string(const char* fmt, ...) YAFC_PRINTF(1, 2);
int input_read_args(args_t **args, const char* fmt, ...) YAFC_PRINTF(2, 3);
void input_save_history(void);
void input_redisplay_prompt(void);
int ask(int opt, int def, const char *prompt, ...) YAFC_PRINTF(3, 4);

char *make_dequoted_filename(const char *text, int quote_char);

#endif
