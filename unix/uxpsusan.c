/*
 * 'psusan': Pseudo Ssh for Untappable, Separately Authenticated Networks
 *
 * This is a standalone application that speaks on its standard I/O
 * the server end of the bare ssh-connection protocol used by PuTTY's
 * connection sharing.
 */

#include <stdio.h>
#include <stdlib.h>
#include <errno.h>
#include <assert.h>
#include <stdarg.h>
#include <signal.h>
#include <unistd.h>
#include <fcntl.h>
#include <termios.h>
#include <pwd.h>
#include <sys/ioctl.h>
#include <sys/time.h>

#include "putty.h"
#include "mpint.h"
#include "ssh.h"
#include "sshserver.h"

const char *const appname = "psusan";

void modalfatalbox(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "FATAL ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
    exit(1);
}
void nonfatal(const char *p, ...)
{
    va_list ap;
    fprintf(stderr, "ERROR: ");
    va_start(ap, p);
    vfprintf(stderr, p, ap);
    va_end(ap);
    fputc('\n', stderr);
}

char *platform_default_s(const char *name)
{
    return NULL;
}

bool platform_default_b(const char *name, bool def)
{
    return def;
}

int platform_default_i(const char *name, int def)
{
    return def;
}

FontSpec *platform_default_fontspec(const char *name)
{
    return fontspec_new("");
}

Filename *platform_default_filename(const char *name)
{
    return filename_from_str("");
}

char *x_get_default(const char *key)
{
    return NULL;                       /* this is a stub */
}

void old_keyfile_warning(void) { }

void timer_change_notify(unsigned long next)
{
}

char *platform_get_x_display(void) { return NULL; }

static bool verbose;

struct server_instance {
    unsigned id;
    LogPolicy logpolicy;
};

static void log_to_stderr(unsigned id, const char *msg)
{
    if (id != (unsigned)-1)
        fprintf(stderr, "#%u: ", id);
    fputs(msg, stderr);
    fputc('\n', stderr);
    fflush(stderr);
}

static void server_eventlog(LogPolicy *lp, const char *event)
{
    struct server_instance *inst = container_of(
        lp, struct server_instance, logpolicy);
    if (verbose)
        log_to_stderr(inst->id, event);
}

static void server_logging_error(LogPolicy *lp, const char *event)
{
    struct server_instance *inst = container_of(
        lp, struct server_instance, logpolicy);
    log_to_stderr(inst->id, event);    /* unconditional */
}

static int server_askappend(
    LogPolicy *lp, Filename *filename,
    void (*callback)(void *ctx, int result), void *ctx)
{
    return 2; /* always overwrite (FIXME: could make this a cmdline option) */
}

static const LogPolicyVtable server_logpolicy_vt = {
    .eventlog = server_eventlog,
    .askappend = server_askappend,
    .logging_error = server_logging_error,
    .verbose = null_lp_verbose_no,
};

static void show_help(FILE *fp)
{
    fputs("usage:   psusan [options]\n"
          "options: --sessiondir DIR     cwd for session subprocess (default $HOME)\n"
          "         --sshlog FILE        write ssh-connection packet log to FILE\n"
          "         --sshrawlog FILE     write packets and raw data log to FILE\n"
          "also:    psusan --help        show this text\n"
          "         psusan --version     show version information\n", fp);
}

static void show_version_and_exit(void)
{
    char *buildinfo_text = buildinfo("\n");
    printf("%s: %s\n%s\n", appname, ver, buildinfo_text);
    sfree(buildinfo_text);
    exit(0);
}

const bool buildinfo_gtk_relevant = false;

static bool finished = false;
void server_instance_terminated(LogPolicy *lp)
{
    struct server_instance *inst = container_of(
        lp, struct server_instance, logpolicy);

    finished = true;

    sfree(inst);
}

bool psusan_continue(void *ctx, bool fd, bool cb)
{
    return !finished;
}

static bool longoptarg(const char *arg, const char *expected,
                       const char **val, int *argcp, char ***argvp)
{
    int len = strlen(expected);
    if (memcmp(arg, expected, len))
        return false;
    if (arg[len] == '=') {
        *val = arg + len + 1;
        return true;
    } else if (arg[len] == '\0') {
        if (--*argcp > 0) {
            *val = *++*argvp;
            return true;
        } else {
            fprintf(stderr, "%s: option %s expects an argument\n",
                    appname, expected);
            exit(1);
        }
    }
    return false;
}

