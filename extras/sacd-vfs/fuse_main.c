/*
 * This file is part of DSD-Nexus.
 * Copyright (c) 2026 Alexander Wichers
 *
 * @brief SACD FUSE Mount - Main Entry Point
 * Command-line interface for mounting SACD overlay filesystem using FUSE.
 * Works with both native libfuse3 (Linux/macOS) and WinFSP (Windows).
 * Usage: sacd-mount [options] <source_dir> <mount_point>
 *
 * DSD-Nexus is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * DSD-Nexus is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with DSD-Nexus; if not, see <https://www.gnu.org/licenses/>.
 */

#include "fuse_ops.h"

#include <libsacdvfs/sacd_overlay.h>
#include <libsautil/log.h>

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#ifdef _WIN32
#include <windows.h>
#include <io.h>
#define access _access
#define R_OK 4
#else
#include <signal.h>
#include <unistd.h>
#include <getopt.h>
#endif

/* =============================================================================
 * Configuration
 * ===========================================================================*/

typedef struct {
    char *source_dir;
    char *mount_point;
    int threads;
    int cache_timeout;
    int max_isos;
    int foreground;
    int debug;
    int help;
    int stereo;         /* 1 = show stereo, 0 = hide (with fallback) */
    int multichannel;   /* 1 = show multichannel, 0 = hide (with fallback) */
} mount_options_t;

static mount_options_t g_options = {
    NULL,   /* source_dir */
    NULL,   /* mount_point */
    0,      /* threads - Auto */
    300,    /* cache_timeout - 5 minutes */
    0,      /* max_isos - Unlimited */
    0,      /* foreground */
    0,      /* debug */
    0,      /* help */
    1,      /* stereo - Show by default */
    1       /* multichannel - Show by default */
};

/* Global context for signal handler */
static sacd_overlay_ctx_t *g_ctx = NULL;
static struct fuse *g_fuse = NULL;

/* =============================================================================
 * Signal Handling
 * ===========================================================================*/

#ifdef _WIN32
static BOOL WINAPI console_ctrl_handler(DWORD ctrl_type)
{
    switch (ctrl_type) {
    case CTRL_C_EVENT:
    case CTRL_BREAK_EVENT:
    case CTRL_CLOSE_EVENT:
        fprintf(stderr, "\nShutting down...\n");
        if (g_fuse) {
            fuse_exit(g_fuse);
        }
        return TRUE;
    default:
        return FALSE;
    }
}

static void setup_signal_handlers(void)
{
    SetConsoleCtrlHandler(console_ctrl_handler, TRUE);
}

#else
static void signal_handler(int sig)
{
    (void)sig;
    if (g_fuse) {
        fuse_exit(g_fuse);
    }
}

static void setup_signal_handlers(void)
{
    struct sigaction sa;
    sa.sa_handler = signal_handler;
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = 0;

    sigaction(SIGINT, &sa, NULL);
    sigaction(SIGTERM, &sa, NULL);
    sigaction(SIGHUP, &sa, NULL);
}
#endif

/* =============================================================================
 * Usage and Help
 * ===========================================================================*/

static void print_usage(const char *prog)
{
    fprintf(stderr,
        "SACD Overlay Mount - Mount SACD ISOs as virtual directories\n"
        "\n"
        "Usage: %s [options] <source_dir> <mount_point>\n"
        "\n"
        "Options:\n"
#ifdef _WIN32
        "  /threads:N          Number of DST decoder threads (default: auto)\n"
        "  /cache_timeout:N    ISO cache timeout in seconds (default: 300)\n"
        "  /max_isos:N         Maximum concurrent ISO mounts (default: unlimited)\n"
        "  /no_stereo          Hide stereo area (unless it's the only area)\n"
        "  /no_multichannel    Hide multichannel area (unless it's the only area)\n"
        "  /f                  Foreground mode (don't daemonize)\n"
        "  /d                  Debug mode (implies /f, verbose logging)\n"
        "  /h, /help           Show this help message\n"
#else
        "  -o threads=N        Number of DST decoder threads (default: auto)\n"
        "  -o cache_timeout=N  ISO cache timeout in seconds (default: 300)\n"
        "  -o max_isos=N       Maximum concurrent ISO mounts (default: unlimited)\n"
        "  -o no_stereo        Hide stereo area (unless it's the only area)\n"
        "  -o no_multichannel  Hide multichannel area (unless it's the only area)\n"
        "  -f                  Foreground mode (don't daemonize)\n"
        "  -d                  Debug mode (implies -f, verbose logging)\n"
        "  -h, --help          Show this help message\n"
#endif
        "\n"
        "Examples:\n"
#ifdef _WIN32
        "  %s D:\\SACD S:\n"
        "  %s /threads:4 /no_multichannel D:\\SACD S:\n"
#else
        "  %s /media/sacd /mnt/sacd-vfs\n"
        "  %s -f -o threads=4 -o no_multichannel /media/sacd /mnt/sacd-vfs\n"
#endif
        "\n"
        "When mounted, SACD ISO files appear as directories containing DSF files.\n"
        "The original directory structure is preserved (shadow copied).\n"
        "\n",
        prog, prog, prog);
}

