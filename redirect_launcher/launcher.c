#include "redirect_launcher/launcher.h"

#define _POSIX_C_SOURCE 200809L

#include <stdio.h>
#include <string.h>
#include "errno.h"
#include "fcntl.h"
#include "pthread.h"
#include "sys/types.h"
#include "sys/wait.h"
#include "unistd.h"

static const int kStdoutIndex = 0;
static const int kStderrIndex = 1;
static const int kMaxPipeCount = 1;

static const int kReadPipeIndex = 0;
static const int kWritePipeIndex = 1;

static int stdout_read_fd(int pipes[kMaxPipeCount][2]);
static int stdout_write_fd(int pipes[kMaxPipeCount][2]);
static int stderr_read_fd(int pipes[kMaxPipeCount][2]);
static int stderr_write_fd(int pipes[kMaxPipeCount][2]);

struct ThreadContext {
  const char* output_file;
  int readable_pipe;
};

static void* CopyPipeToFile(void* context);

int launch(const char* stdout_file, const char* stderr_file,
           const char* main_file, char* const argv[]) {
  int ec;
  int exit_code = 0;
  int pipes[2][2];

  ec = pipe(pipes[kStdoutIndex]);
  if (ec != 0) {
    fprintf(stderr, "Failed to create pipe: %s", strerror(errno));
    return 2;
  }
  ec = pipe(pipes[kStderrIndex]);
  if (ec != 0) {
    fprintf(stderr, "Failed to create pipe: %s", strerror(errno));
    return 2;
  }

  struct ThreadContext stdout_context = {stdout_file, stdout_read_fd(pipes)};
  struct ThreadContext stderr_context = {stderr_file, stderr_read_fd(pipes)};
  pthread_t stdout_tid = 0;
  pthread_t stderr_tid = 0;
  ec = pthread_create(&stdout_tid, NULL /* attr */, &CopyPipeToFile,
                      &stdout_context);
  if (ec != 0) {
    // TODO(zhangshuai.ds): Use strerror_r()
    fprintf(stderr, "Failed to create STDOUT redirecting thread: %s",
            strerror(errno));
    exit_code = 3;
    goto CLEANUP;
  }
  ec = pthread_create(&stderr_tid, NULL /* attr */, &CopyPipeToFile,
                      &stderr_context);
  if (ec != 0) {
    fprintf(stderr, "Failed to create STDERR redirecting thread: %s",
            strerror(errno));
    exit_code = 3;
    goto CLEANUP;
  }

  pid_t pid = fork();
  if (pid == -1) {
    fprintf(stderr, "Failed to fork child process: %s", strerror(errno));
    exit_code = 4;
    goto CLEANUP;
  }

  if (pid == 0) {  // Child process
    dup2(stdout_write_fd(pipes),
         STDOUT_FILENO);  // Redirect STDOUT to our pipe.
    dup2(stderr_write_fd(pipes),
         STDERR_FILENO);  // Redirect STDERR to our pipe.

    // Close before exec for hiding.
    close(stdout_read_fd(pipes));
    close(stdout_write_fd(pipes));
    close(stderr_read_fd(pipes));
    close(stderr_write_fd(pipes));

    return execv(main_file, argv);
  }

  if (waitpid(pid, &exit_code, 0) < 0) {
    fprintf(stderr, "Failed to wait child process: %s", strerror(errno));
    // TODO(zhangshuai.ds): Kill child process
    exit_code = 5;
    goto CLEANUP;
  }

CLEANUP:
  // TODO(zhangshuai.ds): Signal children for exiting.
  if (stdout_tid != 0) {
    ec = pthread_join(stdout_tid, NULL);
    if (ec != 0) {
      fprintf(stderr, "Failed to join STDOUT redirecting thread: %s",
              strerror(errno));
    }
  }
  if (stderr_tid != 0) {
    ec = pthread_join(stderr_tid, NULL);
    if (ec != 0) {
      fprintf(stderr, "Failed to join STDERR redirecting thread: %s",
              strerror(errno));
    }
  }

  return exit_code;
}

int stdout_read_fd(int pipes[kMaxPipeCount][2]) {
  return pipes[kStdoutIndex][kReadPipeIndex];
}
int stdout_write_fd(int pipes[kMaxPipeCount][2]) {
  return pipes[kStdoutIndex][kWritePipeIndex];
}

int stderr_read_fd(int pipes[kMaxPipeCount][2]) {
  return pipes[kStderrIndex][kReadPipeIndex];
}
int stderr_write_fd(int pipes[kMaxPipeCount][2]) {
  return pipes[kStderrIndex][kWritePipeIndex];
}

void* CopyPipeToFile(void* context) {
  struct ThreadContext* the_context = (struct ThreadContext*)context;

  int output_fileno =
      open(the_context->output_file,
           O_WRONLY | O_APPEND | O_CREAT | O_TRUNC | O_CLOEXEC, 777);

  // TODO(zhangshuai.ds): Read from pipe, write to file.
  // TODO(zhangshuai.ds): Use select() for timeout.
  (void)&output_fileno;

  return NULL;
}
