#include "my_pthread_t.h"
#include "my_allocate.h"
#include <stdio.h>

#define TESTING 0

static char* physical;
int is_init = 0;
static char page[8192];
char * page_ptr = page;
unsigned int mem_size = 4096;

free_list* f_list = 0;
occupied_list* o_list = 0;
int pagesize = 0;
int reserved_pages = 48;
int total_user_pages = 2000;
unsigned int reserved_mem_size = 4096;
long int swap_offset_count = 0;
int i;
page_info* occupied_or_not(void* address){
	thread_info* tmp = o_list->head;
	while(tmp != NULL){
		page_info* heap = tmp->head;
		while(heap != NULL){
			if(heap->current_ptr == address){
				return heap;
			}
			heap = heap->next_page;
		}

		tmp=tmp->next_thread;
	}

	return NULL;
}

thread_info* get_thr_info(long int thr_id){
	thread_info* tmp = o_list->head;
	while(tmp != NULL){
		if(tmp->thread_id==thr_id){
			return tmp;
		}
		tmp=tmp->next_thread;
	}

	return NULL;
}

void del_ele_from_occupied_list(thread_info* t){
	if(o_list->head == t){
		dequeue_occupied_list();
	}else{
		t->pre_thread->next_thread=t->next_thread;
		t->next_thread->pre_thread=t->pre_thread;
		t->pre_thread=NULL;
		t->next_thread=NULL;
	}
}

void enqueue_occupied_list(thread_info* t){
	if(o_list->head == NULL){
		o_list->head = t;
		o_list->tail = t;
	}else{
		t->pre_thread = o_list->tail;
		o_list->tail->next_thread=t;
		o_list->tail=t;
	}
}

thread_info* dequeue_occupied_list(){
	thread_info* tmp=o_list->head;

	if(tmp == NULL){
		return NULL;
	}

	o_list->head=tmp->next_thread;
	if(o_list->head!=NULL){
		o_list->head->pre_thread=NULL;
	}
	tmp->next_thread=NULL;

	return tmp;
}

void enqueue_free_list(page_info* p){
	if(f_list->head == NULL){
		f_list->head = p;
		f_list->tail = p;
	}else{
		f_list->tail->next_page=p;
		f_list->tail=p;
	}
}

page_info* dequeue_free_list(){
	page_info* tmp=f_list->head;

	if(tmp == NULL){
		return NULL;
	}

	f_list->head=tmp->next_page;
	tmp->next_page=NULL;

	return tmp;
}

void enqueue_thread_info(page_info* p, thread_info* t){
	if(t->head == NULL){
		t->head = p;
		t->tail = p;
	}else{
		t->tail->next_page=p;
		t->tail=p;
	}
}

page_info* dequeue_thread_info(thread_info* t){
	page_info* tmp=t->head;

	if(tmp == NULL){
		return NULL;
	}

	t->head=tmp->next_page;
	tmp->next_page=NULL;

	return tmp;
}

