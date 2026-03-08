#include <stdio.h>
#include <errno.h>
#include <unistd.h>
#include <sys/mman.h>
#include "head.h"


void setup_memory_access_rules(){

	void (*address)(void);
	long int ret;
	int i = 0;

	address = _instructions;
	ret = syscall(10,((unsigned long)address) & mask, SIZE, PROT_READ | PROT_WRITE);
	AUDIT
        printf("(%d) protection command at address %p returned %ld (errno is %d)\n",i++,address,ret,errno);

	address = _patches;
	ret = syscall(10,((unsigned long)address) & mask, SIZE, PROT_READ | PROT_EXEC |PROT_WRITE);
	AUDIT
        printf("(%d) protection command at address %p returned %ld (errno is %d)\n",i++,address,ret,errno);

	address = _codemap;
	ret = syscall(10,((unsigned long)address) & mask, SIZE, PROT_READ | PROT_EXEC | PROT_WRITE);
	AUDIT
        printf("(%d) protection command at address %p returned %ld (errno is %d)\n",i++,address,ret,errno);

}
