#ifndef HELPERS4_H
#define HELPERS4_H

// A header file for helpers.c
// Declare any additional functions in this file

//word size
#define WSIZE 8
#define DSIZE 16 //double word size
#define QSIZE 32 //quad word size
#define CHUNKSIZE (1<<12) //page size(2^12)

#endif

extern volatile int allocated_pages;

ics_free_header *find_fit(size_t asize);
void place(void* bp, size_t asize, size_t req_size);
void remove_from_list(ics_free_header*fbp);
void insert_in_order(ics_free_header*fbp);
void* coalesce(void* bp);
