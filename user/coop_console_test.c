// Test program for /dev/coop_console

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

static int console_fd = -1;
static int running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    running = 0;
    if (console_fd >= 0)
        close(console_fd);
    printf("\n[main] Caught SIGINT, shutting down...\n");
}

// Writer thread: periodically writes a test message
static void *writer_thread(void *arg) {
    (void)arg;
    const char *msg = "Hello from userland!";
    while (running) {
        ssize_t written = write(console_fd, msg, strlen(msg));
        if (written < 0) {
            perror("[writer] write failed");
            break;
        }
        printf("[writer] sent: %s\n", msg);
        sleep(2);
    }
    return NULL;
}

// Reader thread: blocks on read() and prints anything from kernel
static void *reader_thread(void *arg) {
    (void)arg;
    char buf[256];
    while (running) {
        ssize_t n = read(console_fd, buf, sizeof(buf) - 1);
        if (n < 0) {
            if (errno == EINTR)
                continue;
            perror("[reader] read failed");
            break;
        }
        if (n == 0) {
            printf("[reader] EOF on /dev/coop_console\n");
            break;
        }
        buf[n] = '\0';
        printf("[reader] received: %s\n", buf);
    }
    return NULL;
}

// Optional: forward stdin → /dev/coop_console
static void *stdin_thread(void *arg) {
    (void)arg;
    char line[256];
    while (running && fgets(line, sizeof(line), stdin)) {
        ssize_t written = write(console_fd, line, strlen(line));
        if (written < 0) {
            perror("[stdin] write failed");
            break;
        }
    }
    return NULL;
}

int main(void) {
    signal(SIGINT, handle_sigint);

    console_fd = open("/dev/coop_console", O_RDWR);
    if (console_fd < 0) {
        perror("[main] Failed to open /dev/coop_console");
        return 1;
    }
    printf("[main] Opened /dev/coop_console successfully.\n");

    pthread_t writer, reader, input;
    pthread_create(&writer, NULL, writer_thread, NULL);
    pthread_create(&reader, NULL, reader_thread, NULL);
    pthread_create(&input, NULL, stdin_thread, NULL);

    pthread_join(writer, NULL);
    pthread_join(reader, NULL);
    pthread_join(input, NULL);

    if (console_fd >= 0)
        close(console_fd);
    printf("[main] Exiting cleanly.\n");
    return 0;
}

