/* $Id$ */

/***
  This file is part of polypaudio.

  polypaudio is free software; you can redistribute it and/or modify
  it under the terms of the GNU General Public License as published
  by the Free Software Foundation; either version 2 of the License,
  or (at your option) any later version.

  polypaudio is distributed in the hope that it will be useful, but
  WITHOUT ANY WARRANTY; without even the implied warranty of
  MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the GNU
  General Public License for more details.

  You should have received a copy of the GNU General Public License
  along with polypaudio; if not, write to the Free Software
  Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307
  USA.
***/

#ifdef HAVE_CONFIG_H
#include <config.h>
#endif

#include <errno.h>
#include <stdio.h>
#include <string.h>
#include <assert.h>
#include <unistd.h>
#include <samplerate.h>

#include "daemon-conf.h"
#include "util.h"
#include "xmalloc.h"
#include "strbuf.h"
#include "conf-parser.h"

#ifndef DEFAULT_SCRIPT_FILE
#define DEFAULT_SCRIPT_FILE "/etc/polypaudio/default.pa"
#endif

#ifndef DEFAULT_SCRIPT_FILE_USER
#define DEFAULT_SCRIPT_FILE_USER ".polypaudio/default.pa"
#endif

#ifndef DEFAULT_CONFIG_FILE
#define DEFAULT_CONFIG_FILE "/etc/polypaudio/daemon.conf"
#endif

#ifndef DEFAULT_CONFIG_FILE_USER
#define DEFAULT_CONFIG_FILE_USER ".polypaudio/daemon.conf"
#endif

#define ENV_SCRIPT_FILE "POLYP_SCRIPT"
#define ENV_CONFIG_FILE "POLYP_CONFIG"
#define ENV_DL_SEARCH_PATH "POLYP_DLPATH"

static const struct pa_daemon_conf default_conf = {
    .cmd = PA_CMD_DAEMON,
    .daemonize = 0,
    .fail = 1,
    .verbose = 0,
    .high_priority = 0,
    .disallow_module_loading = 0,
    .exit_idle_time = -1,
    .module_idle_time = 20,
    .scache_idle_time = 20,
    .auto_log_target = 1,
    .script_commands = NULL,
    .dl_search_path = NULL,
    .default_script_file = NULL,
    .log_target = PA_LOG_SYSLOG,
    .resample_method = SRC_SINC_FASTEST
};

char* default_file(const char *envvar, const char *global, const char *local) {
    char *p, *h;

    assert(envvar && global && local);

    if ((p = getenv(envvar)))
        return pa_xstrdup(p);

    if ((h = getenv("HOME"))) {
        p = pa_sprintf_malloc("%s/%s", h, local);
        if (!access(p, F_OK)) 
            return p;
        
        pa_xfree(p);
    }

    return pa_xstrdup(global);
}

struct pa_daemon_conf* pa_daemon_conf_new(void) {
    struct pa_daemon_conf *c = pa_xmemdup(&default_conf, sizeof(default_conf));
    c->default_script_file = default_file(ENV_SCRIPT_FILE, DEFAULT_SCRIPT_FILE, DEFAULT_SCRIPT_FILE_USER);
#ifdef DLSEARCHPATH
    c->dl_search_path = pa_xstrdup(DLSEARCHPATH);
#endif
    return c;
}

void pa_daemon_conf_free(struct pa_daemon_conf *c) {
    assert(c);
    pa_xfree(c->script_commands);
    pa_xfree(c->dl_search_path);
    pa_xfree(c->default_script_file);
    pa_xfree(c);
}

int pa_daemon_conf_set_log_target(struct pa_daemon_conf *c, const char *string) {
    assert(c && string);

    if (!strcmp(string, "auto"))
        c->auto_log_target = 1;
    else if (!strcmp(string, "syslog")) {
        c->auto_log_target = 0;
        c->log_target = PA_LOG_SYSLOG;
    } else if (!strcmp(string, "stderr")) {
        c->auto_log_target = 0;
        c->log_target = PA_LOG_STDERR;
    } else
        return -1;

    return 0;
}

int pa_daemon_conf_set_resample_method(struct pa_daemon_conf *c, const char *string) {
    int m;
    assert(c && string);

    if ((m = pa_parse_resample_method(string)) < 0)
        return -1;

    c->resample_method = m;
    return 0;
}

int parse_log_target(const char *filename, unsigned line, const char *lvalue, const char *rvalue, void *data, void *userdata) {
    struct pa_daemon_conf *c = data;
    assert(filename && lvalue && rvalue && data);

    if (pa_daemon_conf_set_log_target(c, rvalue) < 0) {
        pa_log(__FILE__": [%s:%u] Invalid log target '%s'.\n", filename, line, rvalue);
        return -1;
    }

    return 0;
}