static bool longoptnoarg(const char *arg, const char *expected)
{
    int len = strlen(expected);
    if (memcmp(arg, expected, len))
        return false;
    if (arg[len] == '=') {
        fprintf(stderr, "%s: option %s expects no argument\n",
                appname, expected);
        exit(1);
    } else if (arg[len] == '\0') {
        return true;
    }
    return false;
}

struct server_config {
    Conf *conf;
    const SshServerConfig *ssc;
    unsigned next_id;
};

static Plug *server_conn_plug(
    struct server_config *cfg, struct server_instance **inst_out)
{
    struct server_instance *inst = snew(struct server_instance);

    memset(inst, 0, sizeof(*inst));

    inst->id = cfg->next_id++;
    inst->logpolicy.vt = &server_logpolicy_vt;

    if (inst_out)
        *inst_out = inst;

    return ssh_server_plug(
        cfg->conf, cfg->ssc, NULL, 0, NULL, NULL,
        &inst->logpolicy, &unix_live_sftpserver_vt);
}

unsigned auth_methods(AuthPolicy *ap)
{ return 0; }
bool auth_none(AuthPolicy *ap, ptrlen username)
{ return false; }
int auth_password(AuthPolicy *ap, ptrlen username, ptrlen password,
                  ptrlen *new_password_opt)
{ return 0; }
bool auth_publickey(AuthPolicy *ap, ptrlen username, ptrlen public_blob)
{ return false; }
RSAKey *auth_publickey_ssh1(
    AuthPolicy *ap, ptrlen username, mp_int *rsa_modulus)
{ return NULL; }
AuthKbdInt *auth_kbdint_prompts(AuthPolicy *ap, ptrlen username)
{ return NULL; }
int auth_kbdint_responses(AuthPolicy *ap, const ptrlen *responses)
{ return -1; }
char *auth_ssh1int_challenge(AuthPolicy *ap, unsigned method, ptrlen username)
{ return NULL; }
bool auth_ssh1int_response(AuthPolicy *ap, ptrlen response)
{ return false; }
bool auth_successful(AuthPolicy *ap, ptrlen username, unsigned method)
{ return false; }

int main(int argc, char **argv)
{
    SshServerConfig ssc;

    Conf *conf = make_ssh_server_conf();

    memset(&ssc, 0, sizeof(ssc));

    ssc.session_starting_dir = getenv("HOME");
    ssc.bare_connection = true;

    while (--argc > 0) {
        const char *arg = *++argv;
        const char *val;

        if (longoptnoarg(arg, "--help")) {
            show_help(stdout);
            exit(0);
        } else if (longoptnoarg(arg, "--version")) {
            show_version_and_exit();
        } else if (longoptarg(arg, "--sessiondir", &val, &argc, &argv)) {
            ssc.session_starting_dir = val;
        } else if (longoptarg(arg, "--sshlog", &val, &argc, &argv) ||
                   longoptarg(arg, "-sshlog", &val, &argc, &argv)) {
            Filename *logfile = filename_from_str(val);
            conf_set_filename(conf, CONF_logfilename, logfile);
            filename_free(logfile);
            conf_set_int(conf, CONF_logtype, LGTYP_PACKETS);
            conf_set_int(conf, CONF_logxfovr, LGXF_OVR);
        } else if (longoptarg(arg, "--sshrawlog", &val, &argc, &argv) ||
                   longoptarg(arg, "-sshrawlog", &val, &argc, &argv)) {
            Filename *logfile = filename_from_str(val);
            conf_set_filename(conf, CONF_logfilename, logfile);
            filename_free(logfile);
            conf_set_int(conf, CONF_logtype, LGTYP_SSHRAW);
            conf_set_int(conf, CONF_logxfovr, LGXF_OVR);
        } else {
            fprintf(stderr, "%s: unrecognised option '%s'\n", appname, arg);
            exit(1);
        }
    }

    sk_init();
    uxsel_init();

    struct server_config scfg;
    scfg.conf = conf;
    scfg.ssc = &ssc;
    scfg.next_id = 0;

    struct server_instance *inst;
    Plug *plug = server_conn_plug(&scfg, &inst);
    ssh_server_start(plug, make_fd_socket(0, 1, -1, plug));

    cli_main_loop(cliloop_no_pw_setup, cliloop_no_pw_check,
                  psusan_continue, NULL);

    return 0;
}