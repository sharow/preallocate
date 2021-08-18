/* -*- Mode: c; tab-width: 4; c-basic-offset: 4; indent-tabs-mode: nil; -*- */
// SPDX-License-Identifier: MIT

#define _GNU_SOURCE
#define _LARGEFILE64_SOURCE
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>
#include <stdint.h>
#include <stdbool.h>
#include <inttypes.h>
#include <stdnoreturn.h>
#include <assert.h>
#include <limits.h>
#include <time.h>
#include <errno.h>
#include <sys/types.h>
#include <sys/epoll.h>
#include <unistd.h>
#include <fcntl.h>
#include <getopt.h>

#define CHUNK_BUFFER_SIZE (1024 * 1024 * 2)
#define DEFAULT_LENGTH (1024 * 1024 * 512)
#define MAX_EVENTS 10


static noreturn void help(void)
{
    puts(
"  usage:\n"
"     preallocate [options] <file>\n"
"     $ dd if=/dev/very_large | gzip | preallocate -l 100gb <file>\n"
"\n"
"         Preallocate file, and write. also truncate when EOF.\n"
"\n"
"  options:\n"
"     -l, --length <num>   length for allocate space, in bytes (kb/mb/gb/tb/pb suffix allowed)\n"
"         --fsync          call fsync() after each write()\n"
"     -w, --overwrite      overwrite <file> if already exist\n");
    exit(EXIT_SUCCESS);
}

static off_t do_read_write(int fd, int opt_sync)
{
    int epfd = epoll_create1(0);
    if (epfd == -1) {
        perror("epoll_create1() failed");
        return -1;
    }

    {
        struct epoll_event ev = {
            .events = EPOLLIN | EPOLLERR | EPOLLHUP | EPOLLRDHUP,
            .data.fd = STDIN_FILENO
        };
        if (epoll_ctl(epfd, EPOLL_CTL_ADD, STDIN_FILENO, &ev) == -1) {
            perror("epoll_ctl() failed");
            goto onerror;
        }
    }

    off_t total_write = 0;
    bool eof = false;
    while (!eof) {
        struct epoll_event events[MAX_EVENTS];
        int nfds = epoll_wait(epfd, events, MAX_EVENTS, -1);
        if (nfds == -1) {
            perror("epoll_wait() failed");
            goto onerror;
        }
        for (int n = 0; n < nfds; n++) {
            static char buf[CHUNK_BUFFER_SIZE];
            struct epoll_event *e = &events[n];
            assert(e->data.fd == STDIN_FILENO);
            if (e->events & EPOLLIN) {
                ssize_t bytes_read = read(STDIN_FILENO, buf, CHUNK_BUFFER_SIZE);
                if (bytes_read == -1) {
                    perror("read() failed");
                    goto onerror;
                }
                assert(bytes_read > 0);
                ssize_t bytes_write = write(fd, buf, bytes_read);
                if (bytes_write == -1) {
                    perror("write() failed");
                    goto onerror;
                }
                assert(bytes_write > 0);
                total_write += bytes_write;
                if (opt_sync) {
                    if (fsync(fd) == -1) {
                        perror("fsync() failed");
                        goto onerror;
                    }
                }
            }
            if (e->events & EPOLLERR) {
                perror("EPOLLERR received");
                goto onerror;
            }
            if (e->events & (EPOLLHUP | EPOLLRDHUP)) {
                if (errno != 0) {
                    perror("EPOLLHUP/EPOLLRDHUP received");
                    goto onerror;
                }
                eof = true;
                break;
            }
        }
    }
    close(epfd);
    return total_write;

  onerror:
    close(epfd);
    return -1;
}

static bool preallocate_io(int fd, off_t len, int opt_sync)
{
    // allocate
    if (fallocate(fd, 0, 0L, len) != 0) {
        perror("fallocate() failed");
        return false;
    }

    // read/write
    off_t wrote = do_read_write(fd, opt_sync);
    if (wrote == (off_t)-1) {
        return false;
    }

    // truncate
    if (wrote < len) {
        if (ftruncate(fd, wrote) != 0) {
            perror("ftruncate() failed");
            return false;
        }
    }
    return true;
}

