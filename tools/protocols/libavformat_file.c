/*
 * Copyright (c) 2021 Taner Sener
 *
 * This file is part of FFmpegKit.
 *
 * FFmpegKit is free software: you can redistribute it and/or modify
 * it under the terms of the GNU Lesser General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * FFmpegKit is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public License
 * along with FFmpegKit.  If not, see <http://www.gnu.org/licenses/>.
 */

#include "libavutil/file.h"

static int64_t saf_seek(URLContext *h, int64_t pos, int whence)
{
    FileContext *c = h->priv_data;
    int64_t ret;

    if (whence == AVSEEK_SIZE) {
        struct stat st;
        ret = fstat(c->fd, &st);
        return ret < 0 ? AVERROR(errno) : (S_ISFIFO(st.st_mode) ? 0 : st.st_size);
    }

    ret = lseek(c->fd, pos, whence);

    return ret < 0 ? AVERROR(errno) : ret;
}

static int saf_open(URLContext *h, const char *filename, int flags)
{
    FileContext *c = h->priv_data;
    int saf_id;
    struct stat st;
    char *final;
    char *saveptr = NULL;
    char *saf_id_string = NULL;
    char filename_backup[128];

    av_strstart(filename, "saf:", &filename);
    av_strlcpy(filename_backup, filename, sizeof(filename_backup));
    /* Java emits "saf:<fd>/<displayName>", split on '/' to isolate the fd */
    saf_id_string = av_strtok(filename_backup, "/", &saveptr);

    if (!saf_id_string || *saf_id_string == '\0') {
        return AVERROR(EINVAL);
    }
    saf_id = strtol(saf_id_string, &final, 10);
    if (saf_id_string == final || *final != '\0' || saf_id < 0) {
        return AVERROR(EINVAL);
    }

    saf_open_function custom_saf_open = av_get_saf_open();
    if (custom_saf_open != NULL) {
        int rc = custom_saf_open(saf_id);
        if (rc < 0) {
            return AVERROR(EBADF);
        }
        c->fd = rc;
    } else {
        c->fd = saf_id;
    }

    h->is_streamed = !fstat(c->fd, &st) && S_ISFIFO(st.st_mode);

    /* Buffer writes more than the default 32k to improve throughput especially
     * with networked file systems */
    if (!h->is_streamed && flags & AVIO_FLAG_WRITE)
        h->min_packet_size = h->max_packet_size = 262144;

    if (c->seekable >= 0)
        h->is_streamed = !c->seekable;

    return 0;
}

static int saf_check(URLContext *h, int mask)
{
    /* SAF URIs are opaque file descriptors managed by Java; local filesystem
     * access/stat calls on the remainder are meaningless. */
    return AVERROR(ENOSYS);
}

static int saf_delete(URLContext *h)
{
    return AVERROR(ENOSYS);
}

static int saf_move(URLContext *h_src, URLContext *h_dst)
{
    return AVERROR(ENOSYS);
}

static int saf_close(URLContext *h)
{
    FileContext *c = h->priv_data;

    saf_close_function custom_saf_close = av_get_saf_close();
    if (custom_saf_close != NULL) {
        return custom_saf_close(c->fd);
    } else {
        return 0;
    }
}

static const AVClass saf_class = {
    .class_name = "saf",
    .item_name  = av_default_item_name,
    .option     = file_options,
    .version    = LIBAVUTIL_VERSION_INT,
};

const URLProtocol ff_saf_protocol = {
    .name                = "saf",
    .url_open            = saf_open,
    .url_read            = file_read,
    .url_write           = file_write,
    .url_seek            = saf_seek,
    .url_close           = saf_close,
    .url_get_file_handle = file_get_handle,
    .url_check           = saf_check,
    .url_delete          = saf_delete,
    .url_move            = saf_move,
    .priv_data_size      = sizeof(FileContext),
    .priv_data_class     = &saf_class,
    .default_whitelist   = "saf,crypto,data"
};