void initialize_memory_management(){
	int i,size,c=0;
	//initialize system reserved memory
	pagesize = sysconf(_SC_PAGE_SIZE);

	//physical = (char*)memalign(pagesize, 2^23);
	for(size=0;size<8000;size+=1){
		char  *p=memalign(pagesize, 1024);
		if(c==0){
			physical=p;
		}
		c++;
	}
	
	//Change environment to reserved area
	page_ptr=physical;
	mem_size=reserved_mem_size;
	mem_init();
	//printf("(PMEM/sysconf(_SC_PAGE_SIZE)) * sizeof(page_info): %d\n", (PMEM/sysconf(_SC_PAGE_SIZE)) * sizeof(page_info));
	//printf("sizeof(free_list): %d\n", sizeof(free_list));
	f_list = myallocate(sizeof(free_list), __FILE__, __LINE__, SCHED_THREAD);
	f_list->head=NULL;
	f_list->tail=NULL;

	//printf("sizeof(occupied_list): %d\n", sizeof(occupied_list));
	o_list = myallocate(sizeof(occupied_list), __FILE__, __LINE__, SCHED_THREAD);
	o_list->head=NULL;
	o_list->tail=NULL;
	//sched->thr_main = (mypthread_t *) calloc(1, sizeof(mypthread_t));

	for(i=0;i<total_user_pages;i++){
		page_info* p_i = myallocate(sizeof(page_info), __FILE__, __LINE__, SCHED_THREAD);
		p_i->thread_id=0;
		p_i->current_ptr= physical+(reserved_pages+i)*pagesize;//?
		//p_i->current_ptr= physical+(2^(i+reserved_pages)-1)
		p_i->original_ptr= p_i->current_ptr;
		p_i->next_page=NULL;
		//swap offest context
		p_i->swap_offset = -1;
		enqueue_free_list(p_i);
	}

	reserved_mem_size = mem_size;

	// Create Swap file 
	// FILE *fp=fopen("swap.txt", "w");
	// fseek(fp, 1024*1024*16, SEEK_SET);
	// fputc('0', fp);
	// fclose(fp);
	return;
}

void*  initialize_new_thread(long int thr_id, unsigned int size){
	//Change environment to reserved area
	page_ptr=physical;
	mem_size=reserved_mem_size;
	thread_info* t_i=myallocate(sizeof(thread_info), __FILE__, __LINE__, SCHED_THREAD);

	t_i->thread_id=thr_id;
	t_i->mem_size=pagesize; // heap size
	t_i->stack=NULL;
	t_i->head=NULL;
	t_i->tail=NULL;
	t_i->pre_thread=NULL;
	t_i->next_thread=NULL;

	t_i->stack=dequeue_free_list(); // allocate stack
	if(t_i->stack == NULL){
		return NULL;
	}
	t_i->stack->thread_id=thr_id;

	page_info* tmp=dequeue_free_list();
	if(tmp == NULL){
		return NULL;
	}
	tmp->thread_id=thr_id;

	enqueue_thread_info(tmp, t_i); // allocate heap

	enqueue_occupied_list(t_i);  // add new thread to occupied list

	//save memory size before change environment
	reserved_mem_size=mem_size;

	//change to heap environment
	page_ptr=t_i->head->current_ptr;
	mem_size=t_i->mem_size;
	mem_init();  

	//change to stack environment
	page_ptr=t_i->stack->current_ptr;
	mem_size=t_i->mem_size;
	mem_init(); 

	return myallocate(size, __FILE__, __LINE__, thr_id); //give a full page
}

void mem_init(void) {
/*
	This function initializes the memory for a newly defind thread. It adds
	the space not used for bookkeeping in the free list maintains in the
	same page.
*/
	
	// There are 14 bins, each associated with a different size. Each bin
	// tracks a head pointer and a tail pointer, each 4B. So the mallocable
	// block start 104B after the starting address. The first 104B are
	// initialized to be 0. An integer of 0 for the address of a free list
	// indicates that the free list is empty.

	int i;
	int * bin_head = NULL;
	int * bin_tail = NULL;
	
	for (i = 0; i < 112; i =  i + 8) {
		bin_head = (int *) (page_ptr + i);
		bin_tail = (int *) (page_ptr + i + 4);
		printf("bin_head: %p\t bin_tail: %p\n", bin_head, bin_tail);
		memcpy(page_ptr + i, &bin_tail, sizeof(int));
		memcpy(page_ptr + i + 4, &bin_head, sizeof(int));
	}

	void * tmp_page_ptr = (void *)(page_ptr + (14 * 8));
	
	// There are 3984 byte remaining. We will use the first 4B and last 4B
	// to write the record the size of the memory. There is effectively 3988B
	// left for the user to use.
	int start_size = 3984;
	memcpy(tmp_page_ptr, &start_size, sizeof(start_size));
	memcpy(tmp_page_ptr + 3980, &start_size, sizeof(start_size));

//	printf("The head size is %d\n", *((int *) tmp_page_ptr));
//	printf("The tail size is %d\n", *((int *) (tmp_page_ptr + 3980)));
	// Insert the beginning block into the CDLL of the proper size
	list_insert(tmp_page_ptr);
//	printf("The head address of block: %p\n", (int *) (bin_select(start_size)));
//	printf("The tail address of block: %p\n", (int *) (bin_select(start_size) + 4));
}


