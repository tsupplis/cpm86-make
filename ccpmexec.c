#include <stdio.h>
#include <util.h>

int main(int argc, char ** argv) {
    int c;
    char * command="show";
    fprintf(stderr,"INF: pid=%x\n", getpid());
    fprintf(stderr,"INF: ppid=%x\n", getppid());
    fprintf(stderr,"INF: priority=%d\n", getpriority());
    fprintf(stderr,"INF: ppriority=%d\n", getppriority());
    fprintf(stderr,"INF: calling p_cli('%s')\n",command);
    /*condetach();*/
    c=p_cli(command);
    p_priority(197);
    conattach();
    p_priority(200);
    fprintf(stderr,"\nINF: p_cli()=%d\n",c);
    return 0;
}
