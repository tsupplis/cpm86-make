#include <stdio.h>
#include <string.h>
#include <ctype.h>
#include <util.h>

int main(int argc, char **argv)
{
    char command[128];
    int i, rc;

    strcpy(command, argc > 1 ? "" : "show");
    for (i = 1; i < argc; ++i) {
        if (strlen(command) + (i > 1) + strlen(argv[i]) >= sizeof(command)) {
            fprintf(stderr, "Command line too long\n");
            return 1;
        }
        if (i > 1)
            strcat(command, " ");
        strcat(command, argv[i]);
    }
    rc = p_spawn(command);
    fprintf(stderr,"Spawn returned %d\n",rc);
    return rc != 0;
}
