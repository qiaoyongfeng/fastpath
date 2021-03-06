/*-
 * Copyright (c) <2010>, Intel Corporation
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 * - Redistributions of source code must retain the above copyright
 *   notice, this list of conditions and the following disclaimer.
 *
 * - Redistributions in binary form must reproduce the above copyright
 *   notice, this list of conditions and the following disclaimer in
 *   the documentation and/or other materials provided with the
 *   distribution.
 *
 * - Neither the name of Intel Corporation nor the names of its
 *   contributors may be used to endorse or promote products derived
 *   from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
 * "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
 * LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS
 * FOR A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE
 * COPYRIGHT OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT,
 * INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES
 * (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION)
 * HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT,
 * STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED
 * OF THE POSSIBILITY OF SUCH DAMAGE.
 */

/**
 * Copyright (c) <2010-2014>, Wind River Systems, Inc. All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without modification, are
 * permitted provided that the following conditions are met:
 *
 * 1) Redistributions of source code must retain the above copyright notice,
 * this list of conditions and the following disclaimer.
 *
 * 2) Redistributions in binary form must reproduce the above copyright notice,
 * this list of conditions and the following disclaimer in the documentation and/or
 * other materials provided with the distribution.
 *
 * 3) Neither the name of Wind River Systems nor the names of its contributors may be
 * used to endorse or promote products derived from this software without specific
 * prior written permission.
 *
 * 4) The screens displayed by the application must contain the copyright notice as defined
 * above and can not be removed without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 *  AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDER OR CONTRIBUTORS BE
 * LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR CONSEQUENTIAL
 * DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR
 * SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER
 * CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY,
 * OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE
 * USE OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
/* Created 2010 by Keith Wiles @ windriver.com */

#include "include/fastpath.h"

/* Log sizes and data structure */
#define LOG_HISTORY         64          /* log "scrollback buffer" size */
#define LOG_MAX_LINE        1024        /* max. length of a log line */

/* Log message and metadata */
typedef struct log_msg_s {
    struct timeval  tv;                 /**< Timestamp */
    int             level;              /**< Log level */
    char            *file;              /**< Source file of the caller */
    long            line;               /**< Line number of the caller */
    char            *func;              /**< Function name of the caller */
    char            msg[LOG_MAX_LINE];  /**< Log message */
} log_msg_t;

/* Log history */
typedef struct log_s {
    log_msg_t   msg[LOG_HISTORY];    /**< Log message buffer */
    uint16_t    head;                /**< index of most recent log msg */
    uint16_t    tail;                /**< index of oldest log msg */
    uint8_t     need_refresh;        /**< log page doesn't contain the latest messages */
    rte_rwlock_t    lock;            /**< multi-threaded list lock */
} log_t;

log_t log_history;

FILE *log_file = NULL;
int log_level_screen = LOG_LEVEL_INFO;


/* Forward declarations of log entry formatting functions */
static const char * fastpath_format_msg_file(const log_msg_t *log_msg);
static const char * fastpath_format_msg_stdout(const log_msg_t *log_msg);


/* Initialize screen data structures */
void
fastpath_init_log(void)
{
    memset(&log_history, 0, sizeof(log_history));
    log_history.head = 0;
    log_history.tail = 0;
    log_history.need_refresh = 0;
}

/* Set minimum message level for printing to screen */
extern void fastpath_log_set_screen_level(int level)
{
    log_level_screen = level;
}

/* Log the provided message to the log screen and optionally a file. */
void
fastpath_log(int level, const char *file, long line,
        const char *func, const char *fmt, ...)
{
    log_msg_t *curr_msg;
    va_list args;

    rte_rwlock_write_lock(&log_history.lock);

    curr_msg = &log_history.msg[log_history.head];

    /* log message metadata */
    gettimeofday(&curr_msg->tv, NULL);

    curr_msg->level = level;

    if (curr_msg->file != NULL)
        free(curr_msg->file);
    curr_msg->file  = strdup(file);

    curr_msg->line = line;

    if (curr_msg->func != NULL)
        free(curr_msg->func);
    curr_msg->func = strdup(func);

    /* actual log message */
    va_start(args, fmt);
    vsnprintf(curr_msg->msg, LOG_MAX_LINE, fmt, args);
    va_end(args);

    /* Adjust head and tail indexes: head must point one beyond the last valid
     * entry, tail must move one entry if head has caught up.
     * The array acts as a circular buffer, so if either head or tail move
     * beyond the last array element, they are wrapped around.
     */
    log_history.head = (log_history.head + 1) % LOG_HISTORY;

    if (log_history.head == log_history.tail)
        log_history.tail = (log_history.tail + 1) % LOG_HISTORY;

    /* Log to file if enabled */
    if (log_file != NULL)
        fprintf(log_file, "%s", fastpath_format_msg_file(curr_msg));

    /* Print message to screen if its level is high enough. */
    if (level >= log_level_screen)
        fprintf(stdout, "%s", fastpath_format_msg_stdout(curr_msg));

    log_history.need_refresh = 1;

    rte_rwlock_write_unlock(&log_history.lock);
}


