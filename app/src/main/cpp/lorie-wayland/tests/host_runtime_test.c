#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>

#define LOGE(...) fprintf(stderr, __VA_ARGS__)

int lorie_setup_wayland_runtime_dir(const char *jni_path) {
    const char *xdg_runtime = getenv("XDG_RUNTIME_DIR");
    const char *tmpdir = getenv("TMPDIR");
    char runtime_dir[1024] = {0};

    if (jni_path && jni_path[0]) {
        strncpy(runtime_dir, jni_path, sizeof(runtime_dir) - 1);
    } else if (xdg_runtime && xdg_runtime[0]) {
        strncpy(runtime_dir, xdg_runtime, sizeof(runtime_dir) - 1);
    } else if (tmpdir && tmpdir[0]) {
        strncpy(runtime_dir, tmpdir, sizeof(runtime_dir) - 1);
    } else if (access("/data/data/com.termux/files/usr/tmp", F_OK) == 0) {
        strcpy(runtime_dir, "/data/data/com.termux/files/usr/tmp");
    } else if (access("/tmp", F_OK) == 0) {
        strcpy(runtime_dir, "/tmp");
    }

    if (runtime_dir[0]) {
        if (mkdir(runtime_dir, 0700) != 0 && errno != EEXIST) {
            LOGE("Failed to create runtime dir %s: %s\n", runtime_dir, strerror(errno));
            return -1;
        }
        if (chmod(runtime_dir, 0700) != 0) {
            LOGE("Failed to chmod runtime dir %s: %s\n", runtime_dir, strerror(errno));
            return -1;
        }
        setenv("XDG_RUNTIME_DIR", runtime_dir, 1);
        if (!tmpdir || !tmpdir[0])
            setenv("TMPDIR", runtime_dir, 1);
    }

    const char *wayland_display = getenv("WAYLAND_DISPLAY");
    if (!wayland_display || !wayland_display[0]) {
        wayland_display = "wayland-0";
        setenv("WAYLAND_DISPLAY", wayland_display, 1);
    }
    return 0;
}

int main(void) {
    printf("Running host runtime test...\n");

    // Test jni_path prioritization
    char jni_dir[] = "/tmp/host_jni_XXXXXX";
    if (!mkdtemp(jni_dir)) {
        perror("mkdtemp");
        return 1;
    }
    setenv("XDG_RUNTIME_DIR", "/invalid/dir", 1);
    
    if (lorie_setup_wayland_runtime_dir(jni_dir) != 0) {
        printf("Test failed: setup returned non-zero\n");
        return 1;
    }
    
    if (strcmp(getenv("XDG_RUNTIME_DIR"), jni_dir) != 0) {
        printf("Test failed: XDG_RUNTIME_DIR does not match jni_dir. Got: %s\n", getenv("XDG_RUNTIME_DIR"));
        return 1;
    }
    
    rmdir(jni_dir);
    printf("Host runtime test passed successfully!\n");
    return 0;
}
