#include "icsmm.h"
#include "helpers4.h"
#include "debug.h"

/* Helper function definitions go here */

volatile int allocated_pages = 0;

ics_free_header* find_fit(size_t asize){
    //first-fit
    ics_free_header *fbp; //free block pointer
    fbp = freelist_head;

    while(fbp){
        //if the asize is less than or equal to the block size of current block
        if(asize <= fbp->header.block_size){
            //remove bp from list
            remove_from_list(fbp);
            return fbp;
        }
        fbp = fbp->next;
    }
    return NULL;//no fit
}

void place(void* bp, size_t asize, size_t req_size){
    //get bp as free_header
    ics_free_header* cur_hdr = (ics_free_header*)bp;
    size_t csize = cur_hdr->header.block_size;
    //check if size after splitting has atleast 32 bytes to set(hdr, next, prev, ftr)
    if(csize - asize < 32){
        //add the splinter to the asize
        asize+=(csize - asize);
    }
    //create new header for malloced space
    ics_header* hdr = (ics_header*)bp;
    hdr->block_size = asize+1;
    hdr->hid = HEADER_MAGIC;
    hdr->requested_size = req_size;

    //put footer at end of new alloced block
    ics_footer* ft = (ics_footer*)(((uintptr_t)bp)+asize-sizeof(ics_footer));
    ft->block_size = asize+1;
    ft->fid = FOOTER_MAGIC;
    ft->requested_size = req_size;

    //split block if not perfect fit
    if(csize - asize != 0){
        //put free_header after alloced footer
        ics_free_header* free_hdr = (ics_free_header*)(((uintptr_t)bp)+asize);
        free_hdr->header.block_size = csize-asize; //size of og block size - asize;
        free_hdr->header.hid = HEADER_MAGIC;
        free_hdr->header.requested_size = req_size;
        free_hdr->next = NULL;
        free_hdr->prev = NULL;

        //put new footer at end of new free block
        ics_footer* end_ft = (ics_footer*)(((uintptr_t)bp)+csize-sizeof(ics_footer));
        end_ft->block_size = csize-asize;
        end_ft->fid = FOOTER_MAGIC;
        end_ft->requested_size = req_size;

        //sort into free list
        coalesce(free_hdr);
        insert_in_order(free_hdr);
    }
}

void remove_from_list(ics_free_header* fbp){
    //get next and prev
    ics_free_header* prev = NULL;
    ics_free_header* next = NULL;
    if(fbp->prev){
        prev = fbp->prev;
    }
    if(fbp->next){
        next = fbp->next;
    }

    if(prev && !next){ // if last node
        prev->next = NULL;
    }else if(!prev && next){ //if first node
        next->prev = NULL;
        freelist_head = next;
    }else if (prev && next){ //if middle node
        //set prev->next == next
        prev->next = next;
        //set next->prev == prev
        next->prev = prev;
    }else if(!prev && !next){ //only 1 node
        freelist_head = NULL;
    }
    
}

void insert_in_order(ics_free_header* fbp){
    if(!freelist_head){//list is empty
        freelist_head = fbp;
        return;
    }
    ics_free_header* cur_freeblk = freelist_head;
    ics_free_header* prev;
    size_t cur_blksize;
    size_t size = fbp->header.block_size;
    while(cur_freeblk){
        cur_blksize = cur_freeblk->header.block_size;
        if(size <= cur_blksize){
            if(cur_freeblk->prev){
                prev = cur_freeblk->prev;
                prev->next = fbp; // prev next is new block
                fbp->prev = prev; // new block prev is current block prev
                cur_freeblk->prev = fbp; // current block prev is new block
            }else{
                //insert in first node
                fbp->prev = NULL; // new block prev is current block prev
                cur_freeblk->prev = fbp;
                freelist_head = fbp;
            }
            fbp->next = cur_freeblk; //new block next is current block in loop
            return;
        }else if(!cur_freeblk->next){
            //insert in last place
            prev = cur_freeblk;
            if(prev){
                prev->next = fbp; // prev next is new block
                fbp->prev = prev; // new block prev is current block
                fbp->next = NULL;
            }
            return;
        }
        cur_freeblk = cur_freeblk->next;
    }
}

void* coalesce(void* bp){
    int behind = 0; // if block behind is free
    int ahead = 0; // if block ahead is free
    ics_free_header* cur_hdr = (ics_free_header*)bp;
    size_t cur_size = cur_hdr->header.block_size;
    ics_footer* cur_ftr = (ics_footer*)(((uintptr_t)bp)+cur_size-sizeof(ics_footer));

    //check if block behind is free
    ics_footer* last_page_ft = (ics_footer*)(((uintptr_t)bp)-sizeof(ics_footer));
    //if last_page_ft is not the prologue and it's free
    if(last_page_ft != ics_get_brk()-(CHUNKSIZE*allocated_pages) && last_page_ft->block_size % 2 == 0){
        behind = 1;
        //remove from list
        ics_free_header* last_ftr_free_hdr = (ics_free_header*)(((uintptr_t)bp)-last_page_ft->block_size);
        remove_from_list(last_ftr_free_hdr);
    }

    // check if block ahead is free
    ics_header* next_page_header = (ics_header*)(((uintptr_t)bp)+cur_hdr->header.block_size);
    //if next_page_header is not the epilogue and it's free
    if(next_page_header != ics_get_brk()-sizeof(ics_header) && next_page_header->block_size % 2 == 0){
        //take free block in front of you or behind you out of the linked list if free
        ahead = 1;
        //remove from list
        ics_free_header* next_hdr_free_hdr = (ics_free_header*)next_page_header;
        remove_from_list(next_hdr_free_hdr);
    }
    
    //get size of block in list and add to size of block we are freeing
    if(!behind && !ahead){
        //if neither are free
        return bp;
    }else if(!behind && ahead){
        //if behind is not free and ahead is free
        //get size of current header and next header together
        cur_size+=next_page_header->block_size;
        //update header of current block and footer of next block
        cur_hdr->header.block_size = cur_size;
        //get footer of next page
        ics_footer* nxt_pg_ftr = (ics_footer*)(((uintptr_t)bp)+cur_size-sizeof(ics_footer));
        nxt_pg_ftr->block_size = cur_size;
    }else if(behind && !ahead){
        //if behind is free and ahead is not free, get size of current block and prev block together
        cur_size+=last_page_ft->block_size;
        //get header of last block and update
        ics_header* lst_pg_hdr = (ics_header*)(((uintptr_t)bp)-last_page_ft->block_size);
        lst_pg_hdr->block_size = cur_size;
        //update footer of current block
        cur_ftr->block_size = cur_size;
        // set bp to last header address
        bp = lst_pg_hdr;
    }else if(behind && ahead){
        //if both are free

        //get header of last block and update
        ics_header* lst_pg_hdr = (ics_header*)(((uintptr_t)bp)-last_page_ft->block_size);
        //get footer of next page
        ics_footer* nxt_pg_ftr = (ics_footer*)(((uintptr_t)bp)+cur_size+next_page_header->block_size-sizeof(ics_footer));

        cur_size+=(last_page_ft->block_size)+next_page_header->block_size;
        //update footer and header
        lst_pg_hdr->block_size = cur_size;
        nxt_pg_ftr->block_size = cur_size;

        bp = lst_pg_hdr;
    }
    return bp;
}