void * myallocate(unsigned int size, char * filename, int lineno, int req_id) {//req_id==THREADREQ: change environment to cur_thr_id
																			   //req_id==SCHED_THREAD: change environment to reseved area
																			   //req_id==others: change environment to req_id's stack
/*
	This function takes in the requested size and returns a pointer. The 
	pointer points to a block if a block of the requested size can be
	found. Otherwise, the block will return NULL. If we find that there
	is a block that is bigger than the requested size, we will split the
	block into smaller chunks in order to increase spac4e efficiency.
*/
	thread_info* target;
	long int thr_id=get_cur_thr_id();

	if (size == 0) {
		return NULL;
	}

	if (req_id == THREADREQ){
		target=get_thr_info(thr_id);
		if(target == NULL){
			return NULL;
		} 
		page_ptr=target->head->original_ptr; //1st page of heap
		mem_size=target->mem_size;
	} else if(req_id == SCHED_THREAD){
		page_ptr=physical;
		mem_size=reserved_mem_size;
	} else {
		target=get_thr_info(req_id);
		if(target == NULL){
			return NULL;
		} 
		page_ptr=target->stack->original_ptr; //stack page
		mem_size=target->mem_size;
	}

	void * ptr = NULL;
	unsigned int req_size = size_roundup(size);
	printf("size_roundup(%d) = %d\n", size, req_size);
	unsigned int check_size = req_size;
	void * bin = bin_select(req_size);
	
	unsigned int reselect[14] = {16, 32, 48, 64, 80, 96, 112, 128, 144, 272, 528, 1040, 2064, 4112};
	int reselect_index = 0;

	ptr = pick_to_remove(check_size);
	
	if (!ptr) {
		while (check_size > reselect[reselect_index]) {
			reselect_index++;
		}
		while (reselect_index < 14) {
			ptr = pick_to_remove(reselect[reselect_index]);
			if (!ptr) {
				reselect_index++;
			} else {
				break;
			}
		}
		if (reselect_index >= 14) {
			//when to stop?
			if (req_id == THREADREQ){
				page_info* last_page;
				page_info* next_page;
				page_info* free_page;
				thread_info* another_thread;
				
				void* next_page_ptr;

				if(target == NULL){
					return NULL;
				} 
				last_page=target->tail;
				next_page_ptr=last_page->current_ptr+pagesize;
				next_page=occupied_or_not(next_page_ptr);
				if(next_page!=NULL){
					another_thread = get_thr_info(next_page->thread_id);

					mprotect(next_page->current_ptr, pagesize, PROT_READ | PROT_WRITE); // unlock next page
					free_page=dequeue_free_list();

					// pages are not available
					if (free_page == NULL){
						FILE *fp=fopen("swap.txt", "a");


						page_info* evict_page =  occupied_or_not ( physical+(reserved_pages)*pagesize) ;

						if(evict_page->swap_offset == -1){
							fwrite(evict_page, sizeof(page_info), 1, fp);
							evict_page->swap_offset = swap_offset_count;
							swap_offset_count++;
						}
						else{
							i = 1;
							while( evict_page->swap_offset != -1 ){
								evict_page =  occupied_or_not ( physical+(reserved_pages+i)*pagesize);
								i++;
							};
							fwrite(evict_page, sizeof(page_info), 1, fp);
							evict_page->swap_offset = swap_offset_count;
							swap_offset_count++;
						}
						
						fclose(fp);
						if (swap_offset_count > 4096){
							memcpy(evict_page->current_ptr, next_page->current_ptr, pagesize);
							next_page->current_ptr=evict_page->current_ptr;//belongs to another_thread
							mprotect(next_page->current_ptr, pagesize, PROT_NONE); // unlock next page

							evict_page->thread_id=thr_id;
							evict_page->current_ptr=next_page_ptr;
							evict_page->original_ptr=next_page_ptr;
						}
						else{
							return NULL;
						}
						
					}
					else{
						memcpy(free_page->current_ptr, next_page->current_ptr, pagesize);
						next_page->current_ptr=free_page->current_ptr;//belongs to another_thread
						mprotect(next_page->current_ptr, pagesize, PROT_NONE); // unlock next page

						free_page->thread_id=thr_id;
						free_page->current_ptr=next_page_ptr;
						free_page->original_ptr=next_page_ptr;

						enqueue_thread_info(free_page, target);
					}
					
				}

			} 


			page_extend();
			if (req_id == THREADREQ){
				target->mem_size=mem_size;
			} else if(req_id == SCHED_THREAD){
				reserved_mem_size=mem_size;
			} else {
				//do nothing, specially for stack
			}
			return myallocate(size, filename, lineno, req_id);
			//printf("There are no blocks which satisfy the request\n");
			//return NULL;
		}
		

	}

	block_remove(ptr);
	
	if (*((int *) ptr) > req_size) {
		printf("*((int *) ptr) : %d\t req_size: %d\n", *((int *) ptr), req_size);
		split(req_size, ptr);
	}

	if (req_id == THREADREQ){
		target->mem_size=mem_size;
	} else if(req_id == SCHED_THREAD){
		reserved_mem_size=mem_size;
	} else {
		//do nothing, specially for stack
	}
	return ptr + 4;
}


