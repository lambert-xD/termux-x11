/*
 * Lorie Wayland Compositor — Clipboard Wayland→Android forwarding
 *
 * PR #5a: Pipe-based fd passing for clipboard data transfer.
 * When a Wayland client sets the selection, we create a pipe,
 * ask the source to write data, and forward the result to Android
 * via a text callback (JNI in production, test hook in tests).
 */

#include "compositor.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>

struct lorie_clipboard {
    struct lorie_compositor *compositor;
    struct wl_resource *current_source;
    struct wl_resource *current_offer;
    pthread_t worker_thread;
    int write_fd;
    int read_fd;
    void (*text_callback)(const char *text, size_t len, void *user_data);
    void *text_callback_user_data;
    pthread_mutex_t lock;
};

struct lorie_clipboard *lorie_clipboard_create(struct lorie_compositor *c) {
    struct lorie_clipboard *cb = calloc(1, sizeof(*cb));
    if (!cb) return NULL;

    cb->compositor = c;
    cb->read_fd = -1;
    cb->write_fd = -1;

    if (pthread_mutex_init(&cb->lock, NULL) != 0) {
        free(cb);
        return NULL;
    }

    return cb;
}

void lorie_clipboard_destroy(struct lorie_clipboard *cb) {
    if (!cb) return;

    if (cb->write_fd >= 0) {
        close(cb->write_fd);
        cb->write_fd = -1;
    }

    if (cb->read_fd >= 0) {
        close(cb->read_fd);
        cb->read_fd = -1;
    }

    if (cb->worker_thread) {
        pthread_join(cb->worker_thread, NULL);
        cb->worker_thread = 0;
    }

    pthread_mutex_destroy(&cb->lock);
    free(cb);
}

int lorie_clipboard_read_pipe(int read_fd, char **out_text, size_t *out_len) {
    char *buf = NULL;
    size_t capacity = 0;
    size_t len = 0;
    char chunk[4096];
    ssize_t n;

    while ((n = read(read_fd, chunk, sizeof(chunk))) > 0) {
        if (len + (size_t)n > capacity) {
            capacity = capacity ? capacity * 2 : 4096;
            while (capacity < len + (size_t)n) capacity *= 2;
            char *new_buf = realloc(buf, capacity);
            if (!new_buf) {
                free(buf);
                return -1;
            }
            buf = new_buf;
        }
        memcpy(buf + len, chunk, (size_t)n);
        len += (size_t)n;
    }

    if (n < 0 && errno != EAGAIN && errno != EWOULDBLOCK) {
        free(buf);
        return -1;
    }

    *out_text = buf;
    *out_len = len;
    return 0;
}

static void *clipboard_worker(void *data) {
    struct lorie_clipboard *cb = data;

    char *text = NULL;
    size_t len = 0;

    if (lorie_clipboard_read_pipe(cb->read_fd, &text, &len) == 0 && text && len > 0) {
        pthread_mutex_lock(&cb->lock);
        if (cb->text_callback) {
            cb->text_callback(text, len, cb->text_callback_user_data);
        }
        pthread_mutex_unlock(&cb->lock);
    }

    free(text);

    if (cb->read_fd >= 0) {
        close(cb->read_fd);
        cb->read_fd = -1;
    }

    return NULL;
}

void lorie_clipboard_set_selection(struct lorie_clipboard *cb, struct wl_resource *source_resource) {
    if (!cb) return;

    pthread_mutex_lock(&cb->lock);

    /* Cancel previous source */
    if (cb->current_source) {
        wl_data_source_send_cancelled(cb->current_source);
    }

    /* Clean up previous pipe and thread */
    if (cb->write_fd >= 0) {
        close(cb->write_fd);
        cb->write_fd = -1;
    }
    if (cb->read_fd >= 0) {
        close(cb->read_fd);
        cb->read_fd = -1;
    }
    if (cb->worker_thread) {
        pthread_join(cb->worker_thread, NULL);
        cb->worker_thread = 0;
    }

    cb->current_source = source_resource;
    cb->current_offer = NULL;

    if (!source_resource) {
        pthread_mutex_unlock(&cb->lock);
        return;
    }

    int fds[2];
    if (pipe(fds) != 0) {
        pthread_mutex_unlock(&cb->lock);
        return;
    }

    cb->read_fd = fds[0];
    cb->write_fd = fds[1];

    /* Ask source client to write data to the write end */
    wl_data_source_send_send(source_resource, "text/plain;charset=utf-8", cb->write_fd);
    close(cb->write_fd);
    cb->write_fd = -1;

    /* Start worker to read from the read end */
    if (pthread_create(&cb->worker_thread, NULL, clipboard_worker, cb) != 0) {
        close(cb->read_fd);
        cb->read_fd = -1;
        cb->current_source = NULL;
    }

    pthread_mutex_unlock(&cb->lock);
}

void lorie_clipboard_set_text_callback(struct lorie_clipboard *cb,
    void (*cb_fn)(const char *text, size_t len, void *user_data), void *user_data) {
    if (!cb) return;
    pthread_mutex_lock(&cb->lock);
    cb->text_callback = cb_fn;
    cb->text_callback_user_data = user_data;
    pthread_mutex_unlock(&cb->lock);
}

int lorie_clipboard_mime_type_supported(const char *mime_type) {
    if (!mime_type) return 0;
    return (strcmp(mime_type, "text/plain") == 0 ||
            strcmp(mime_type, "text/plain;charset=utf-8") == 0);
}
