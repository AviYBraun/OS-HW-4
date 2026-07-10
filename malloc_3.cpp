//
// Created by student on 7/7/25.
//
#include <unistd.h>
#include <iostream>
#include <cstring>
#include <cstdint>
#include <sys/mman.h>

using namespace std;
#define MMAP_TREHSHOLD 128*1024


struct MallocMetadata {
    size_t size;
    bool is_free;
    bool is_mmap = false; // for the free part
    MallocMetadata* next = nullptr;
    MallocMetadata* prev = nullptr;
};

const size_t BYTE_SIZE = sizeof(MallocMetadata);
const int MAX_ORDER = 10;



size_t _size_meta_data(){
    return sizeof(MallocMetadata);
}

//TODO: add Orders array

//TODO: understand how to split blocks and how to keep track of allocated blocks

//TODO: understand how to make two blocks buddy blocks


struct BuddyAllocator{
    MallocMetadata* array[MAX_ORDER + 1];
    int orders[MAX_ORDER + 1];
    MallocMetadata *mmapHead = nullptr;
    MallocMetadata *mmapTail = nullptr;
    size_t num_free_blocks = 32;
    size_t num_free_bytes = 32 * (MMAP_TREHSHOLD - BYTE_SIZE);
    size_t num_allocated_blocks = 32;
    size_t num_allocated_bytes = 32 * (MMAP_TREHSHOLD - BYTE_SIZE);
    size_t num_meta_data_bytes = 32 * BYTE_SIZE; //TODO: if matters

    BuddyAllocator(){
        // int num = 128;
        for(int i = 0; i <= MAX_ORDER; i++) {
            orders[i] = 128 << i;
            array[i] = nullptr;
        }
        size_t region = 32*128*1024;
        intptr_t cur = (intptr_t)sbrk(0);
        size_t pad = (region - ((size_t)cur%region)) % region;
        if(pad){
            sbrk(pad);
        }
        char* base = (char*)sbrk(region);
        
        MallocMetadata* prev = nullptr;
        for(int i = 0; i < 32; i++){
            MallocMetadata* meta_ptr = (MallocMetadata*)(base + i*(128*1024));
            meta_ptr->size = 1024 * 128 - BYTE_SIZE;
            meta_ptr->is_free = true;
            meta_ptr->is_mmap = false;
            meta_ptr->prev = prev;
            meta_ptr->next = nullptr;
            if (prev){
                prev->next = meta_ptr;
            }
            prev = meta_ptr;
        }
        array[MAX_ORDER] = (MallocMetadata*)base;

    }

    int getOrder(size_t size){
        size_t tmp = size + BYTE_SIZE;
        if ((size_t)tmp > 128 * 1024) {

            return -1;
        }
        for (int i = 0; i <= MAX_ORDER; i++) {

            if (orders[i] >= tmp) { return i; }
        }
        return -1;
    }

    void insert(MallocMetadata* p, int order){
        if (order > MAX_ORDER || order < 0) {
            return;
        }
        MallocMetadata* head = array[order];
        MallocMetadata* current = head;
        p->is_free = true;
        if(!current) {
            array[order] = p;
            p->prev = nullptr;
            p->next = nullptr;
        }

        while (current){
            if(current == p){
                return;
            }
            if (p < current){
                if (current->prev){
                    current->prev->next = p;
                }
                if (current == head) {
                    array[order] = p;
                }
                p->prev = current->prev;
                current->prev = p;
                p->next = current;
                break;
            }
            if (current->next == nullptr){
                current->next = p;
                p->prev = current;
                p->next = nullptr;
                break;
            }
            current = current->next;
        }

        num_free_bytes += p->size;
        num_free_blocks++;
//        num_allocated_bytes -= p->size;

//        num_allocated_blocks--;
//        num_meta_data_bytes-=BYTE_SIZE;
    }
    void remove(MallocMetadata* p, int order){
        if (array[order] == p){
            array[order] = p->next;
        }
        if (p->prev){
            p->prev->next = p->next;
        }
        if (p->next){
            p->next->prev = p->prev;
        }
        p->next = nullptr;
        p->prev = nullptr;
        p->is_free = false;

        num_free_bytes -= p->size;
        num_free_blocks--;
//        num_allocated_bytes += p->size;

    }

