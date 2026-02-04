#include "icsmm.h"
#include "debug.h"
#include "helpers4.h"
#include <stdio.h>
#include <stdlib.h>

/*
 * The allocator MUST store the head of its free list in this variable. 
 * Doing so will make it accessible via the extern keyword.
 * This will allow ics_freelist_print to access the value from a different file.
 */

ics_free_header *freelist_head = NULL;

void *ics_malloc(size_t size) { 
    size_t asize;
    void *bp; //points to free block

    if(size == 0){
        //if no req_size, error
        errno = EINVAL;
        return NULL;
    }

    if(size <= DSIZE){ //if less than or equal to min payload size
        asize = 2*DSIZE;
    }else{ //if greater than min block size(multiply min block size)
        // add in min_block_size to req_size and round up to nearest multiple of block size
        asize = DSIZE * ((size + (DSIZE) + (DSIZE-1)) / DSIZE);
    }

    if(asize > 1<<16){//if block size is greater than 2^16, error
        errno = EINVAL;
        return NULL;
    }

    while(true){
        //search free list for a fit
        if((bp = find_fit(asize)) != NULL){
            place(bp, asize, size);
            return (void *)((uintptr_t)bp+sizeof(ics_header));
        }

        //no fit found get more mem and place block
        if(allocated_pages != 5){ // if not hit max pages allocated
            if((bp = ics_inc_brk()) == (void*)-1){ //get more space on heap
                errno = ENOMEM;
                return NULL;
            }
            
            if(!allocated_pages){ //if first time extending heap
                //prologue
                ics_footer* pro = (ics_footer*)bp;
                pro->block_size = 0;
                pro->fid = FOOTER_MAGIC;
                pro->requested_size = 0;

                //epilogue
                ics_header* epi = (ics_header*)(((uintptr_t)bp)+CHUNKSIZE-sizeof(ics_header));
                epi->block_size = 0;
                epi->hid = HEADER_MAGIC;
                epi->requested_size = 0;

                //free_header
                ics_free_header* free_hdr = (ics_free_header*)(((uintptr_t)bp)+sizeof(ics_footer));
                free_hdr->header.block_size = CHUNKSIZE-sizeof(ics_footer)-sizeof(ics_header); //4096 - prologue - epilogue
                free_hdr->header.hid = HEADER_MAGIC;
                free_hdr->header.requested_size = CHUNKSIZE;
                free_hdr->next = NULL;
                free_hdr->prev = NULL;

                //footer
                ics_footer* ft = (ics_footer*)(((uintptr_t)bp)+CHUNKSIZE-sizeof(ics_header)-sizeof(ics_footer));
                ft->block_size = CHUNKSIZE-sizeof(ics_footer)-sizeof(ics_header);
                ft->fid = FOOTER_MAGIC;
                ft->requested_size = CHUNKSIZE;

                freelist_head = free_hdr;
            }else{
                //if extending heap after first time
                //check if the last page's last space is free
                //check if block behind is free
                ics_footer* last_page_ft = (ics_footer*)(((uintptr_t)bp)-sizeof(ics_header)-sizeof(ics_footer));
                ics_free_header* free_hdr;
                if(last_page_ft->block_size % 2 == 0){
                    //if free, the coalesce
                    //set header size = header size + new amount we just extended
                    free_hdr = (ics_free_header*)(((uintptr_t)bp)-sizeof(ics_header)-last_page_ft->block_size);
                    free_hdr->header.block_size += CHUNKSIZE;
                }else{
                    //if last page end is allocated, make new header
                    free_hdr = (ics_free_header*)(((uintptr_t)bp)-sizeof(ics_header));
                    free_hdr->header.block_size = CHUNKSIZE;
                    free_hdr->header.hid = HEADER_MAGIC;
                    free_hdr->header.requested_size = size;
                    free_hdr->next = NULL;
                    free_hdr->prev = NULL;
                    //insert new free block into list
                    insert_in_order(free_hdr);
                }
                //make new epilogue
                ics_header* epi = (ics_header*)(((uintptr_t)bp)+CHUNKSIZE-sizeof(ics_header));
                epi->block_size = 0;
                epi->hid = HEADER_MAGIC;
                epi->requested_size = 0;
                //make new footer
                ics_footer* ft = (ics_footer*)(((uintptr_t)bp)+CHUNKSIZE-sizeof(ics_header)-sizeof(ics_footer));
                ft->block_size = free_hdr->header.block_size;
                ft->fid = FOOTER_MAGIC;
                ft->requested_size = size;
            }
            allocated_pages+=1;
        }else{
            errno = ENOMEM;
            return NULL;
        }
    }
    return NULL;
}


int ics_free(void *ptr) {
    //check if ptr is actually allocated
    if(ptr < ics_get_brk()-(allocated_pages*CHUNKSIZE)+sizeof(ics_footer) || ptr > (ics_get_brk() - sizeof(ics_header))){
        //if before epilogue or after prologue error
        errno = EINVAL;
        return -1;
    }

    ics_header* hdr = (ics_header*)(((uintptr_t)ptr)-sizeof(ics_header));
    ics_footer* ft = (ics_footer*)(((uintptr_t)hdr)+hdr->block_size-1-sizeof(ics_footer));
    //check ptr's size isn't greater than where brkptr is
    //check if footer and header match and their id's
    if((void*)(((uintptr_t)ptr)+hdr->block_size-1) > ics_get_brk() || hdr->hid != HEADER_MAGIC || ft->fid != FOOTER_MAGIC || hdr->block_size != ft->block_size 
    || hdr->block_size % 2 == 0 || ft->block_size % 2 == 0 || hdr->requested_size != ft->requested_size){
        errno = EINVAL;
        return -1;
    }
    //reset allocated block in header and pointer
    hdr->block_size -= 1;
    ft->block_size -=1;
    //coalesce
    void* new_free_blk;
    new_free_blk = coalesce(hdr);
    //put this block into free list
    insert_in_order(new_free_blk);
    return 0;
}