int parse_resample_method(const char *filename, unsigned line, const char *lvalue, const char *rvalue, void *data, void *userdata) {
    struct pa_daemon_conf *c = data;
    assert(filename && lvalue && rvalue && data);

    if (pa_daemon_conf_set_resample_method(c, rvalue) < 0) {
        pa_log(__FILE__": [%s:%u] Inavalid resample method '%s'.\n", filename, line, rvalue);
        return -1;
    }

    return 0;
}

int pa_daemon_conf_load(struct pa_daemon_conf *c, const char *filename) {
    char *def = NULL;
    int r;
    
    struct pa_config_item table[] = {
        { "verbose",                 pa_config_parse_bool,    NULL },
        { "daemonize",               pa_config_parse_bool,    NULL },
        { "fail",                    pa_config_parse_bool,    NULL },
        { "high-priority",           pa_config_parse_bool,    NULL },
        { "disallow-module-loading", pa_config_parse_bool,    NULL },
        { "exit-idle-time",          pa_config_parse_int,     NULL },
        { "module-idle-time",        pa_config_parse_int,     NULL },
        { "scache-idle-time",        pa_config_parse_int,     NULL },
        { "dl-search-path",          pa_config_parse_string,  NULL },
        { "default-script-file",     pa_config_parse_string,  NULL },
        { "log-target",              parse_log_target,        NULL },
        { "resample-method",         parse_resample_method,   NULL },
        { NULL,                      NULL,                    NULL },
    };
    
    table[0].data = &c->verbose;
    table[1].data = &c->daemonize;
    table[2].data = &c->fail;
    table[3].data = &c->high_priority;
    table[4].data = &c->disallow_module_loading;
    table[5].data = &c->exit_idle_time;
    table[6].data = &c->module_idle_time;
    table[7].data = &c->scache_idle_time;
    table[8].data = &c->dl_search_path;
    table[9].data = &c->default_script_file;
    table[10].data = c;
    table[11].data = c;
    
    if (!filename)
        filename = def = default_file(ENV_CONFIG_FILE, DEFAULT_CONFIG_FILE, DEFAULT_CONFIG_FILE_USER);
    
    r = pa_config_parse(filename, table, NULL);
    pa_xfree(def);
    return r;
}

int pa_daemon_conf_env(struct pa_daemon_conf *c) {
    char *e;

    if ((e = getenv(ENV_DL_SEARCH_PATH))) {
        pa_xfree(c->dl_search_path);
        c->dl_search_path = pa_xstrdup(e);
    }
    if ((e = getenv(ENV_SCRIPT_FILE))) {
        pa_xfree(c->default_script_file);
        c->default_script_file = pa_xstrdup(e);
    }

    return 0;
}

char *pa_daemon_conf_dump(struct pa_daemon_conf *c) {
    struct pa_strbuf *s = pa_strbuf_new();
    char *d;

    static const char const* resample_methods[] = {
        "sinc-best-quality",
        "sinc-medium-quality",
        "sinc-fastest",
        "zero-order-hold",
        "linear"
    };

    d = default_file(ENV_CONFIG_FILE, DEFAULT_CONFIG_FILE, DEFAULT_CONFIG_FILE_USER);
    pa_strbuf_printf(s, "### Default configuration file: %s ###\n", d);
    
    pa_strbuf_printf(s, "verbose = %i\n", !!c->verbose);
    pa_strbuf_printf(s, "daemonize = %i\n", !!c->daemonize);
    pa_strbuf_printf(s, "fail = %i\n", !!c->fail);
    pa_strbuf_printf(s, "high-priority = %i\n", !!c->high_priority);
    pa_strbuf_printf(s, "disallow-module-loading = %i\n", !!c->disallow_module_loading);
    pa_strbuf_printf(s, "exit-idle-time = %i\n", c->exit_idle_time);
    pa_strbuf_printf(s, "module-idle-time = %i\n", c->module_idle_time);
    pa_strbuf_printf(s, "scache-idle-time = %i\n", c->scache_idle_time);
    pa_strbuf_printf(s, "dl-search-path = %s\n", c->dl_search_path ? c->dl_search_path : "");
    pa_strbuf_printf(s, "default-script-file = %s\n", c->default_script_file);
    pa_strbuf_printf(s, "log-target = %s\n", c->auto_log_target ? "auto" : (c->log_target == PA_LOG_SYSLOG ? "syslog" : "stderr"));

    assert(c->resample_method <= 4 && c->resample_method >= 0);
    pa_strbuf_printf(s, "resample-method = %s\n", resample_methods[c->resample_method]);
    
    pa_xfree(d);
    
    return pa_strbuf_tostring_free(s);
}
