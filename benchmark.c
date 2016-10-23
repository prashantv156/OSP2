#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/syscall.h>
#include <time.h>
#include <fcntl.h>
#include <asm/types.h>

int main()
{
    int i=0;
    int val;
    int a;
    __u64 size;
    char data[1024];
    srand((int)time(NULL)+(int)getpid());
    // Initializing the keys
    for(i = 0; i < 10; i++)
    {
        val = memset(data, 0, 1024);
        a = rand();
        sprintf(data,"%d",a);
        fprintf(stderr,"S\t%d\t%d\t%lu\t%s\n",val,i,strlen(data),data);
    }

    return 0;
}

