#include "os_malloc.h"


void* smalloc(size_t bytes){
    if(bytes <= 0 || bytes > 100000000 ){
        return NULL;
    }
    intptr_t increment = (intptr_t)bytes;
    void* ret_val = sbrk(increment);

    //try casting to int for
    if(ret_val == (void*) -1){
        return NULL;
    }

    return ret_val;

}


/*
void* smalloc(size_t size)  
● Tries to allocate ‘size’ bytes.  
● Return value:  
i. Success –a pointer to the first allocated byte within the allocated block.  
ii. Failure –   
a. If ‘size’ is 0 returns NULL.  
b. If ‘size’ is more than 108, return NULL.   
c. If sbrk fails, return NULL.  
*/