/* =============================================================================
 * Option Parsing
 * ===========================================================================*/

#ifdef _WIN32
static int parse_options(int argc, char *argv[])
{
    int i;
    int nonopt = 0;

    for (i = 1; i < argc; i++) {
        if (argv[i][0] == '/' || argv[i][0] == '-') {
            char *opt = argv[i] + 1;

            if (strncmp(opt, "threads:", 8) == 0) {
                g_options.threads = atoi(opt + 8);
            } else if (strncmp(opt, "cache_timeout:", 14) == 0) {
                g_options.cache_timeout = atoi(opt + 14);
            } else if (strncmp(opt, "max_isos:", 9) == 0) {
                g_options.max_isos = atoi(opt + 9);
            } else if (strcmp(opt, "f") == 0) {
                g_options.foreground = 1;
            } else if (strcmp(opt, "d") == 0) {
                g_options.debug = 1;
                g_options.foreground = 1;
            } else if (strcmp(opt, "no_stereo") == 0) {
                g_options.stereo = 0;
            } else if (strcmp(opt, "no_multichannel") == 0) {
                g_options.multichannel = 0;
            } else if (strcmp(opt, "h") == 0 || strcmp(opt, "help") == 0 ||
                       strcmp(opt, "?") == 0) {
                g_options.help = 1;
                return 0;
            } else {
                fprintf(stderr, "Unknown option: %s\n", argv[i]);
                return -1;
            }
        } else {
            /* Non-option argument */
            if (nonopt == 0) {
                g_options.source_dir = argv[i];
            } else if (nonopt == 1) {
                g_options.mount_point = argv[i];
            }
            nonopt++;
        }
    }

    if (nonopt < 2 && !g_options.help) {
        fprintf(stderr, "Error: Missing source directory or mount point\n\n");
        return -1;
    }

    return 0;
}

#else
static int parse_options(int argc, char *argv[])
{
    static struct option long_options[] = {
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };

    int opt;
    while ((opt = getopt_long(argc, argv, "fdo:h", long_options, NULL)) != -1) {
        switch (opt) {
        case 'f':
            g_options.foreground = 1;
            break;
        case 'd':
            g_options.debug = 1;
            g_options.foreground = 1;
            break;
        case 'o':
            /* Parse -o options */
            if (strncmp(optarg, "threads=", 8) == 0) {
                g_options.threads = atoi(optarg + 8);
            } else if (strncmp(optarg, "cache_timeout=", 14) == 0) {
                g_options.cache_timeout = atoi(optarg + 14);
            } else if (strncmp(optarg, "max_isos=", 9) == 0) {
                g_options.max_isos = atoi(optarg + 9);
            } else if (strcmp(optarg, "no_stereo") == 0) {
                g_options.stereo = 0;
            } else if (strcmp(optarg, "no_multichannel") == 0) {
                g_options.multichannel = 0;
            }
            /* Other -o options are passed to FUSE */
            break;
        case 'h':
            g_options.help = 1;
            return 0;
        default:
            return -1;
        }
    }

    /* Need at least source_dir and mount_point */
    if (optind + 2 > argc) {
        if (!g_options.help) {
            fprintf(stderr, "Error: Missing source directory or mount point\n");
            return -1;
        }
        return 0;
    }

    g_options.source_dir = argv[optind];
    g_options.mount_point = argv[optind + 1];

    return 0;
}
#endif

/* =============================================================================
 * Main
 * ===========================================================================*/

