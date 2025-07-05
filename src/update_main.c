#include "update_module.h"
#include <getopt.h>
#include <stddef.h>
extern int update_debug;

int main(int argc, char** argv) {
    int opt;
    static struct option long_options[] = {
        {"debug", no_argument, 0, 'd'},
        {0, 0, 0, 0}
    };
    while ((opt = getopt_long(argc, argv, "d", long_options, NULL)) != -1) {
        switch (opt) {
            case 'd':
                update_debug = 1;
                break;
        }
    }
    return run_update();
} 