/* Open file on disk for logging. */
void
fastpath_log_set_file(const char *filename)
{
    FILE *fp;

    /* Clean up if already logging to a file */
    if (log_file != NULL) {
        fclose(log_file);
        log_file = NULL;
    }

    /* No filename given: disable logging to disk */
    if (filename == NULL)
        return;

    fp = fopen(filename, "w");

    if (fp == NULL)
        fastpath_log_warning("Unable to open log file '%s' for writing", filename);

    /* Unbuffered output if file is successfully opened */
    if (fp != NULL)
        setbuf(fp, NULL);

    log_file = fp;
}

/**************************************************************************//**
*
* fastpath_format_msg_file - formats the log entry for output to disk
*
* DESCRIPTION
* Generates a string representation of the log entry, suitable for writing to
* disk.
* The output is more verbose than the output of the format log functions for
* stdout and page.
* No effort is made to prettify multi-line messages: if indentation
* of multiple lines is required, the log msg itself must contain appropriate
* whitespace.
*
* RETURNS: Pointer to formatted string. The memory associated with the pointer
*          is managed by this function and must not be free'd by the calling
*          function.
*          The memory pointed to may be altered on subsequent calls to this
*          function. Copy the result if needed.
*
* SEE ALSO:
*/
static const char *
fastpath_format_msg_file(const log_msg_t *log_msg)
{
    /* Example log line:
     *   II 2014-03-14 13:37:05.123 [foo.c:42(bar_func)] This is a message
     */
    static char msg[LOG_MAX_LINE] = { 0 };
    char timestamp[32] = { 0 };
    char *file;

    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S",
            localtime(&log_msg->tv.tv_sec));

    file = strdup(log_msg->file);

    snprintf(msg, sizeof(msg), "%s %s.%03ld [%s:%ld(%s)] %s",
              (log_msg->level == LOG_LEVEL_TRACE)   ? "tt"
            : (log_msg->level == LOG_LEVEL_DEBUG)   ? "dd"
            : (log_msg->level == LOG_LEVEL_INFO)    ? "II"
            : (log_msg->level == LOG_LEVEL_WARNING) ? "WW"
            : (log_msg->level == LOG_LEVEL_ERROR)   ? "EE"
            : (log_msg->level == LOG_LEVEL_PANIC)   ? "PP"
            : "??",
            timestamp, log_msg->tv.tv_usec / 1000,
            basename(file), log_msg->line, log_msg->func, log_msg->msg);

    free(file);

    return msg;
}

/**************************************************************************//**
*
* fastpath_format_msg_stdout - formats the log entry for output to screen
*
* DESCRIPTION
* Generates a string representation of the log entry, suitable for writing to
* the screen.
* For info mesaages, just the message is printed. Warnings and more severe
* messages get an appropriate label.
* No effort is made to prettify multi-line messages: if indentation
* of multiple lines is required, the log msg itself must contain appropriate
* whitespace.
*
* RETURNS: Pointer to formatted string. The memory associated with the pointer
*          is managed by this function and must not be free'd by the calling
*          function.
*          The memory pointed to may be altered on subsequent calls to this
*          function. Copy the result if needed.
*
* SEE ALSO:
*/
static const char *
fastpath_format_msg_stdout(const log_msg_t *log_msg)
{
    /* Example log line:
     *   This is a message
     */
    static char msg[LOG_MAX_LINE] = { 0 };

    snprintf(msg, sizeof(msg), "%s%s",
              (log_msg->level <= LOG_LEVEL_INFO)    ? ""
            : (log_msg->level == LOG_LEVEL_WARNING) ? "WARNING: "
            : (log_msg->level == LOG_LEVEL_ERROR)   ? "!ERROR!: "
            : (log_msg->level == LOG_LEVEL_PANIC)   ? "!PANIC!: "
            : "??? ",
            log_msg->msg);

    return msg;
}

