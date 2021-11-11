/*
 * Copyright (C) 2018-2021 Jolla Ltd.
 *
 * You may use this file under the terms of BSD license as follows:
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 *
 *   1. Redistributions of source code must retain the above copyright
 *      notice, this list of conditions and the following disclaimer.
 *   2. Redistributions in binary form must reproduce the above copyright
 *      notice, this list of conditions and the following disclaimer in the
 *      documentation and/or other materials provided with the distribution.
 *   3. Neither the names of the copyright holders nor the names of its
 *      contributors may be used to endorse or promote products derived
 *      from this software without specific prior written permission.
 *
 * THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS "AS IS"
 * AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE
 * IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE
 * ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT HOLDERS OR CONTRIBUTORS
 * BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL, SPECIAL, EXEMPLARY, OR
 * CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO, PROCUREMENT OF
 * SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS; OR BUSINESS
 * INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY, WHETHER IN
 * CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR OTHERWISE)
 * ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF ADVISED OF
 * THE POSSIBILITY OF SUCH DAMAGE.
 */

#define _GNU_SOURCE 1
#include "test_common.h"

#include "gbinder_driver.h"
#include "gbinder_fmq_p.h"

#include <stdio.h>
#include <sys/mman.h>
#include <unistd.h>

static TestOpt test_opt;

typedef struct test_fmq_data {
    const char* name;
    gsize item_size;
    gsize max_num_items;
    gint type;
    gint flags;
    gint fd;
    gsize buffer_size;
} TestFmqData;

/*==========================================================================*
 * null
 *==========================================================================*/

static const TestFmqData test_fmq_tests_null[] = {
    { "wrong_size", 0, 8,
        GBINDER_FMQ_TYPE_SYNC_READ_WRITE, 0, -1, 0 },
    { "wrong_count", sizeof(guint32), 0,
        GBINDER_FMQ_TYPE_SYNC_READ_WRITE, 0, -1, 0 },
    { "wrong_buffer_size", sizeof(guint32), 8,
        GBINDER_FMQ_TYPE_SYNC_READ_WRITE, 0, 1, 0 },
};

static
void
test_null(
    gconstpointer test_data)
{
    const TestFmqData* test = test_data;

    GBinderFmq* fmq = gbinder_fmq_new(
        test->item_size,
        test->max_num_items,
        test->type,
        test->flags,
        test->fd,
        test->buffer_size);

    g_assert(!fmq);

    g_assert(!gbinder_fmq_ref(fmq));

    g_assert(gbinder_fmq_available_to_read(fmq) == 0);
    g_assert(gbinder_fmq_available_to_write(fmq) == 0);
    g_assert(gbinder_fmq_available_to_read_contiguous(fmq) == 0);
    g_assert(gbinder_fmq_available_to_write_contiguous(fmq) == 0);

    g_assert(!gbinder_fmq_begin_read(fmq, 1));
    g_assert(!gbinder_fmq_begin_read((GBinderFmq*)0x1, 0));
    g_assert(!gbinder_fmq_begin_write(fmq, 1));
    g_assert(!gbinder_fmq_begin_write((GBinderFmq*)0x1, 0));

    g_assert(!gbinder_fmq_end_read(fmq, 1));
    g_assert(!gbinder_fmq_end_read((GBinderFmq*)0x1, 0));
    g_assert(!gbinder_fmq_end_write(fmq, 1));
    g_assert(!gbinder_fmq_end_write((GBinderFmq*)0x1, 0));

    g_assert(!gbinder_fmq_read(fmq, (void*)0x1, 1));
    g_assert(!gbinder_fmq_read((GBinderFmq*)0x1, NULL, 1));
    g_assert(!gbinder_fmq_read((GBinderFmq*)0x1, (void*)0x1, 0));

    g_assert(!gbinder_fmq_write(fmq, (const void*)0x1, 1));
    g_assert(!gbinder_fmq_write((GBinderFmq*)0x1, NULL, 1));
    g_assert(!gbinder_fmq_write((GBinderFmq*)0x1, (const void*)0x1, 0));

    g_assert(gbinder_fmq_wait_timeout(fmq, 0, (guint32*)0x1, 0) == -EINVAL);
    g_assert(gbinder_fmq_wait_timeout((GBinderFmq*)0x1, 0, NULL, 0) == -EINVAL);
    g_assert(gbinder_fmq_wait(fmq, 0, (guint32*)0x1) == -EINVAL);
    g_assert(gbinder_fmq_wait((GBinderFmq*)0x1, 0, NULL) == -EINVAL);
    g_assert(gbinder_fmq_wake(fmq, 0) == -EINVAL);
}

