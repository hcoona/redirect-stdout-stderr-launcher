#include "redirect_launcher/launcher.h"

#define _GNU_SOURCE
#define _POSIX_C_SOURCE 200809L

#include <errno.h>
#include <fcntl.h>
#include <pthread.h>
#include <signal.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

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
  int readable_pipe_fd;
  pthread_mutex_t* exit_mutex;
  pthread_cond_t* exit_cond;  // Guarded by exit_mutex;
};

static void* CopyPipeToFile(void* context);

int launch(const char* stdout_file, const char* stderr_file,
           const char* main_file, char* const argv[]) {
  int ec;
  int exit_code = 0;

  int pipes[2][2];
  ec = pipe(pipes[kStdoutIndex]);
  if (ec != 0) {
    fprintf(stderr, "Failed to create pipe: %s\n", strerror(errno));
    return 2;
  }
  ec = pipe(pipes[kStderrIndex]);
  if (ec != 0) {
    fprintf(stderr, "Failed to create pipe: %s\n", strerror(errno));
    return 2;
  }

  pthread_mutex_t exit_mutex;
  ec = pthread_mutex_init(&exit_mutex, NULL /* attr */);
  if (ec != 0) {
    fprintf(stderr, "Failed to create exit mutex: %s\n", strerror(errno));
    return 3;
  }

  pthread_cond_t exit_cond;
  ec = pthread_cond_init(&exit_cond, NULL /* attr */);
  if (ec != 0) {
    fprintf(stderr, "Failed to create exit condition: %s\n", strerror(errno));
    return 3;
  }

  struct ThreadContext stdout_context = {
      .output_file = stdout_file,
      .readable_pipe_fd = stdout_read_fd(pipes),
      .exit_mutex = &exit_mutex,
      .exit_cond = &exit_cond,
  };
  struct ThreadContext stderr_context = {
      .output_file = stderr_file,
      .readable_pipe_fd = stderr_read_fd(pipes),
      .exit_mutex = &exit_mutex,
      .exit_cond = &exit_cond,
  };
  pthread_t stdout_tid = 0;
  pthread_t stderr_tid = 0;
  ec = pthread_create(&stdout_tid, NULL /* attr */, &CopyPipeToFile,
                      &stdout_context);
  if (ec != 0) {
    // TODO(zhangshuai.ds): Use strerror_r()
    fprintf(stderr, "Failed to create STDOUT redirecting thread: %s\n",
            strerror(errno));
    exit_code = 3;
    goto CLEANUP;
  }
  ec = pthread_create(&stderr_tid, NULL /* attr */, &CopyPipeToFile,
                      &stderr_context);
  if (ec != 0) {
    fprintf(stderr, "Failed to create STDERR redirecting thread: %s\n",
            strerror(errno));
    exit_code = 3;
    goto CLEANUP;
  }

  pid_t pid = fork();
  if (pid == -1) {
    fprintf(stderr, "Failed to fork child process: %s\n", strerror(errno));
    exit_code = 4;
    goto CLEANUP;
  }

  if (pid == 0) {  // Child process
    ec = dup2(stdout_write_fd(pipes),
              STDOUT_FILENO);  // Redirect STDOUT to our pipe.
    if (ec == -1) {
      fprintf(stderr, "Failed to redirect STDOUT to pipe: %s\n",
              strerror(errno));
      return 20;
    }
    ec = dup2(stderr_write_fd(pipes),
              STDERR_FILENO);  // Redirect STDERR to our pipe.
    if (ec == -1) {
      fprintf(stderr, "Failed to redirect STDERR to pipe: %s\n",
              strerror(errno));
      return 20;
    }

    // Close before exec for hiding.
    close(stdout_read_fd(pipes));
    close(stdout_write_fd(pipes));
    close(stderr_read_fd(pipes));
    close(stderr_write_fd(pipes));

    return execvp(main_file, argv);
  }

  int status;
  if (waitpid(pid, &status, 0) < 0) {
    fprintf(stderr, "Failed to wait child process: %s\n", strerror(errno));
    ec = kill(-getpgid(pid), SIGKILL);
    if (ec != 0) {
      fprintf(stderr, "Failed to kill child process group: %s\n",
              strerror(errno));
    }

    exit_code = 5;
    goto CLEANUP;
  }

  if (WIFEXITED(status)) {
    exit_code = WEXITSTATUS(status);
    fprintf(stdout, "Child process finished with exit code: %d\n", exit_code);
  } else {
    fprintf(stderr, "Child process finished abnormally\n");
    exit_code = 255;
  }

CLEANUP:
  ec = pthread_cond_broadcast(&exit_cond);
  if (ec != 0) {
    fprintf(stderr, "Failed to unblock redirecting threads: %s\n",
            strerror(errno));
    // Skip joining children threads.
    return exit_code;
  }

  if (stdout_tid != 0) {
    ec = pthread_join(stdout_tid, NULL);
    if (ec != 0) {
      fprintf(stderr, "Failed to join STDOUT redirecting thread: %s\n",
              strerror(errno));
    }
  }
  if (stderr_tid != 0) {
    ec = pthread_join(stderr_tid, NULL);
    if (ec != 0) {
      fprintf(stderr, "Failed to join STDERR redirecting thread: %s\n",
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
  int ec = 0;

  // TODO(zhangshuai.ds): Rolling the file.
  int output_fileno =
      open(the_context->output_file,
           O_WRONLY | O_CREAT | O_TRUNC | O_CLOEXEC | O_NONBLOCK, 777);
  if (output_fileno == -1) {
    fprintf(stderr, "Failed to open file %s: %s\n", the_context->output_file,
            strerror(errno));
    exit(10);
  }
  loff_t output_file_offset = 0;

  // Need one more round for remaining contents after received notification.
  bool should_exit = false;

  while (true) {
    while (true) {
      ssize_t count = splice(
          the_context->readable_pipe_fd, NULL /* offset_in */, output_fileno,
          &output_file_offset, 1024, SPLICE_F_MOVE | SPLICE_F_NONBLOCK);
      if (count == -1) {
        if (errno == EAGAIN) {
          break;
        }
        fprintf(stderr, "Failed to splice contents from pipe %d to %s: %s\n",
                the_context->readable_pipe_fd, the_context->output_file,
                strerror(errno));
        break;
      }
      if (count == 0) {
        break;
      }
      fprintf(stdout, "%zd bytes transferred from pipe %d to %s\n", count,
              the_context->readable_pipe_fd, the_context->output_file);
    }

    if (should_exit) {
      break;
    }

    ec = pthread_mutex_lock(the_context->exit_mutex);
    if (ec != 0) {
      fprintf(stderr, "Failed to lock exit mutex: %s\n", strerror(errno));
      exit(11);
    }

    struct timeval now;
    ec = gettimeofday(&now, NULL /* timezone */);
    if (ec != 0) {
      fprintf(stderr, "Failed to get current time: %s\n", strerror(errno));
      exit(11);
    }

    // TODO(zhangshuai.ds): Load from settings
    struct timespec timeout = {.tv_sec = now.tv_sec + 1,
                               .tv_nsec = now.tv_usec * 1000};
    ec = pthread_cond_timedwait(the_context->exit_cond, the_context->exit_mutex,
                                &timeout);
    if (ec != 0 && ec != ETIMEDOUT) {
      fprintf(stderr, "Failed to wait exit condition: %s\n", strerror(errno));
      exit(11);
    }

    int ec_unlock = pthread_mutex_unlock(the_context->exit_mutex);
    if (ec_unlock != 0) {
      fprintf(stderr, "Failed to unlock exit mutex: %s\n", strerror(errno));
      exit(11);
    }
    if (ec == 0) {  // Should exit current thread
      should_exit = true;
    }
  }

  ec = close(output_fileno);
  if (ec != 0) {
    fprintf(stderr, "Failed to close %s: %s\n", the_context->output_file,
            strerror(errno));
  }

  return NULL;
}
