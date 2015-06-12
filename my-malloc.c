#include "my-malloc.h"
#include <unistd.h>
#include <stdio.h>
#include <math.h>

//----------------------------------------------
//------------ DATA TYPE DEFINITION ------------
//----------------------------------------------
#define MOST_RESTRICTING_TYPE double

typedef unsigned long size_t;

struct _header {
	unsigned long blockSize;
	struct _header *next;
};
typedef struct _header Header;

#define SIZE_BLOCK sizeof(Header)
#define NEXT(header) (*header).next
#define SIZE(header) (*header).blockSize
#define MIN_BLOCK 50

//----------------------------------------------
//------------ DEBUGGING TOOLS -----------------
//----------------------------------------------
#ifdef MALLOC_DBG
static int nb_alloc = 0, nb_dealloc = 0, nb_sbrk = 0;

void printBlockList(Header* head)
{
	fprintf(stderr, "Free blocks: ");
	Header* h = head;
	do
	{
		printf("{0x%08x, %d (0x%02x bytes)} -> ", h, SIZE(h), SIZE(h));
		h = NEXT(h);

	} while(h != head);
	printf("\n");
}
#endif

//----------------------------------------------
//----------------------------------------------

Header freeList = {0, &freeList};

void allocate(Header *tail, int nBlocks)
{
	//Request memory from OS
	void* newBlock = sbrk( nBlocks * SIZE_BLOCK );	

	//First block of newBlock is the header.
	//Set header->next to be the our freeList head, and set
	//previous->next to be our newBlock. This effectively
	//inserts the block in the circular list.
	Header *blockHeader = (Header*)newBlock;
	blockHeader->blockSize = nBlocks;
	blockHeader->next = &freeList;
	tail->next = blockHeader;

	#ifdef MALLOC_DBG
	printf("Allocating new memory\n");
	printf("Requested %d blocks -> %d bytes\n", nBlocks, nBlocks * SIZE_BLOCK);
	printBlockList(&freeList);
	nb_sbrk++;
	#endif
}

void* mymalloc(size_t n)
{
	// Calculate the number of blocks we need to allocate
	// 1 block for the header and n/SIZE_BLOCK blocks for the
	// payload
	int nBlocks = 1 + (int)ceil( (double)n/SIZE_BLOCK );

	Header *cur, *prev;
	for(prev = &freeList, cur = NEXT(&freeList); ;prev = cur, cur = cur->next)
	{
		// No space available
		if(cur == &freeList)
		{
			//Request memory and insert it in list
			allocate(prev, nBlocks < MIN_BLOCK ? MIN_BLOCK : nBlocks);
			continue;
		}

		// Space available?
		if( cur->blockSize > nBlocks )
		{	
			//We split the block in two parts.
			//The second half will be returned and we'll leave the first one free.
			//splitPoint hold the block we'll return
			//We must resize the block
			size_t offset = cur->blockSize - nBlocks;
			cur->blockSize = offset;

			Header *splitPoint = cur + offset;
			splitPoint->blockSize = nBlocks-1; //nBlocks - 1 -> the block containing metadata
			splitPoint->next = 0;
		
			//Debug info
			#ifdef MALLOC_DBG
			nb_alloc++;
			printf("\nRequested: %d bytes [%d blocks]\n", n, nBlocks);
			printf("Current position: 0x%08x, offset: %d\n", cur, offset);
			printf("New block header: 0x%08x, size: %d, returned valued: 0x%08x\n", splitPoint, SIZE(splitPoint), (splitPoint + 1));
			printBlockList(&freeList);
			#endif

			return (void*)(splitPoint + 1);
		}	
	}
}

void* mycalloc(size_t nmemb, size_t size)
{
	size_t total = nmemb * size;
	void* out = mymalloc(total);
	bzero(out, total);
	return out;
}

void myfree(void* ptr)
{
	Header* metadata = (Header*)ptr - 1; 
	
	printf("\n-------- Freeing stuff here -----------\n");
	printf("Address: 0x%08x, Next: %p, Size: %lu\n", metadata, NEXT(metadata), SIZE(metadata));
	
	Header *cur, *prev;
	
	for(prev = &freeList, cur = NEXT(&freeList); ;prev = cur, cur = cur->next)
	{
		printf("Cur: %p , prev: %p , metadata: %p\n", cur, prev, metadata);

		if (cur == prev || cur > metadata || (cur < prev && (cur < metadata && prev < metadata)) ) { //we found the block to remove, it's between cur and prev

			//we link the new empty block
			prev->next=metadata;
			metadata->next=cur;

			//we check if we can join the free bloc with the one before/after
			if(metadata+SIZE(metadata)+1 == cur)
			{
				metadata->next=cur->next;//NEXT(&cur)?
				metadata->blockSize += SIZE(cur) + 1;

				printf("Sticking blocks here. metadata + size(metadata) == cur\n");
			}
			if(metadata-SIZE(prev)-1 == prev)
			{
				prev->next=cur;
				prev->blockSize += SIZE(metadata) + 1;

				printf("Sticking blocks here. metadata - SIZE(prev) -1 == prev\n");
			}

			break;
		}
	}

	nb_dealloc++;
	printBlockList(&freeList);
}

void* nextFreeBlock(void *ptr)
{
	Header *cur;
	for(cur = NEXT(&freeList); cur < NEXT(cur); cur = cur->next)
	{
		printf("cur: %p next: %p\n", cur, NEXT(cur));
		if(cur > ptr) return cur;
	}
	return NULL;
}

Header* getPreviousBlock(Header* b)
{
	Header *prev = &freeList, *cur = NEXT(&freeList);
	for( ; cur != b && cur != NEXT(cur); prev = cur, cur = cur->next);
		return prev;
}

void *myrealloc(void *ptr, size_t size)
{
	Header* metadata = (Header*)ptr - 1;
	
	Header* next = nextFreeBlock(ptr);
	//If the bloc after is free 
	if(ptr + size + 1 == next)
	{
		Header* oldHeaderNextFree = nextFreeBlock(ptr);

		//Create header of new shifted block
		Header* nextFree = (Header*)(ptr + size);
		nextFree->blockSize = SIZE(oldHeaderNextFree) - (size - SIZE(metadata));
		nextFree->next = oldHeaderNextFree->next;
		getPreviousBlock(oldHeaderNextFree)->next = nextFree;

		//Resize PTR
		metadata->blockSize = size;

		return ptr;
	}
	else {

		Header *new = mymalloc(size);
		memcpy(ptr, new, size);
		myfree(ptr);
		ptr=&new;

		return new;
	}
}

#ifdef MALLOC_DBG
void mymalloc_infos(char *msg)
{
	if (msg) fprintf(stderr, "**********\n*** %s\n", msg);

	fprintf(stderr, "# allocs = %3d - # deallocs = %3d - # sbrk = %3d\n",
		nb_alloc, nb_dealloc, nb_sbrk);

	printBlockList(&freeList);

	if (msg) fprintf(stderr, "**********\n\n");
}
#endif