/*==========================================================================*
 * read/write guint8
 *==========================================================================*/

static const TestFmqData test_fmq_tests_read_write_guint8[] = {
    { "event_flag", sizeof(guint8), 8, GBINDER_FMQ_TYPE_SYNC_READ_WRITE,
        GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG, -1, 0 },
    { "no_event_flag", sizeof(guint8), 8, GBINDER_FMQ_TYPE_SYNC_READ_WRITE,
        0, -1, 0 },
    { "unsync", sizeof(guint8), 8, GBINDER_FMQ_TYPE_UNSYNC_WRITE,
        GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG, -1, 0 },
    { "no_reset", sizeof(guint8), 8, GBINDER_FMQ_TYPE_SYNC_READ_WRITE,
        GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG |
        GBINDER_FMQ_FLAG_NO_RESET_POINTERS, -1, 0 },
};

static
void
test_read_write_guint8(
    gconstpointer test_data)
{
    const TestFmqData* test = test_data;
    guint i;
    guint8 in_data[test->max_num_items];
    guint8 out_data[test->max_num_items];

    GBinderFmq* fmq = gbinder_fmq_new(
        test->item_size,
        test->max_num_items,
        test->type,
        test->flags,
        test->fd,
        test->buffer_size);

    /* Intiailize input data with random numbers */
    for (i = 0; i < test->max_num_items; ++i) {
        in_data[i] = g_random_int() % G_MAXUINT8;
    }
    memset(out_data, 0, test->max_num_items);

    /* Write data one value at a time */
    for (i = 0; i < test->max_num_items; ++i) {
        g_assert(gbinder_fmq_write(fmq, &in_data[i], 1));
        g_assert(gbinder_fmq_available_to_read(fmq) == i + 1);
    }

    /* Try to write one item to full buffer
     * only sync write fails if buffer is full */
    if (test->type == GBINDER_FMQ_TYPE_SYNC_READ_WRITE) {
        g_assert(!gbinder_fmq_write(fmq, &in_data[0], 1));
        g_assert(gbinder_fmq_available_to_read(fmq) == test->max_num_items);
    }

    /* Read data one value at a time */
    for (i = 0; i < test->max_num_items; ++i) {
        g_assert(gbinder_fmq_read(fmq, &out_data, 1));
        g_assert(out_data[0] == in_data[i]);
        g_assert(gbinder_fmq_available_to_read(fmq) ==
            test->max_num_items - i - 1);
    }

    memset(out_data, 0, test->max_num_items);

    /* Fill whole buffer with data */
    g_assert(gbinder_fmq_write(fmq, in_data, test->max_num_items));
    g_assert(gbinder_fmq_available_to_read(fmq) == test->max_num_items);
    /* Read whole buffer */
    g_assert(gbinder_fmq_read(fmq, &out_data, test->max_num_items));
    g_assert(!memcmp(in_data, out_data, test->max_num_items * test->item_size));

    memset(out_data, 0, test->max_num_items);

    /* Try to write too many items */
    g_assert(!gbinder_fmq_write(fmq, in_data, test->max_num_items + 1));

    gbinder_fmq_unref(fmq);
}

/*==========================================================================*
 * read/write gint64
 *==========================================================================*/

static const TestFmqData test_fmq_tests_read_write_gint64[] = {
    { "event_flag", sizeof(gint64), 8, GBINDER_FMQ_TYPE_SYNC_READ_WRITE,
        GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG, -1, 0 },
    { "no_event_flag", sizeof(gint64), 8, GBINDER_FMQ_TYPE_SYNC_READ_WRITE,
        0, -1, 0 },
    { "unsync", sizeof(gint64), 8, GBINDER_FMQ_TYPE_UNSYNC_WRITE,
        GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG, -1, 0 },
    { "no_reset", sizeof(gint64), 8, GBINDER_FMQ_TYPE_SYNC_READ_WRITE,
        GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG |
        GBINDER_FMQ_FLAG_NO_RESET_POINTERS, -1, 0 },
};