    MallocMetadata* divideBlock(MallocMetadata* metaPtr, size_t size,int order, int index){
//        int diff = index - order;
        remove(metaPtr,index);
        MallocMetadata* left = metaPtr;
        for (int i = index - 1; i >= order; i--) {
            // left = splitBlock(left,order,i);
            MallocMetadata* right = (MallocMetadata*)((char*)left + orders[i]);
            right->size = orders[i] - BYTE_SIZE;
            right->is_mmap = false;
            right->is_free = false;
            insert(right,i);
            num_allocated_blocks++;
            num_allocated_bytes -= BYTE_SIZE;
            left->size = orders[order] - BYTE_SIZE;    
        }
        left->next = nullptr;
        left->prev = nullptr;
        return left;
    }

    void* searchBlock(size_t size){
        int order = getOrder(size);
        if(order == -1){
            return nullptr;
        }
        for(int i = order; i <= MAX_ORDER; i++){
            if(array[i]){
                MallocMetadata* ret = divideBlock(array[i],size,order, i);
                return (char*)ret + BYTE_SIZE;
            }
        }
        return nullptr;
    }

    void* mmapBlock(size_t size){
        size_t total_size = size + BYTE_SIZE;

        MallocMetadata* p = (MallocMetadata*)mmap(nullptr, total_size,
                                                  PROT_READ | PROT_WRITE,
                                                  MAP_PRIVATE | MAP_ANONYMOUS,
                                                  -1, 0);
        if(p == MAP_FAILED){
            return nullptr;
        }
        p->is_mmap = true;
        p->is_free = false;
        if(mmapTail) {
            mmapTail->next = p;
        }
        if(!mmapHead){
            mmapHead = p;
        }
        p->prev = mmapTail;
        mmapTail = p;
        p->next = nullptr;
        p->size = size;
//            num_free_bytes -= p->size;
//            num_free_blocks--;
        num_allocated_blocks++;
        num_allocated_bytes+=p->size;
//            num_meta_data_bytes+=BYTE_SIZE;

        return ((char*)p+BYTE_SIZE);
    }

    void ummapBlock(MallocMetadata* p){
        if (p->prev){
            p->prev->next = p->next;
        }
        if (p->next){
            p->next->prev = p->prev;
        }
        if(p == mmapHead){
            mmapHead = p->next;
        }
        if(p == mmapTail){
            mmapTail = p->prev;
        }
        p->next = nullptr;
        p->prev = nullptr;
//        num_free_bytes += p->size;
//        num_free_blocks++;
       num_allocated_blocks--;
       num_allocated_bytes-=p->size;
//        num_meta_data_bytes-=BYTE_SIZE;
        munmap(p,p->size+BYTE_SIZE);
    }

    MallocMetadata* uniteBlocks(MallocMetadata* metaPtr){
        int order = getOrder(metaPtr->size);
        insert(metaPtr,order);

        if(order == MAX_ORDER){
            return nullptr;
        }

        MallocMetadata* buddy = (MallocMetadata*)((uintptr_t)metaPtr ^ (metaPtr->size +BYTE_SIZE));

        if(!buddy->is_free || buddy->size != metaPtr->size){
            return nullptr;
        }
        remove(buddy,order);
        remove(metaPtr,order);
        if(buddy<metaPtr){
            metaPtr = buddy;
        }
        metaPtr->size = orders[order+1] - BYTE_SIZE;
        num_allocated_blocks--;
        num_allocated_bytes += BYTE_SIZE;
        return metaPtr;
       
    }
    bool canReachByMerging(MallocMetadata* meta_Ptr, size_t size){
        uintptr_t addr = (uintptr_t)meta_Ptr;
        size_t total = meta_Ptr->size + BYTE_SIZE;
        while (total < (size_t)(128*1024)){
            MallocMetadata* buddy = (MallocMetadata*)(addr^total);
            if(!buddy->is_free || buddy->size + BYTE_SIZE != total){
                return false;
            }
            if((uintptr_t)buddy < addr){
                addr = (uintptr_t)buddy;
            }
            total *=2;
            if(total - BYTE_SIZE >= size){
                return true;
            }
        }
        return false;
    }

};


