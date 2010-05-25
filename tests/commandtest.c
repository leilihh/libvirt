/*
 * commandtest.c: Test the libCommand API
 *
 * Copyright (C) 2010 Red Hat, Inc.
 *
 * This library is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This library is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this library; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307  USA
 */

#include <config.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <signal.h>
#include <sys/stat.h>
#include <fcntl.h>

#include "testutils.h"
#include "internal.h"
#include "nodeinfo.h"
#include "util.h"
#include "memory.h"
#include "command.h"
#include "files.h"

#ifdef WIN32

static int
mymain(int argc ATTRIBUTE_UNUSED, char **argv ATTRIBUTE_UNUSED)
{
    exit (EXIT_AM_SKIP);
}

#else

static char *progname;
static char *abs_srcdir;


static int checkoutput(const char *testname) {
    int ret = -1;
    char cwd[1024];
    char *expectname = NULL;
    char *expectlog = NULL;
    char *actualname = NULL;
    char *actuallog = NULL;

    if (!getcwd(cwd, sizeof(cwd)))
        return -1;

    if (virAsprintf(&expectname, "%s/commanddata/%s.log", abs_srcdir,
                    testname) < 0)
        goto cleanup;
    if (virAsprintf(&actualname, "%s/commandhelper.log", abs_builddir) < 0)
        goto cleanup;

    if (virFileReadAll(expectname, 1024*64, &expectlog) < 0) {
        fprintf(stderr, "cannot read %s\n", expectname);
        goto cleanup;
    }

    if (virFileReadAll(actualname, 1024*64, &actuallog) < 0) {
        fprintf(stderr, "cannot read %s\n", actualname);
        goto cleanup;
    }

    if (STRNEQ(expectlog, actuallog)) {
        virtTestDifference(stderr, expectlog, actuallog);
        goto cleanup;
    }


    ret = 0;

cleanup:
    unlink(actuallog);
    VIR_FREE(actuallog);
    VIR_FREE(actualname);
    VIR_FREE(expectlog);
    VIR_FREE(expectname);
    return ret;
}

/*
 * Run program, no args, inherit all ENV, keep CWD.
 * Only stdin/out/err open
 * No slot for return status must log error.
 */
static int test0(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd;
    char *log;
    int ret = -1;

    free(virtTestLogContentAndReset());
    cmd = virCommandNew(abs_builddir "/commandhelper-doesnotexist");
    if (virCommandRun(cmd, NULL) == 0)
        goto cleanup;
    if ((log = virtTestLogContentAndReset()) == NULL)
        goto cleanup;
    if (strstr(log, ": error :") == NULL)
        goto cleanup;
    virResetLastError();
    ret = 0;

cleanup:
    virCommandFree(cmd);
    return ret;
}

/*
 * Run program, no args, inherit all ENV, keep CWD.
 * Only stdin/out/err open
 * Capturing return status must not log error.
 */
static int test1(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd;
    int ret = -1;
    int status;

    cmd = virCommandNew(abs_builddir "/commandhelper-doesnotexist");
    if (virCommandRun(cmd, &status) < 0)
        goto cleanup;
    if (status == 0)
        goto cleanup;
    ret = 0;

cleanup:
    virCommandFree(cmd);
    return ret;
}

/*
 * Run program (twice), no args, inherit all ENV, keep CWD.
 * Only stdin/out/err open
 */
static int test2(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");
    int ret;

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    if ((ret = checkoutput("test2")) != 0) {
        virCommandFree(cmd);
        return ret;
    }

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    return checkoutput("test2");
}

/*
 * Run program, no args, inherit all ENV, keep CWD.
 * stdin/out/err + two extra FD open
 */
static int test3(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");
    int newfd1 = dup(STDERR_FILENO);
    int newfd2 = dup(STDERR_FILENO);
    int newfd3 = dup(STDERR_FILENO);

    virCommandPreserveFD(cmd, newfd1);
    virCommandTransferFD(cmd, newfd3);

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    if (fcntl(newfd1, F_GETFL) < 0 ||
        fcntl(newfd2, F_GETFL) < 0 ||
        fcntl(newfd3, F_GETFL) >= 0) {
        puts("fds in wrong state");
        return -1;
    }

    virCommandFree(cmd);
    VIR_FORCE_CLOSE(newfd1);
    VIR_FORCE_CLOSE(newfd2);

    return checkoutput("test3");
}


/*
 * Run program, no args, inherit all ENV, CWD is /
 * Only stdin/out/err open.
 * Daemonized
 */