static
void
test_read_write_gint64(
    gconstpointer test_data)
{
    const TestFmqData* test = test_data;
    guint i;
    gint64 in_data[test->max_num_items];
    gint64 out_data[test->max_num_items];

    GBinderFmq* fmq = gbinder_fmq_new(
        test->item_size,
        test->max_num_items,
        test->type,
        test->flags,
        test->fd,
        test->buffer_size);

    /* Intiailize input data with random numbers */
    for (i = 0; i < test->max_num_items; ++i) {
        in_data[i] = g_random_int();
    }
    memset(out_data, 0, test->max_num_items);

    /* Write data one value at a time */
    for (i = 0; i < test->max_num_items; ++i) {
        g_assert(gbinder_fmq_write(fmq, &in_data[i], 1));
        g_assert(gbinder_fmq_available_to_read(fmq) == i + 1);
    }

    /* Try to write one item to full buffer
     * only sync write fails if buffer is full */
    if (test->type == GBINDER_FMQ_TYPE_SYNC_READ_WRITE) {
        g_assert(!gbinder_fmq_write(fmq, &in_data[0], 1));
        g_assert(gbinder_fmq_available_to_read(fmq) == test->max_num_items);
    }

    /* Read data one value at a time */
    for (i = 0; i < test->max_num_items; ++i) {
        g_assert(gbinder_fmq_read(fmq, &out_data, 1));
        g_assert(out_data[0] == in_data[i]);
        g_assert(gbinder_fmq_available_to_read(fmq) ==
            test->max_num_items - i - 1);
    }

    memset(out_data, 0, test->max_num_items);

    /* Fill whole buffer with data */
    g_assert(gbinder_fmq_write(fmq, in_data, test->max_num_items));
    g_assert(gbinder_fmq_available_to_read(fmq) == test->max_num_items);
    /* Read whole buffer */
    g_assert(gbinder_fmq_read(fmq, &out_data, test->max_num_items));
    g_assert(!memcmp(in_data, out_data, test->max_num_items * test->item_size));

    memset(out_data, 0, test->max_num_items);

    /* Try to write too many items */
    g_assert(!gbinder_fmq_write(fmq, in_data, test->max_num_items + 1));

    gbinder_fmq_unref(fmq);
}

/*==========================================================================*
 * read/write counters
 *==========================================================================*/

static
void
test_read_write_counters(
    void)
{
    gint max_num_items = 8;
    gint write_count = 6;
    gint64 in_data[max_num_items];
    gint64 out_data[max_num_items];
    guint i;
    GBinderFmq* fmq = gbinder_fmq_new(sizeof(gint64), max_num_items,
        GBINDER_FMQ_TYPE_SYNC_READ_WRITE, GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG,
        -1, 0);

    /* Intiailize input data with random numbers */
    for (i = 0; i < max_num_items; ++i) {
        in_data[i] = g_random_int();
    }
    memset(out_data, 0, max_num_items);

    /* Write data one value at a time */
    for (i = 0; i < write_count; ++i) {
        g_assert(gbinder_fmq_write(fmq, &in_data[i], 1));
        g_assert(gbinder_fmq_available_to_read(fmq) == i + 1);
        g_assert(gbinder_fmq_available_to_write(fmq) ==
            max_num_items - i - 1);
    }

    /* Read data one value at a time */
    for (i = 0; i < 2; ++i) {
        g_assert(gbinder_fmq_read(fmq, &out_data, 1));
        g_assert(out_data[0] == in_data[i]);
        g_assert(gbinder_fmq_available_to_read(fmq) ==
            write_count - i - 1);
        g_assert(gbinder_fmq_available_to_write(fmq) ==
            max_num_items - write_count + i + 1);
        g_assert(gbinder_fmq_available_to_write_contiguous(fmq) ==
            max_num_items - write_count);
    }

    g_assert(gbinder_fmq_read(fmq, &out_data, 2));
    g_assert(gbinder_fmq_available_to_read(fmq) == 2);
    g_assert(gbinder_fmq_available_to_write(fmq) == 6);
    g_assert(gbinder_fmq_available_to_write_contiguous(fmq) == 2);
    g_assert(gbinder_fmq_write(fmq, in_data, 4));
    g_assert(gbinder_fmq_available_to_read(fmq) == 6);
    g_assert(gbinder_fmq_available_to_read_contiguous(fmq) == 4);
    g_assert(gbinder_fmq_available_to_write(fmq) == 2);
    g_assert(gbinder_fmq_available_to_write_contiguous(fmq) == 2);

    gbinder_fmq_unref(fmq);
}

/*==========================================================================*
 * read/write external fd
 *==========================================================================*/