void mydeallocate(void * block, char * filename, int lineno, int req_id) {
/*
	This function takes in a pointer to a malloc'd block and frees the
	memory. During this process, we will check if the adjacent blocks are
	free. If there is at least one free adjacent block, the freed block will
	be merged to create one contiguous free block.
*/
	thread_info* target;
	long int thr_id=get_cur_thr_id();

	if (block == NULL) {
//		printf("Why am I freeing a NULL block?\n");
	} else {
		if (req_id == THREADREQ){
			target=get_thr_info(thr_id);
			if(target == NULL){
				return NULL;
			} 
			page_ptr=target->head->original_ptr; //1st page of heap
			mem_size=target->mem_size;
		} else if(req_id == SCHED_THREAD){
			page_ptr=physical;
			mem_size=reserved_mem_size;
		} else {
			target=get_thr_info(req_id);
			if(target == NULL){
				return NULL;
			} 
			page_ptr=target->stack->original_ptr; //stack page
			mem_size=target->mem_size;
		}

		void * tmp_block = block - 4;
//		printf("Checking Join Left\n");
		tmp_block = join_left(tmp_block);
//		printf("Checking Join Right\n");
		tmp_block = join_right(tmp_block);
//		printf("Inserting free block\n");
		list_insert(tmp_block);

		if (req_id == THREADREQ){
			target->mem_size=mem_size;
		} else if(req_id == SCHED_THREAD){
			reserved_mem_size=mem_size;
		} else {
			//do nothing, specially for stack
		}
	}
}


