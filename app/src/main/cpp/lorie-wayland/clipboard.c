/*
 * Lorie Wayland Compositor — Clipboard Wayland→Android forwarding
 *
 * PR #5a: Pipe-based fd passing for clipboard data transfer.
 * PR #5b: Android→Wayland forwarding + loop prevention.
 */

#include "compositor.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>
#include <time.h>
#include <stdint.h>

#define MAX_CLIPBOARD_SIZE (1024 * 1024)
#define LOOP_PREVENTION_MS 500

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
    char *android_text;
    size_t android_text_len;
    enum lorie_clipboard_source last_source;
    uint64_t last_timestamp_ms;
    uint64_t sequence;
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

static uint64_t current_time_ms(void) {
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000 + (uint64_t)ts.tv_nsec / 1000000;
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

    free(cb->android_text);
    cb->android_text = NULL;
    cb->android_text_len = 0;

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
        /* Reserve len + n + 1 bytes (the "+1" is for the NUL terminator
         * appended below) so growth never needs a second realloc just to
         * fit it — every consumer of *out_text may legitimately treat it
         * as a C string (this project's own tests assert it via
         * ASSERT_EQ_STR/strcmp; logging/debug paths may printf("%s") it). */
        if (len + (size_t)n + 1 > capacity) {
            capacity = capacity ? capacity * 2 : 4096;
            while (capacity < len + (size_t)n + 1) capacity *= 2;
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

    /* NUL-terminate: out_text is a char** (string-shaped contract by name,
     * type and by this file's own ASSERT_EQ_STR-based tests). Without this,
     * buf[len] is uninitialized heap memory — any C-string consumer
     * (strcmp/strlen/printf %s) reads past the valid region into garbage.
     * Reproduced via test_clipboard_wayland_to_android: ASSERT_EQ_STR
     * compared "wayland text" (valid) against "wayland text<garbage>"
     * (heap bytes beyond `len`) and failed non-deterministically depending
     * on heap layout. `len > 0` always has the +1 slot reserved above; the
     * `len == 0` / no-data case still needs a fresh 1-byte allocation so
     * *out_text is a valid empty C string rather than NULL-with-len-0
     * (which would be inconsistent: ret == 0 "success" but text == NULL). */
    if (len > 0) {
        buf[len] = '\0';
    } else if (!buf) {
        buf = calloc(1, 1);
        if (!buf)
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
        cb->last_source = CLIPBOARD_SOURCE_WAYLAND;
        cb->last_timestamp_ms = current_time_ms();
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

void lorie_clipboard_clear_source(struct lorie_clipboard *cb, struct wl_resource *source_resource) {
    if (!cb) return;
    pthread_mutex_lock(&cb->lock);
    if (cb->current_source == source_resource) {
        cb->current_source = NULL;
    }
    pthread_mutex_unlock(&cb->lock);
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

/* --- Android → Wayland --- */

void lorie_clipboard_send_android_text(struct lorie_clipboard *cb, const char *text, size_t len) {
    if (!cb) return;
    if (len > MAX_CLIPBOARD_SIZE) return;

    pthread_mutex_lock(&cb->lock);

    /* Loop prevention: ignore echo from Wayland within 500ms */
    if (cb->last_source == CLIPBOARD_SOURCE_WAYLAND) {
        uint64_t now = current_time_ms();
        if (now - cb->last_timestamp_ms < LOOP_PREVENTION_MS) {
            pthread_mutex_unlock(&cb->lock);
            return;
        }
    }

    free(cb->android_text);
    cb->android_text = NULL;
    cb->android_text_len = 0;

    cb->android_text = calloc(len + 1, 1);
    if (cb->android_text) {
        if (len > 0 && text) {
            memcpy(cb->android_text, text, len);
        }
        cb->android_text[len] = '\0';
        cb->android_text_len = len;
    }

    cb->last_source = CLIPBOARD_SOURCE_ANDROID;
    cb->last_timestamp_ms = current_time_ms();
    cb->sequence++;

    pthread_mutex_unlock(&cb->lock);

    /* Notify Wayland clients */
    if (cb->compositor) {
        lorie_clipboard_send_android_selection(cb->compositor);
    }
}

const char *lorie_clipboard_get_android_text(struct lorie_clipboard *cb, size_t *out_len) {
    if (!cb || !out_len) return NULL;
    pthread_mutex_lock(&cb->lock);
    *out_len = cb->android_text_len;
    const char *text = cb->android_text;
    pthread_mutex_unlock(&cb->lock);
    return text;
}

enum lorie_clipboard_source lorie_clipboard_get_last_source(struct lorie_clipboard *cb) {
    if (!cb) return CLIPBOARD_SOURCE_NONE;
    pthread_mutex_lock(&cb->lock);
    enum lorie_clipboard_source src = cb->last_source;
    pthread_mutex_unlock(&cb->lock);
    return src;
}

uint64_t lorie_clipboard_get_timestamp(struct lorie_clipboard *cb) {
    if (!cb) return 0;
    pthread_mutex_lock(&cb->lock);
    uint64_t ts = cb->last_timestamp_ms;
    pthread_mutex_unlock(&cb->lock);
    return ts;
}

void lorie_clipboard_set_last_source(struct lorie_clipboard *cb, enum lorie_clipboard_source src) {
    if (!cb) return;
    pthread_mutex_lock(&cb->lock);
    cb->last_source = src;
    pthread_mutex_unlock(&cb->lock);
}

void lorie_clipboard_set_timestamp(struct lorie_clipboard *cb, uint64_t ts) {
    if (!cb) return;
    pthread_mutex_lock(&cb->lock);
    cb->last_timestamp_ms = ts;
    pthread_mutex_unlock(&cb->lock);
}