static int test4(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");
    pid_t pid;
    char *pidfile = virFilePid(abs_builddir, "commandhelper");

    virCommandSetPidFile(cmd, pidfile);
    virCommandDaemonize(cmd);

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    if (virFileReadPid(abs_builddir, "commandhelper", &pid) != 0) {
        printf("cannot read pidfile\n");
        return -1;
    }
    while (kill(pid, 0) != -1)
        usleep(100*1000);

    virCommandFree(cmd);

    VIR_FREE(pidfile);

    return checkoutput("test4");
}


/*
 * Run program, no args, inherit filtered ENV, keep CWD.
 * Only stdin/out/err open
 */
static int test5(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");

    virCommandAddEnvPassCommon(cmd);

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    return checkoutput("test5");
}


/*
 * Run program, no args, inherit filtered ENV, keep CWD.
 * Only stdin/out/err open
 */
static int test6(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");

    virCommandAddEnvPass(cmd, "DISPLAY");
    virCommandAddEnvPass(cmd, "DOESNOTEXIST");

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    return checkoutput("test6");
}


/*
 * Run program, no args, inherit filtered ENV, keep CWD.
 * Only stdin/out/err open
 */
static int test7(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");

    virCommandAddEnvPassCommon(cmd);
    virCommandAddEnvPass(cmd, "DISPLAY");
    virCommandAddEnvPass(cmd, "DOESNOTEXIST");

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    return checkoutput("test7");
}

/*
 * Run program, no args, inherit filtered ENV, keep CWD.
 * Only stdin/out/err open
 */
static int test8(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");

    virCommandAddEnvString(cmd, "LANG=C");
    virCommandAddEnvPair(cmd, "USER", "test");

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    return checkoutput("test8");
}


/*
 * Run program, some args, inherit all ENV, keep CWD.
 * Only stdin/out/err open
 */
static int test9(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");
    const char* const args[] = { "arg1", "arg2", NULL };

    virCommandAddArg(cmd, "-version");
    virCommandAddArgPair(cmd, "-log", "bar.log");
    virCommandAddArgSet(cmd, args);
    virCommandAddArgList(cmd, "arg3", "arg4", NULL);

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    return checkoutput("test9");
}


/*
 * Run program, some args, inherit all ENV, keep CWD.
 * Only stdin/out/err open
 */
static int test10(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");
    const char *const args[] = {
        "-version", "-log=bar.log", NULL,
    };

    virCommandAddArgSet(cmd, args);

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    return checkoutput("test10");
}

/*
 * Run program, some args, inherit all ENV, keep CWD.
 * Only stdin/out/err open
 */
static int test11(const void *unused ATTRIBUTE_UNUSED) {
    const char *args[] = {
        abs_builddir "/commandhelper",
        "-version", "-log=bar.log", NULL,
    };
    virCommandPtr cmd = virCommandNewArgs(args);

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    return checkoutput("test11");
}

/*
 * Run program, no args, inherit all ENV, keep CWD.
 * Only stdin/out/err open. Set stdin data
 */
static int test12(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");

    virCommandSetInputBuffer(cmd, "Hello World\n");

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    return checkoutput("test12");
}

/*
 * Run program, no args, inherit all ENV, keep CWD.
 * Only stdin/out/err open. Set stdin data
 */
static int test13(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");
    char *outactual = NULL;
    const char *outexpect = "BEGIN STDOUT\n"
        "Hello World\n"
        "END STDOUT\n";
    int ret = -1;

    virCommandSetInputBuffer(cmd, "Hello World\n");
    virCommandSetOutputBuffer(cmd, &outactual);

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    if (!STREQ(outactual, outexpect)) {
        virtTestDifference(stderr, outactual, outexpect);
        goto cleanup;
    }

    if (checkoutput("test13") < 0)
        goto cleanup;

    ret = 0;

cleanup:
    VIR_FREE(outactual);
    return ret;
}

/*
 * Run program, no args, inherit all ENV, keep CWD.
 * Only stdin/out/err open. Set stdin data
 */