void page_extend(void) {
/*
	This function extends the physica; memory of a thread by one page. This
	function assumes that the memory will be contiguous. If a new page is
	assigned to thread, the memory manage must first allocate a new page to
	a thread, then move the newly assigned page to the end of the existing
	memory block to maintain a contiguous block of memory. Then, the function
	can add a new block to the free list of the existing mallocable memory.
*/

	// Initializing the new page as a contiguous block to mallocable memory.
	// Adding size overhead information to conform with malloc mechanisms
//	printf("Initializing free page as mallocable block\n");
	int * new_page_ptr = (int *) (page_ptr + mem_size); 
//	printf("page_ptr: %p\t new_page_ptr: %p\n", page_ptr, new_page_ptr);
	*new_page_ptr = 4096;
//	printf("*new_page_ptr: %d\n", *new_page_ptr);
	*(new_page_ptr + (4092 / 4)) = 4096;

	// Checking if the block to the left is also free. new_page_ptr will either
	// point to the head of the joined block if the left block is free. Otherwise,
	// new_page_ptr will point to the same block as before.
//	printf("Joining page extension with left block\n");
	new_page_ptr = join_left((void *) new_page_ptr);

	// We are now sure that the newly added free space from the page forms a
	// contiguous free space with the end of the existing memory (aka wilderness).
	// Now we insert the block into the free list.
//	printf("Inserting the new block into mallocable memory\n");
	list_insert((void *) new_page_ptr);

	// Finally, we increase the total memory size by 4096;
	mem_size = mem_size + 4096;
//	printf("mem_size: %d\n", mem_size);

}

void * join_left(void * block) {
/*
	This function takes in a pointer to a block and checks if the adjacent
	block to the left is free. If so, it will join the two blocks and return
	a pointer to the newly joined block. Otherwise, it will return a pointer
	to the current block.
*/
	int * block_ptr = (int *) block;
//	printf("page_ptr: %p\t page_ptr + 112: %p\n", page_ptr, page_ptr + 112);
//	printf("block_ptr: %p\n", block_ptr);
	if ((block_ptr - 1) < (page_ptr + 112)) {
//		printf("Free lists are to the left\n");
		return block;
	}
	else {
//		int block_size = *block_ptr;
		if (*(block_ptr - 1) % 2 == 0) {
//			printf("The left block is in use\n");
			return block;
		} else {
//			printf("The left block is also free. Removing left block from the list\n");
			int block_size = *block_ptr;
			int left_size = *(block_ptr - 1) - 1;
			int * left_block = block_ptr - (left_size / 4);
			block_remove((void *) left_block);

//			printf("Merging the left block with the current block\n");
			int new_block_size = block_size + left_size;
			*left_block = new_block_size;
			block_ptr = block_ptr + (block_size / 4) - 1;
			*block_ptr = new_block_size;
			
//			printf("Returning left-merged block\n");
			return left_block;
		}
	}
}


void * join_right(void * block) {
/*
	The function takes in a pointer to a block and checks if the adjacent
	block to the right is free. If so, it will join the two blocks and return
	a pointer to the newly joined block. Otherwise, it will return a pointer
	to the current block

	MING:: It should be noted that joining a block with a free block to is
	right does not actually changed the pointer itself. After all, the pointer
	just points to the beginning. The decision to return a pointer anyways
	was one of symmetry, so that it matches better with join_left.
*/

	int * block_ptr = (int *) block;
	int block_size = *block_ptr;
	if ((block_ptr + (block_size / 4)) >= (page_ptr + mem_size)) {
//		printf("This block is at the end of the memory\n");
		return block;
	} else {
		if (*(block_ptr + (block_size / 4)) % 2 == 0) {
//			printf("The right block is in use\n");
			return block;
		} else {
//			printf("The right block is also free. Removing right block from free list\n");
			int * right_block = block_ptr + (block_size / 4);
			int right_size = *right_block - 1;
			block_remove((void *) right_block);

//			printf("Merging the right block with the current block\n");
			int new_block_size = block_size + right_size;
			*block_ptr = new_block_size;
			right_block = right_block + (right_size / 4) - 1;
			*right_block = new_block_size;

//			printf("Returning right-merged block\n");
			return block_ptr;
		}
	}
}