static int stricmp(const char *s, const char *t)
{
    assert(s && t);
    
    while (*s) {
        int d;
        if ((d = tolower(*(unsigned char *)s) - tolower(*(unsigned char *)t)) != 0) {
            return d;
        }
        s += 1;
        t += 1;
    }
    return tolower(*s) - tolower(*t);
}

int main(int argc, char *argv[])
{
    static const struct option longopts[] = {
        { "help",      no_argument,       NULL, 'h' },
        { "length",    required_argument, NULL, 'l' },
        { "sync",      no_argument,       NULL, 's' },
        { "overwrite", no_argument,       NULL, 'w' },
        // mode
        { NULL, 0, NULL, 0 }
    };

    int opt_sync = 0;
    int opt_overwrite = 0;
    off_t len = DEFAULT_LENGTH;
    int c;
    while ((c = getopt_long(argc, argv, "hl:sw", longopts, NULL)) != -1) {
        switch (c) {
        case 'l':
            {
                uintmax_t x;
                char *ep;
                x = strtoumax(optarg, &ep, 10);
                if (x == 0) {
                    fprintf(stderr, "invalid --length value\n");
                    return EXIT_FAILURE;
                }
                if (*ep != '\0') {
                    const uintmax_t s = 1024;
                    if      (strcmp(ep, "k") == 0)   x *= s;
                    else if (stricmp(ep, "kb") == 0) x *= s;
                    else if (stricmp(ep, "m") == 0)  x *= s * s;
                    else if (stricmp(ep, "mb") == 0) x *= s * s;
                    else if (stricmp(ep, "g") == 0)  x *= s * s * s;
                    else if (stricmp(ep, "gb") == 0) x *= s * s * s;
                    else if (stricmp(ep, "t") == 0)  x *= s * s * s * s;
                    else if (stricmp(ep, "tb") == 0) x *= s * s * s * s;
                    else if (stricmp(ep, "pb") == 0) x *= s * s * s * s * s;
                    else {
                        fprintf(stderr, "invalid --length suffix\n");
                        return EXIT_FAILURE;
                    }
                }
                if (x >= 1152921504606846976ULL) { // 1EiB
                    // 1EiB is the maximum file size of XFS
                    // note: 17592186044416ULL (16TiB) for ext4
                    fprintf(stderr, "--length TOO LARGE\n");
                    return EXIT_FAILURE;
                }
                len = (off_t)x;
            }
            break;
        case 's':
            opt_sync = 1;
            break;
        case 'w':
            opt_overwrite = 1;
            break;
        case 'h':
            help();
            break;
        default:
            fprintf(stderr, "invalid argument\n");
            return EXIT_FAILURE;
        }
    }

    if (optind == argc) {
        fprintf(stderr, "no file specified\n");
        return EXIT_FAILURE;
    }

    char *filename = argv[optind++];
    if (optind != argc) {
        fprintf(stderr, "too many arguments\n");
        return EXIT_FAILURE;
    }

    int fd = open(filename,
                  O_WRONLY | O_CREAT | (opt_overwrite ? 0 : O_EXCL),
                  0644);
    if (fd == -1) {
        perror("open() failed");
        return EXIT_FAILURE;
    }

    int flag = fcntl(STDIN_FILENO, F_GETFL);
    if (fcntl(STDIN_FILENO, F_SETFL, flag | O_NONBLOCK) == -1) {
        perror("fcntl() failed");
        close(fd);
        return EXIT_FAILURE;
    }

    if (!preallocate_io(fd, len, opt_sync)) {
        close(fd);
        return EXIT_FAILURE;
    }

    if (close(fd) == -1) {
        perror("close() failed");
        return EXIT_FAILURE;
    }
    return EXIT_SUCCESS;
}