static int test14(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");
    char *outactual = NULL;
    const char *outexpect = "BEGIN STDOUT\n"
        "Hello World\n"
        "END STDOUT\n";
    char *erractual = NULL;
    const char *errexpect = "BEGIN STDERR\n"
        "Hello World\n"
        "END STDERR\n";
    int ret = -1;

    virCommandSetInputBuffer(cmd, "Hello World\n");
    virCommandSetOutputBuffer(cmd, &outactual);
    virCommandSetErrorBuffer(cmd, &erractual);

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    if (!STREQ(outactual, outexpect)) {
        virtTestDifference(stderr, outactual, outexpect);
        goto cleanup;
    }
    if (!STREQ(erractual, errexpect)) {
        virtTestDifference(stderr, erractual, errexpect);
        goto cleanup;
    }

    if (checkoutput("test14") < 0)
        goto cleanup;

    ret = 0;

cleanup:
    VIR_FREE(outactual);
    VIR_FREE(erractual);
    return ret;
}


/*
 * Run program, no args, inherit all ENV, change CWD.
 * Only stdin/out/err open
 */
static int test15(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew(abs_builddir "/commandhelper");

    virCommandSetWorkingDirectory(cmd, abs_builddir "/commanddata");

    if (virCommandRun(cmd, NULL) < 0) {
        virErrorPtr err = virGetLastError();
        printf("Cannot run child %s\n", err->message);
        return -1;
    }

    virCommandFree(cmd);

    return checkoutput("test15");
}

/*
 * Don't run program; rather, log what would be run.
 */
static int test16(const void *unused ATTRIBUTE_UNUSED) {
    virCommandPtr cmd = virCommandNew("/bin/true");
    char *outactual = NULL;
    const char *outexpect = "A=B /bin/true C";
    int ret = -1;
    int fd = -1;

    virCommandAddEnvPair(cmd, "A", "B");
    virCommandAddArg(cmd, "C");

    if ((outactual = virCommandToString(cmd)) == NULL) {
        virErrorPtr err = virGetLastError();
        printf("Cannot convert to string: %s\n", err->message);
        return -1;
    }
    if ((fd = open(abs_builddir "/commandhelper.log",
                   O_CREAT | O_TRUNC | O_WRONLY, 0600)) < 0) {
        printf("Cannot open log file: %s\n", strerror (errno));
        goto cleanup;
    }
    virCommandWriteArgLog(cmd, fd);
    if (VIR_CLOSE(fd) < 0) {
        printf("Cannot close log file: %s\n", strerror (errno));
        goto cleanup;
    }

    virCommandFree(cmd);

    if (checkoutput("test16") < 0)
        goto cleanup;

    if (!STREQ(outactual, outexpect)) {
        virtTestDifference(stderr, outactual, outexpect);
        goto cleanup;
    }
    ret = 0;

cleanup:
    VIR_FORCE_CLOSE(fd);
    VIR_FREE(outactual);
    return ret;
}

static int
mymain(int argc, char **argv)
{
    int ret = 0;
    char cwd[PATH_MAX];

    abs_srcdir = getenv("abs_srcdir");
    if (!abs_srcdir)
        abs_srcdir = getcwd(cwd, sizeof(cwd));

    progname = argv[0];

    if (argc > 1) {
        fprintf(stderr, "Usage: %s\n", progname);
        return(EXIT_FAILURE);
    }

    if (chdir("/tmp") < 0)
        return(EXIT_FAILURE);

    virInitialize();

    const char *const newenv[] = {
        "PATH=/usr/bin:/bin",
        "HOSTNAME=test",
        "LANG=C",
        "HOME=/home/test",
        "USER=test",
        "LOGNAME=test"
        "TMPDIR=/tmp",
        "DISPLAY=:0.0",
        NULL
    };
    environ = (char **)newenv;

# define DO_TEST(NAME)                                                \
    if (virtTestRun("Command Exec " #NAME " test",                    \
                    1, NAME, NULL) < 0)                               \
        ret = -1

    char *actualname;
    if (virAsprintf(&actualname, "%s/commandhelper.log", abs_builddir) < 0)
        return EXIT_FAILURE;
    unlink(actualname);
    VIR_FREE(actualname);

    DO_TEST(test0);
    DO_TEST(test1);
    DO_TEST(test2);
    DO_TEST(test3);
    DO_TEST(test4);
    DO_TEST(test5);
    DO_TEST(test6);
    DO_TEST(test7);
    DO_TEST(test8);
    DO_TEST(test9);
    DO_TEST(test10);
    DO_TEST(test11);
    DO_TEST(test12);
    DO_TEST(test13);
    DO_TEST(test14);
    DO_TEST(test15);
    DO_TEST(test16);

    return(ret==0 ? EXIT_SUCCESS : EXIT_FAILURE);
}

#endif /* !WIN32 */

VIRT_TEST_MAIN(mymain)