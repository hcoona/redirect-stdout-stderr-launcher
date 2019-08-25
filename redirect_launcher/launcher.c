#include "redirect_launcher/launcher.h"

#define _POSIX_C_SOURCE 200809L

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

int launch(const char* stdout_file, const char* stderr_file,
           const char* main_file, char* const argv[]) {
  int pipes[2][2];

  pipe(pipes[kStdoutIndex]);
  pipe(pipes[kStderrIndex]);

  int my_stdout_fileno = open(
      stdout_file, O_WRONLY | O_APPEND | O_CREAT | O_TRUNC | O_CLOEXEC, 777);
  int my_stderr_fileno = open(
      stdout_file, O_WRONLY | O_APPEND | O_CREAT | O_TRUNC | O_CLOEXEC, 777);

  pid_t pid = fork();
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

  // TODO(zhangshuai.ds): Read from pipe, write to file.

  int ec = 0;
  waitpid(pid, &ec, 0);
  return ec;
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