void * split(unsigned int req_size, void * block) {
/*
	This function takes in a requested size and a block that is assumed to
	be larger than that of the requested size, and splits the blocks into
	two blocks: a block of the requested size, and a block of the leftover
	size. The function then returns a pointer to the block of the requested
	size. The leftover block will be inserted into a free list for later use.
*/

	// Defining all of the required objects. rmd stands for remainder and is
	// associated with the block that trimmed of the larger block.
//	printf("Declaring objects\n");
	int * block_ptr = (int *) block;
	//int * rmd_ptr;
//	printf("block_ptr: %p\t *block_ptr: %d\n", block_ptr, *block_ptr);
	int total_size = *((int *) block);
	int rmd_size = total_size - req_size;

	// The following steps giving the blocks the proper size overhead blocks
	// Making changes to the block of the request size
//	printf("Making changes to requested block\n");
	*block_ptr = req_size;
//	printf("block_ptr: %p\t *block_ptr: %d\n", block_ptr, *block_ptr);
	block_ptr = block_ptr + (req_size / 4) - 1;
	*block_ptr = req_size;
//	printf("block_ptr: %p\t *block_ptr: %d\n", block_ptr, *block_ptr);
	
	// Making changes to the remaining block
//	printf("Making changes to remainder block\n");
	int * rmd_ptr = block_ptr + 1;
//	printf("rmd_ptr: %p\t *rmd_ptr: %d\n", rmd_ptr, *rmd_ptr);
	*rmd_ptr = rmd_size;
//	printf("rmd_ptr: %p\t *rmd_ptr: %d\n", rmd_ptr, *rmd_ptr);
	rmd_ptr = rmd_ptr + (rmd_size / 4) - 1;
//	printf("rmd_ptr: %p\t page[4092]: %p\n", rmd_ptr, page + 4092);
//	printf("Setting size again\n");
	*rmd_ptr = rmd_size;
//	printf("Set the size\n");

//    printf("rmd block size: %d\n", *rmd_ptr);
	// Reseting the pointer to the remainder block for list insertion
//	printf("Inserting the remaining block\n");
	rmd_ptr = block_ptr + 1;
	list_insert(rmd_ptr);

	// Return the original block
//    printf("Returning remaining block.\n");
	return block;
}


void list_insert(void * block) {
/*
	This function takes in a pointer to a block and inserts it into a doubly
	circularly linked list of the associated size. The pointer to the block
	is assumed to be pointing to the head of a free block. It is assumed that
	any block will be inserted at the end of a DCLL. No insertion can occur
	anywhere else in the DCLL.
*/
	
	// Looking for the size of the block, then using that information to find
	// the correct bin to insert.

	int size = *((int *) block);
	int * bin = (int *) bin_select(size);
	int * tmp_block = (int *) block;

	printf("*bin: %x\n", *bin);
	// Adding 1 to the head size and tail size to denote it is now free
	int free_size = size + 1;
	memcpy(block, &free_size, sizeof(int));
	memcpy((block + size - sizeof(int)), &free_size, sizeof(int));

	// Inserting the block. Inserting the block is done by storing the
	// address of the pointers as values char array
	
	int * tail_last = (int *) *bin;				// A pointer to the last element in the CDLL
	int * head_bin = bin;
	int * head_block = (int *) (block + 4);
	int * tail_block = (int *) (block + 8);

	memcpy(block + 4, &tail_last, sizeof(int));	// Write the tail of last to head of block
	memcpy(block + 8, &bin, sizeof(int));		// Write the head of bin to tail of block
	memcpy(tail_last, &head_block, sizeof(int));// Write the head of block to tail of last
	memcpy(bin, &tail_block, sizeof(int));		// Write the tail fo block to head of bin

//	printf("*(block + 4): %x\t tail_last: %p\n", *(tmp_block + 1), tail_last); 
//	printf("*(block + 8): %x\t bin: %p\n", *(tmp_block + 2), bin); 
//	printf("*(bin + 1): %x\t head_block: %p\n", *(bin + 1), head_block); 
//	printf("*bin: %x\t tail_block: %p\n", *bin, tail_block); 
//	printf("block: %p\t *block: %d\n", block, *((int *)block)); 
}