static
void
test_read_write_external_fd(
    void)
{
    gint max_num_items = 8;
    gint64 in_data[max_num_items];
    gint64 out_data[max_num_items];
    gsize item_size = sizeof(gint64);
    guint i;
    GBinderFmq* fmq;

    /* Allocate shared memory */

    int shmem_fd = memfd_create("MessageQueueData", MFD_CLOEXEC);

    g_assert(shmem_fd >= 0);
    g_assert(ftruncate(shmem_fd, max_num_items * item_size) >= 0);

    fmq = gbinder_fmq_new(item_size, max_num_items,
        GBINDER_FMQ_TYPE_SYNC_READ_WRITE, GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG,
        shmem_fd, max_num_items * item_size);

    /* Intiailize input data with random numbers */
    for (i = 0; i < max_num_items; ++i) {
        in_data[i] = g_random_int();
    }
    memset(out_data, 0, max_num_items);

    /* Write data one value at a time */
    for (i = 0; i < max_num_items; ++i) {
        g_assert(gbinder_fmq_write(fmq, &in_data[i], 1));
        g_assert(gbinder_fmq_available_to_read(fmq) == i + 1);
    }

    /* Read data one value at a time */
    for (i = 0; i < max_num_items; ++i) {
        g_assert(gbinder_fmq_read(fmq, &out_data, 1));
        g_assert(out_data[0] == in_data[i]);
        g_assert(gbinder_fmq_available_to_read(fmq) == max_num_items - i - 1);
    }

    gbinder_fmq_unref(fmq);
}

/*==========================================================================*
 * zero copy
 *==========================================================================*/

static
void
test_zero_copy(
    void)
{
    gint max_num_items = 8;
    gint write_count = 2;
    gint64 in_data[max_num_items];
    gint64 out_data[max_num_items];

    gsize item_size = sizeof(gint64);
    guint i;
    GBinderFmq* fmq = gbinder_fmq_new(item_size, max_num_items,
        GBINDER_FMQ_TYPE_SYNC_READ_WRITE, GBINDER_FMQ_FLAG_CONFIGURE_EVENT_FLAG,
        -1, 0);
    void *data;

    /* Intiailize input data with random numbers */
    for (i = 0; i < max_num_items; ++i) {
        in_data[i] = g_random_int();
    }
    memset(out_data, 0, max_num_items);

    /* external write */
    g_assert_nonnull((data = gbinder_fmq_begin_write(fmq, write_count)));
    memcpy(data, in_data, write_count * item_size);
    g_assert(gbinder_fmq_end_write(fmq, write_count));

    g_assert(gbinder_fmq_available_to_read(fmq) == write_count);
    g_assert(gbinder_fmq_available_to_write(fmq) ==
        (max_num_items - write_count));

    /* external read */
    g_assert_nonnull((data = gbinder_fmq_begin_read(fmq, write_count)));
    memcpy(out_data, data, write_count * item_size);
    g_assert(gbinder_fmq_end_read(fmq, write_count));

    g_assert(!memcmp(in_data, out_data, write_count * item_size));

    gbinder_fmq_unref(fmq);
}

/*==========================================================================*
 * Common
 *==========================================================================*/

#define TEST_PREFIX "/fmq/"
#define TEST_(t) TEST_PREFIX t

int main(int argc, char* argv[])
{
    guint i;

    g_test_init(&argc, &argv, NULL);
    for (i = 0; i < G_N_ELEMENTS(test_fmq_tests_null); i++) {
        const TestFmqData* test = test_fmq_tests_null + i;
        char* path = g_strconcat(TEST_("fmq/"), test->name, NULL);

        g_test_add_data_func(path, test, test_null);
        g_free(path);
    }
    for (i = 0; i < G_N_ELEMENTS(test_fmq_tests_read_write_guint8); i++) {
        const TestFmqData* test = test_fmq_tests_read_write_guint8 + i;
        char* path = g_strconcat(TEST_("fmq/guint8/"), test->name, NULL);

        g_test_add_data_func(path, test, test_read_write_guint8);
        g_free(path);
    }
    for (i = 0; i < G_N_ELEMENTS(test_fmq_tests_read_write_gint64); i++) {
        const TestFmqData* test = test_fmq_tests_read_write_gint64 + i;
        char* path = g_strconcat(TEST_("fmq/gint64/"), test->name, NULL);

        g_test_add_data_func(path, test, test_read_write_gint64);
        g_free(path);
    }
    g_test_add_func(TEST_("fmq/read_write_counters"), test_read_write_counters);
    g_test_add_func(TEST_("fmq/read_write_external_fd"),
        test_read_write_external_fd);
    g_test_add_func(TEST_("fmq/zero_copy"), test_zero_copy);
    test_init(&test_opt, argc, argv);
    return g_test_run();
}

/*
 * Local Variables:
 * mode: C
 * c-basic-offset: 4
 * indent-tabs-mode: nil
 * End:
 */
