#include <stdio.h>

#include "launcher.h"

int main(int argc, char** argv) {
  if (argc < 4) {
    fprintf(stderr, "Usage: %s <stdout_file> <stderr_file> <main_file> [args]",
            argv[0]);
    return 1;
  }

  return launch(argv[1], argv[2], argv[3], &argv[3]);
}
