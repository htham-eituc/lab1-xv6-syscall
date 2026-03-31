#include "kernel/types.h"
#include "kernel/stat.h"
#include "user/user.h"

int main(int argc, char *argv[]) {
    // trace needs at least 3 args: program name, mask, and program to run
    if (argc < 3) {
        fprintf(2, "Usage: trace <mask> <command> [args...]\n");
        exit(1);
    }
    
    // Convert the mask argument from string to integer
    int mask = atoi(argv[1]);
    
    // Call the trace system call with the mask
    trace(mask);
    
    // Replace this process with the target program
    // argv[2] is the program name (e.g., "grep")
    // argv[3], argv[4], ... are the arguments to pass to the program
    // We need to skip argv[0] (program name) and argv[1] (mask)
    // So we pass &argv[2] as the argv to exec
    exec(argv[2], &argv[2]);
    fprintf(2, "exec failed\n");
    exit(1);
}