#include "os_malloc.h"
#include <string.h>
#include <cstring> 

struct MallocMetadata{
    size_t size;
    bool is_free;
    MallocMetadata* next;
    MallocMetadata* prev;
};

//static variables 
static MallocMetadata* head = nullptr;
static MallocMetadata* tail = nullptr;

static size_t free_bytes = 0;
static size_t free_blocks = 0;
static size_t allocated_blocks = 0;
static size_t allocated_bytes = 0;

constexpr size_t META_SIZE = sizeof(MallocMetadata);

inline size_t getTotalSize(size_t req_size) {
    return req_size + sizeof(MallocMetadata);
}




/* smalloc function
Searches for a free block with at least ‘size’ bytes or allocates (sbrk()) one if none are 
found).  
Return value:   
    Success – returns pointer to the first byte in the allocated block (excluding the meta-data of 
    course)  
    Failure –   
        a. If size is 0 returns NULL.  
        b. If ‘size’ is more than 10^8, return NULL.  
        c. If sbrk fails in allocating the needed space, return NULL.   
*/
void* smalloc(size_t size){
    if(size == 0 || size > 100000000){
        return nullptr;
    }
    MallocMetadata* cur = head;
    while(cur != nullptr){
        if(cur->size >= size && cur->is_free){
            break;
        } else {
            cur = cur->next;
        }
    }
    //Case 1: we found a block that can store
    if(cur){
        cur->is_free = false;
        free_bytes -= cur->size;
        free_blocks--;
        return cur + 1; //returns actual value of the allocated memory + 1 since we are in terms of Metadata struct size
    }

    //Case 2: we need to use sbrk()
    void* new_block = sbrk(size + META_SIZE);
    if(new_block == (void*)-1){
        return nullptr;
    }
    allocated_blocks++;
    allocated_bytes += size;
    //create new struct in beginning of the allocated block
    bool first_entry = (head == nullptr) ? true : false;

    MallocMetadata* new_entry = (MallocMetadata*)new_block;
    new_entry->size = size;

    if(first_entry){
        new_entry->prev = nullptr;
        head = new_entry;
        tail = new_entry;
    } else {
        new_entry->prev = tail;
        tail->next = new_entry;
    }
    tail = new_entry;
    new_entry->next = nullptr;

    return new_entry + 1;
}

/* scalloc 
Searches for a free block of at least ‘num’ elements, each ‘size’ bytes that are all set to 0 
or allocates if none are found. In other words, find/allocate size * num bytes and set all 
bytes to 0.  
Return value:
    Success - returns pointer to the first byte in the allocated block.  
    Failure –   
        a. If size or num is 0 returns NULL.  
        b. If ‘size * num’ is more than 10^8, return NULL.  
        c. If sbrk fails in allocating the needed space, return NULL.  

*/

void* scalloc(size_t num, size_t size){
    if(num == 0 || size == 0 || num*size > 100000000 ){
        return nullptr;
    }
    void* new_mem = smalloc(num * size);
    if(!new_mem){
        return nullptr;
    }
    std::memset(new_mem, 0, num*size);
    return new_mem;
}

/*
Releases the usage of the block that starts with the pointer ‘p’.   
If ‘p’ is NULL or already released, simply returns.   
Presume that all pointers ‘p’ truly points to the beginning of an allocated block.  
*/

void sfree(void* p){
    if(!p){
        return;
    }
    MallocMetadata* entry = ((MallocMetadata*)p) - 1;
    if(entry->is_free){
        return;
    }
    free_bytes += entry->size;
    free_blocks++;
    entry->is_free = true;
}

/*
If ‘size’ is smaller than or equal to the current block’s size, reuses the same block. 
Otherwise, finds/allocates ‘size’ bytes for a new space, copies content of oldp into the 
new allocated space and frees the oldp.  
Return value:   
i. 
Success –   
a. Returns pointer to the first byte in the (newly) allocated space.   
b. If ‘oldp’ is NULL, allocates space for ‘size’ bytes and returns a pointer to it.  
ii. Failure –   
a. If size is 0 returns NULL.  
b. If ‘size’ if more than 108, return NULL.  
c. If sbrk fails in allocating the needed space, return NULL.   
d. Do not free ‘oldp’ if srealloc() fails.   
*/
void* srealloc(void* oldp, size_t size){
    if(size == 0 || size > 100000000){
        return nullptr;
    }
    if(!oldp){
        return smalloc(size);
    }

    //first check that we can reallocate, only then perform it, copy, and free the oldp
    //access struct
    MallocMetadata* entry = ((MallocMetadata*)oldp) - 1;
    size_t old_size = entry->size;

    //Case 1: we have enough space in the old block
    if(old_size >= size){
        return oldp;
    }
    //Case 2: we need to try to allocate a new block - use malloc, see what happens before freeing
    void* new_block = smalloc(size);
    if(!new_block){
        return nullptr;
    }
    //we succeeded in creating the block - now copy the memory. Note: we don't need to update the struct, smalloc does this for us
    std::memmove(new_block, oldp, old_size);


    //finally, free oldp
    sfree(oldp);
    return new_block;
    
}


/*
Returns the number of allocated blocks in the heap that are currently free.  
*/
size_t _num_free_blocks(){
    return free_blocks;
}

/*
Returns the number of bytes in all allocated blocks in the heap that are currently free, 
excluding the bytes used by the meta-data structs.   
*/ 
size_t _num_free_bytes(){
    return free_bytes;
}  


/*
Returns the overall (free and used) number of allocated blocks in the heap.   
*/
size_t _num_allocated_blocks(){
    return allocated_blocks;
}  

/*
Returns the overall number (free and used) of allocated bytes in the heap, excluding 
the bytes used by the meta-data structs
*/
size_t _num_allocated_bytes(){
    return allocated_bytes;
}  

/*
Returns the overall number of meta-data bytes currently in the heap. 
*/ 
size_t _num_meta_data_bytes(){
    return allocated_blocks*META_SIZE;
}
 
/*
Returns the number of bytes of a single meta-data structure in your system.
*/
size_t _size_meta_data(){
    return META_SIZE;
}