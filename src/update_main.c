#include "update_module.h"
#include <getopt.h>
#include <stddef.h>
extern int update_debug;
extern int update_rc_mode;
extern void print_update_help();

int main(int argc, char** argv) {
    int opt;
    static struct option long_options[] = {
        {"debug", no_argument, 0, 'd'},
        {"rc", no_argument, 0, 'r'},
        {"prerelease", no_argument, 0, 'r'},
        {"help", no_argument, 0, 'h'},
        {0, 0, 0, 0}
    };
    while ((opt = getopt_long(argc, argv, "drh", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                update_debug = 1;
                break;
            case 'r':
                update_rc_mode = 1;
                break;
            case 'h':
                print_update_help();
                return 0;
        }
    }
    return run_update();
} 