int main(int argc, char *argv[])
{
    int ret = 1;

    /* Parse our options first */
    if (parse_options(argc, argv) != 0) {
        print_usage(argv[0]);
        return 1;
    }

    if (g_options.help) {
        print_usage(argv[0]);
        return 0;
    }

    /* Verify source directory exists */
    if (access(g_options.source_dir, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot access source directory: %s\n",
                g_options.source_dir);
        return 1;
    }

    /* Verify mount point exists (on Unix) */
#ifndef _WIN32
    if (access(g_options.mount_point, R_OK) != 0) {
        fprintf(stderr, "Error: Cannot access mount point: %s\n",
                g_options.mount_point);
        return 1;
    }
#endif

    /* Configure log level */
    if (g_options.debug) {
        sa_log_set_level(SA_LOG_DEBUG);
    }

    /* Create overlay context */
    sacd_overlay_config_t config;
    sacd_overlay_config_init(&config);
    config.source_dir = g_options.source_dir;
    config.thread_pool_size = g_options.threads;
    config.cache_timeout_seconds = g_options.cache_timeout;
    config.max_open_isos = g_options.max_isos;
    config.stereo_visible = g_options.stereo ? true : false;
    config.multichannel_visible = g_options.multichannel ? true : false;

    g_ctx = sacd_overlay_create(&config);
    if (!g_ctx) {
        fprintf(stderr, "Error: Failed to create overlay context\n");
        return 1;
    }

    sacd_fuse_set_context(g_ctx);

    /* Setup signal handlers */
    setup_signal_handlers();

    /* Build FUSE arguments */
    char *fuse_argv[32];
    int fuse_argc = 0;

    fuse_argv[fuse_argc++] = argv[0];

#ifdef _WIN32
    /* WinFSP handles -f, -d, and mountpoint through fuse_new() args */
    if (g_options.foreground) {
        fuse_argv[fuse_argc++] = (char *)"-f";
    }

    if (g_options.debug) {
        fuse_argv[fuse_argc++] = (char *)"-d";
    }

    /* WinFSP options for write support */
    fuse_argv[fuse_argc++] = (char *)"-o";
    fuse_argv[fuse_argc++] = (char *)"uid=-1,gid=-1";  /* Match any user */

    /* WinFSP needs mount point in args */
    fuse_argv[fuse_argc++] = g_options.mount_point;
#else
    /* libfuse3: Only pass -o suboptions to fuse_new(). Foreground mode
     * is handled via fuse_daemonize() and mount point via fuse_mount(). */
    if (g_options.debug) {
        fuse_argv[fuse_argc++] = (char *)"-o";
        fuse_argv[fuse_argc++] = (char *)"debug";
    }
#endif

    /* Initialize FUSE operations */
    struct fuse_operations ops;
    sacd_fuse_init_ops(&ops);

    /* Parse FUSE arguments */
    struct fuse_args args = FUSE_ARGS_INIT(fuse_argc, fuse_argv);

    /* Create FUSE instance */
    g_fuse = fuse_new(&args, &ops, sizeof(ops), g_ctx);
    if (!g_fuse) {
        fprintf(stderr, "Error: Failed to create FUSE instance\n");
        goto cleanup;
    }

    /* Mount */
    if (fuse_mount(g_fuse, g_options.mount_point) != 0) {
        fprintf(stderr, "Error: Failed to mount filesystem at %s\n",
                g_options.mount_point);
        goto cleanup;
    }

    printf("SACD VFS mounted: %s -> %s\n",
           g_options.source_dir, g_options.mount_point);
    printf("Press Ctrl+C to unmount...\n\n");

#ifndef _WIN32
    if (!g_options.foreground) {
        /* Daemonize */
        if (fuse_daemonize(0) != 0) {
            fprintf(stderr, "Error: Failed to daemonize\n");
            goto cleanup;
        }
    }
#endif

    /* Run FUSE event loop.
     * Use single-threaded loop only for -d (debug output is unreadable in MT).
     * Foreground mode (-f) and daemon mode both use multi-threaded. */
    if (g_options.debug) {
        ret = fuse_loop(g_fuse);
    } else {
        struct fuse_loop_config loop_cfg;
        memset(&loop_cfg, 0, sizeof(loop_cfg));
        loop_cfg.clone_fd = 0;
        loop_cfg.max_idle_threads = 10;
        ret = fuse_loop_mt(g_fuse, &loop_cfg);
    }

    if (ret != 0) {
        fprintf(stderr, "Error in FUSE event loop: %d\n", ret);
    }

    printf("\nUnmounting...\n");

cleanup:
    if (g_fuse) {
        fuse_unmount(g_fuse);
        fuse_destroy(g_fuse);
        g_fuse = NULL;
    }

    fuse_opt_free_args(&args);

    if (g_ctx) {
        sacd_overlay_destroy(g_ctx);
        g_ctx = NULL;
    }

    printf("Done.\n");
    return ret;
}