void * pick_to_remove(unsigned int size) {
/*
	This function takes in a size and removes a block from the doubly circularly
	linked list of the associated size. This assumes that the size is a multiple
	of 16.
*/
	
	// Deciding which bin to check
	int * bin = (int *) bin_select(size);
	//printf("Inside pick_to_remove function\n");

//	printf("bin : %p\n", bin);
//	printf("(int *) *(bin + 1) %p\n", (int *) *(bin + 1));
	// Checking if the bin is empty. Be careful of the pointer arithemtic!
	if (bin == (int *) *(bin + 1)) {
		//printf("This bin is empty\n");
		return NULL;
	} 
	// If it is not empty, then we must remove one from the 
	else {
		// Declaring a pointer to the first block. If the size of the block is
		// less than or equal to 128, then the blocks are fixed size and we can
		// return the first block. If, however, the size is greater than 128,
		// the blocks are variable size. In that case, we must go through the
		// the list to find a block whose size is greater than or equal to the
		// the size.

		// The tail of the bin points to the head of the next block
		int * pick_block = (int *) *(bin + 1);
//		printf("pick_block: %p\n", (int *) *(bin + 1));
//		printf("About to start the while-loop\n");
		if (size > 128) {
			// Check if block can is less than the block size, we continue to
			// the next block. If the no block of the requested size can be
			// found in the list, we return NULL. Once we find a block, we
			// stop checking the list.
			int * list_start = bin;
//			printf("list_start: %p\n", bin);
			while (pick_block != list_start) {
				if (*(pick_block - 1) < size) {
					pick_block = (int *) *(pick_block + 1);
					printf("\tBlock too small\n");
				} else {
					printf("\tFound block\n");
					break;
				}
			}
			if (pick_block == list_start) {
				printf("This list does not have a block size that can fulfill the request\n");
				return NULL;
			}
		}
//		printf("pick_block - 1: %p\t *pick_block %d\n", pick_block - 1, *(pick_block - 1));
		return (void *) (pick_block - 1);
	}
}
void block_remove(void * block){
		// From this point on, we assume that we have a block which we can remove
		// from the bin of the given size. We already check if the bin is empty
		// and made sure that the bin has at least one block which is big enough.
		// (The last bit is only relevant for the variable size bins. The fixed
		// sized bins satisfy this criterion by construction.

//		printf("Inside block_remove function\n");
		int * pick_block = (int *) block;
//		printf("block: %p\n", block);

		// Removing the block from the list
//		printf("Defining objects\n");
		int * tail_last = (int *) *(pick_block + 1);
//		printf("tail_last: %p\t *tail_last: %x\n", tail_last, *tail_last);
		int * head_next = (int *) *(pick_block + 2);
//		printf("head_next: %p\t *head_next: %x\n", head_next, *head_next);
		int * head_block = pick_block + 1;
//		printf("head_block: %p\t *head_block: %x\n", head_block, *head_block);
		int * tail_block = pick_block + 2;
//		printf("tail_block: %p\t *tail_block: %x\n", tail_block, *tail_block);

//		printf("Right before the memcpy\n");
		memcpy(tail_last, tail_block, sizeof(int));
		memcpy(head_next, head_block, sizeof(int));
	
//	printf("*tail_last: %p\t tail_last: %p\n", *tail_last, tail_last); 
//	printf("*head_next: %p\t head_next: %p\n", *head_next, head_next); 
	
		*head_block = pick_block + 1;
		*tail_block = pick_block + 2;
//	printf("*head_block: %p\t head_block: %p\n", *head_block, head_block); 
//	printf("*tail_block: %p\t tail_block: %p\n", *tail_block, tail_block);

//	printf("*head_block: %x\t tail_block: %x\n ", *head_block, *tail_block); 

//		printf("Finalizing\n");

		// Finalizing, adjusting the head and tail size values to denote that the
		// block will be used.

//		printf("head_block - 1: %p\t size: %d\n", head_block - 1, *(head_block - 1));

		int * block_ptr = head_block - 1;
//		printf("block_ptr: %p\n", block_ptr);
		*block_ptr = *block_ptr - 1;
		
//		printf("*block_ptr: %d\n", *block_ptr);
		int * back_block = block_ptr + (*block_ptr / 4) - 1;
		*back_block = *block_ptr;
//		printf("The head size of the block is %d\n", *block_ptr);
//		printf("The tail size of the block is %d\n", *back_block);
	//	return (void *) (block_ptr);
}