BuddyAllocator ba;

size_t _num_free_blocks(){
    return ba.num_free_blocks;
}

size_t _num_free_bytes(){
    return ba.num_free_bytes;
}

size_t _num_allocated_blocks(){
    return ba.num_allocated_blocks;
}

size_t _num_allocated_bytes(){
    return ba.num_allocated_bytes;
}

size_t _num_meta_data_bytes(){
//    return (ba.num_allocated_blocks + ba.num_free_blocks) * _size_meta_data();
     return ba.num_allocated_blocks  * _size_meta_data();
}

void* smalloc(size_t size){
    if(size == 0 || size > 1e8){
        return nullptr;
    }
    void* ret;
    if(size + BYTE_SIZE <= MMAP_TREHSHOLD){
        ret = ba.searchBlock(size);
    } else{
        ret = ba.mmapBlock(size);
    }
    // if (ret) {
    //     MallocMetadata* metaPtr = (MallocMetadata*)((char*)ret-BYTE_SIZE);
    //     ba.num_allocated_blocks++;
    //     // ba.num_meta_data_bytes += BYTE_SIZE;
    //     ba.num_allocated_bytes += metaPtr->size;
    //     if(!metaPtr->is_mmap){
    //         ba.num_free_bytes -= metaPtr->size;
    //     }
    // }
    return ret;
}

void sfree(void* p){
    if(!p){
        return;
    }

    MallocMetadata* metaPtr = (MallocMetadata*)((char*)p-BYTE_SIZE);
    if(metaPtr->is_free){
        return;
    }

    if(metaPtr->is_mmap){
        ba.ummapBlock(metaPtr);
        return;
    }
    while (metaPtr){
        metaPtr = ba.uniteBlocks(metaPtr);
    };
}

void* scalloc(size_t num, size_t size){
    if(num == 0 || size == 0 || num > 100000000 / size){
        return nullptr;
    }
    void* ret = smalloc(num*size);
    if(!ret){
        return nullptr;
    }
//    char* tmp = (char*) ret;
//    for(size_t i = 0; i < num * size;i++){
//        tmp[i] = 0;
//    }
    memset(ret, 0, num * size);
    return ret;
}


void* srealloc(void* oldp, size_t size){

    if(!oldp){
        return smalloc(size);
    }

    if(size == 0 || size > 100000000){
        return nullptr;
    }

    MallocMetadata* tmp = (MallocMetadata*)((char*)oldp - BYTE_SIZE);
//    ba.array;

    if(tmp->is_mmap){
    if(tmp->size == size){
        return oldp;
    }
    size_t old_size = tmp->size;
    void* ret = smalloc(size);
    if(!ret){
        return nullptr;
    }
    memmove(ret, oldp, old_size < size ? old_size : size);
    sfree(oldp);
    return ret;
    }


    if(tmp->size >= size && size > 0){
        tmp->is_free = false;
//         tmp->size = size; //TODO: check if we need to change it
        return oldp;
    }
    size_t old_size = tmp->size;

    if(ba.canReachByMerging(tmp, size)){
    while(tmp && tmp->size < size){
        tmp = ba.uniteBlocks(tmp);
    }
    void* ret = (char*)tmp + BYTE_SIZE;
    memmove(ret, oldp, old_size);
    return ret;
    }

    void *ret = smalloc(size);
    if(!ret){
        return nullptr;
    }
    memmove(ret, oldp, old_size);
    sfree(oldp);
    return ret;
}




//int main(){
////    int* intptr = (int*) smalloc(4);
//
//    int* intptr1 = (int*)smalloc(88);  // Should use order 0 (128B)
//    ba.array;
//    int* intptr2 = (int*)smalloc(88);
//    int* intprr3 = (int*)smalloc(88);
//    sfree(intptr2);
//    sfree(intptr1);
//    sfree(intprr3);
//
//    return 0;
//
//}