void * bin_select(unsigned int real_size) {
/*
	This function takes in a real size and returns a pointer which points to the
	location of the bin. There are fixed size bins, where the blocks in these
	bins are all the same size. The sizes and their location can be found in the
	as the individual cases. All the other bins are variable size bins, found as
	the individal if-statements in the default case.
*/
	if (real_size % 16 == 0) {
		switch (real_size) {
			case 16:
				return page_ptr;
			case 32:
				return page_ptr + 8;
			case 48:
				return page_ptr + 16;
			case 64:
				return page_ptr + 24;
			case 80:
				return page_ptr + 32;
			case 96:
				return page_ptr + 40;
			case 112:
				return page_ptr + 48;
			case 128:
				return page_ptr + 56;
			default:
				if (real_size <= 256) {
					return page_ptr + 64;
				}
				if (real_size <= 512) {
					return page_ptr + 72;
				}
				if (real_size <= 1024) {
					return page_ptr + 80;
				}
				if (real_size <= 2048) {
					return page_ptr + 88;
				}
				if (real_size <= 4096) {
//					printf("Pointer to 4096: %p\n", page_ptr + 96);
					return page_ptr + 96;
				}
//				printf("Pointer to blocks larger than 4096: %p\n", page_ptr + 104);
				return page_ptr + 104;
			}
		} else {
			printf("The block you are adding is not a multiple of 16.\n");
		}
}

unsigned int size_roundup(unsigned int size) {
/*
	This function takes in a size and rounds it up to the smallest multiple
	of 16 greater than the given size. This is done in order to ensure that
	the joining and splitting of blocks can always be inserted to some doubly
	circularly linked list. A function is added to increase readability.
*/

//  Being to fancy below, and missed some edge cases
//	int req_size = (size + 8) + (16 - ((size + 8) % 16));
//	if (((size + 8) % 16 == 0) && ((size / 8) % 2 == 1)) {
//		req_size = req_size - 16;
//	}
    
    while (size % 8 != 0) {
        size++;
    }
    if (size % 8 == 0) {
        if (size % 16 == 8) {
            return size + 8;
        } else {
            return size + 16;
        }
    }
}


#if TESTING

int main() {

	mem_init();
//	void * temp = pick_to_remove(3984);
//	block_remove(temp);
//	void * temp2 = split(16, temp); 
//	void * rmd = pick_to_remove(3984);
//	rmd = pick_to_remove(2000);

	unsigned int i;

	for (i = 3950; i < 4051; i++) {
		printf("size_roundup(%d) = %d\n", i, size_roundup(i));
	}



/*
	void * temp1 = malloc(2000);
	void * temp2 = malloc(123);
	void * temp3 = malloc(534);
	free(NULL);
	printf("temp1: %p\n", temp1);
	free(temp1);
	printf("\n\n");
	printf("temp3: %p\n", temp3);
	free(temp3);
	printf("\n\ntemp2\n");
	free(temp2);
	
	printf("\n\ntemp4\n");
	void * temp4 = malloc(3984);
	free(temp4);

	void * temp5 = malloc(1976);
	void * temp6 = malloc(1977);
	printf("\n\ntemp6\n");
	free(temp6);
	printf("\n\ntemp5\n");
	free(temp5);

	//printf("\n\ntemp11\n");
	//temp1=myallocate(3968, __FILE__, __LINE__, 0);
	//printf("\n\ntemp11\n");
	//free(temp1);

	page_extend();

*/

    return 0;

}

#endif
