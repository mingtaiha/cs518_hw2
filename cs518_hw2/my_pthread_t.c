//	AUTHORs: Ming Tai Ha, Harsh Kothari, Zheng Yu
//	Ilab Machine : cd.cs.rutgers.edu

#include "my_pthread_t.h"
#include "my_allocate.h"
#include <unistd.h>

static long int thr_id = 0;
static long int check_flag = 0;
static scheduler * sched;
static mypthread_t * thr_list;
static my_pthread_mutex_t* mutex1;
static int sharedVariable = 0;
static int sharedVariable1 = 0;
static int start = 0;
static int end = 0;

long int cur_thr_id = 0;

// static flag = 0;

long int get_cur_thr_id(){
	return cur_thr_id;
}

void free_pages(long int thr_id){
	thread_info* tmp=o_list->head;
	while(tmp != NULL){
		if(tmp->thread_id == thr_id){
			tmp->stack->thread_id=0;
			tmp->stack->original_ptr=tmp->stack->current_ptr;
			tmp->stack->next_page=NULL;
			enqueue_free_list(tmp->stack);

			page_info* heap=dequeue_thread_info(tmp);
			while(heap!=NULL){
				heap->thread_id=0;
				heap->original_ptr=heap->current_ptr;
				heap->next_page=NULL;
				enqueue_free_list(heap);
				heap=dequeue_thread_info(tmp);
			}

			del_ele_from_occupied_list(tmp);

			mydeallocate(tmp, __FILE__, __LINE__, SCHED_THREAD);
			return;
		}else{
			tmp=tmp->next_thread;
		}
	}
}

long int get_time_stamp(){
	 struct timeval current_time;
	 gettimeofday(&current_time, NULL);
	 return 1000000 * current_time.tv_sec + current_time.tv_usec;
}

void queue_init(queue * first) {
	//first = malloc(sizeof(queue));
	first->head = NULL;
	first->tail = NULL;
	first->size = 0;
}

void enqueue(queue * first, mypthread_t * thr_node) {
	/*
		If there is nothing in the queue, the head and tail nodes will
			point to the same node after insertion.
		Else, insert the node at the tail of the queue
	*/
	if (first->size == 0) {
		first->head = thr_node;
		first->tail = thr_node;
		first->size++;
	} else {
		first->tail->next_thr = thr_node;
		first->tail = thr_node;
		first->size++;
	}
}

mypthread_t * dequeue(queue * first) {
	/*
		If there is nothing in the queue, return NULL
		Otherwise,
			If the queue has only one node, point a temp pointer
				to the head and set the head and tail to NULL for 
				bookkeeping.
			Otherwise, point a temp pointer and adjust only the head.
			Return the temp pointer.
	*/
	if (first->size == 0) {
		printf("Nothing to dequeue from an empty queue. Returning NULL\n");
		return NULL;
	}
	mypthread_t * tmp;
	if (first->size == 1) {
		tmp = first->head;
		first->head = NULL;
		first->tail = NULL;
	} else {
		tmp = first->head;
		first->head = first->head->next_thr;
	}
	tmp->next_thr = NULL;
	first->size--;
	return tmp;
}

mypthread_t * peek(queue * first) {
	return first->head;
}

char queue_isEmpty(queue * first) {
	return first->size == 0;
}

void scheduler_handler(){
	struct itimerval tick;
    ucontext_t sched_ctx;
    
    //clear the timer
    tick.it_value.tv_sec = 0;
    tick.it_value.tv_usec = 0;
    tick.it_interval.tv_sec = 0;
    tick.it_interval.tv_usec = 0;
    setitimer(ITIMER_REAL, &tick, NULL);

    //perform aging
    if(check_flag++ >= CHECK_FREQUENCY){
    	// printf("Start scaling up threads...\n");
    	int i;
    	check_flag = 0;
    	long int current_time = get_time_stamp();
    	for (i = 1; i < NUM_LEVELS; i++) {
			if (sched->mlpq[i].head != NULL) {
				mypthread_t* tmp = sched->mlpq[i].head;
				mypthread_t* parent = NULL;
				while(tmp != NULL){
					if(current_time - tmp->last_exe_tt >= AGE_THRESHOLD){
						// printf("Find one candidate, its thread id is: %d\n", tmp->thr_id);
						//delete from current queue
						if(parent == NULL){
							sched->mlpq[i].head = tmp->next_thr;
						}else{
							parent->next_thr = tmp->next_thr;
						}
						//put the thread to the highest queue
						// printf("Put thread %d to highest level.\n", tmp->thr_id);
						sched_addThread(tmp, 0);
					}else{
						parent = tmp;
					}
					tmp = tmp->next_thr;
				}
			}
		}
		// printf("Finish scaling up threads...\n");
    }
    
    //schelduling
    mypthread_t* tmp = sched->thr_cur;
	if(tmp != NULL){
		int old_priority = tmp->priority;
		tmp->time_runs += TIME_QUANTUM;
		if(tmp->time_runs >= sched->prior_list[old_priority] || tmp->thr_state == YIELD || tmp->thr_state == TERMINATED 
			|| tmp->thr_state == WAITING){
			if (tmp->thr_state == TERMINATED){
				// free(tmp);
			}else if(tmp->thr_state == WAITING){
				//do nothing, the thread is already in the wait queue of the mutex
			}else if(tmp->thr_state == YIELD){
				//put the thread back into the original queue
				sched_addThread(tmp, tmp->priority);
			}else{
				//put the thread back into the queue with the lower priority
				int new_priority = (tmp->priority+1) > (NUM_LEVELS-1) ? (NUM_LEVELS-1) : (tmp->priority+1);
				sched_addThread(tmp, new_priority);
			}
			//pick another thread out and run
			if((sched->thr_cur = sched_pickThread()) != NULL){
				sched->thr_cur->thr_state = RUNNING;
			} 
		}
	}else{
		//pick another thread out and run
		if((sched->thr_cur = sched_pickThread()) != NULL){
			sched->thr_cur->thr_state = RUNNING;
		} 
	}

	//set timer
    tick.it_value.tv_sec = 0;
    tick.it_value.tv_usec = 50000;
    tick.it_interval.tv_sec = 0;
    tick.it_interval.tv_usec = 0;

    setitimer(ITIMER_REAL, &tick, NULL);

    //if(tmp != NULL){
    	//getcontext(&sched_ctx);
    	//tmp->ucp = sched_ctx;
	//}
    
    if(sched->thr_cur != NULL){
    	if(sched->thr_cur->first_exe_tt == 0){
    		sched->thr_cur->first_exe_tt = get_time_stamp();
    	}
    	sched->thr_cur->last_exe_tt = get_time_stamp();
    	if( tmp != NULL){
    		cur_thr_id = sched->thr_cur->thr_id;
    		
    		// change @memory locking unlocking 
    		// @tmp -> outgoing thread
    		// @cur_thr_id -> incoming thread

    		thread_info* out = get_thr_info(tmp->thr_id);
    		thread_info* in = get_thr_info(cur_thr_id);

    		memory_lock_unlock(out, 0);
    		memory_lock_unlock(in, 1);

    		swapcontext(&(tmp->ucp), &(sched->thr_cur->ucp));
    	}
    	else{
    		cur_thr_id = sched->thr_cur->thr_id;
    		// thread_info* out = get_thr_info(temp->thr_id);
    		// thread_info* in = get_thr_info(cur_thr_id);

    		// memory_lock_unlock(out, 0);
    		// memory_lock_unlock(in, 1);
    		swapcontext(&sched_ctx, &(sched->thr_cur->ucp));
    	}
    }
    return;
}

// 0 for lock
// 1 for unlock
void memory_lock_unlock(thread_info *t, int signal){
	page_info* start = t->head;
	page_info* end = t->tail;

	page_info* next;
	do{
		void* current_ptr = start->current_ptr;
		if(signal){
			mprotect( current_ptr, pagesize, PROT_READ | PROT_WRITE);
		}
		else{
			mprotect( current_ptr, pagesize, PROT_NONE);
		}
		next = start;
		start = start->next_page;

	}while( end != next );
}

static void handler(int sig, siginfo_t *si, void *unused){

    printf("Got SIGSEGV at address: 0x%lx\n",(long) si->si_addr);
    long int current_thr_id = get_cur_thr_id();
    thread_info* t = get_thr_info(cur_thr_id);

    page_info* start = t->head;
    page_info* end = t->tail;
    void* erro_addr = si->si_addr;

    int* tmp_erro_addr = (int*) erro_addr;
    *(tmp_erro_addr) = *(tmp_erro_addr) - (*(tmp_erro_addr) % 4096);
   	erro_addr = (void*) tmp_erro_addr;
   	tmp_erro_addr = NULL;

    page_info* next;

    do{
		void* original_ptr = start->original_ptr;
		if (original_ptr == erro_addr){
			// Find free page
			page_info* page = dequeue_free_list();
			void* free_page_addr = page->current_ptr;

			// Find error page information
			page_info* error_page = occupied_or_not(erro_addr);

			void* save_add = page->current_ptr;

			// assign temp page to error page with keeping entire reference and data
			page = error_page;

			// change error_page context
			// page_info* temp = start->next_page;
			// start = error_page;
			start->current_ptr = erro_addr;
			// start->next_page = temp;

			// Enqueue page again
			error_page->current_ptr = free_page_addr;
			error_page->original_ptr = free_page_addr;
			error_page->thread_id = 0;
			enqueue_free_list(error_page);
		}

		next = start;
		start = start->next_page;

	}while( end != next );

}

void sched_init() {
	/*
		Initializes a Scheduler object. The number of levels in the mutlilevel
			priority queue and the number of wait queues (and thus the number
			of locks) is predefined. The scheduler also comes with a list of
			timings which define the length of the runtime cycles, a cleanly
			allocated main thread, and a counter for the threads assigned.
	
		MING:: The r4eason why the scheduler should contain the main context is so the
			scheduler can be instantiated first before creating the first mypthread_t.
			Making the first mypthread requires a uclink, which would be the main context.
			Moreover, the scheduler should exist such that any thread can be scheduled
			at anytime. If the scheduler has a pthread which contains the main context,
			the scheduling new threads can always access the main thread if needed.
	*/
	int i, j, k;
	
	// Init signal hander
	struct sigaction sa;
	sa.sa_flags = SA_SIGINFO;
	sigemptyset(&sa.sa_mask);
	sa.sa_sigaction = handler;

	if (sigaction(SIGSEGV, &sa, NULL) == -1){
	    printf("Fatal error setting up signal handler\n");
	    exit(EXIT_FAILURE);    //explode!
	}



	//sched = malloc(sizeof(scheduler));
	//printf("sizeof(scheduler): %d\n", sizeof(scheduler));
	initialize_memory_management();

	sched = myallocate(sizeof(scheduler), __FILE__, __LINE__, SCHED_THREAD);//allocate memory in the reserved pages
	//printf("NUM_LEVELS * sizeof(queue): %d\n", NUM_LEVELS * sizeof(queue));
	sched->mlpq = myallocate(NUM_LEVELS * sizeof(queue), __FILE__, __LINE__, SCHED_THREAD); //allocate memory in the reserved pages

	//printf("NUM_LOCKS  * sizeof(queue): %d\n", NUM_LOCKS  * sizeof(queue));
	sched->wait = myallocate(NUM_LOCKS  * sizeof(queue), __FILE__, __LINE__, SCHED_THREAD); //allocate memory in the reserved pages

	for (i = 0; i < NUM_LEVELS; i++) {
		queue_init((sched->mlpq) + i);
	}
	for (j = 0; j < NUM_LOCKS; j++) {
		queue_init((sched->wait) + j);
	}
	for (k = 0; k < NUM_LEVELS; k++) {	// This is a temporary placeholder
		sched->prior_list[k] = TIME_QUANTUM * (k+1);	// for storing scheduling times, could be logrithm
	}
	
	sched->num_sched = 0;

	//sched->thr_main->thr_id = 0;
	//sched->thr_main->thr_state = NEW;
	//sched->thr_main->next_thr = sched->thr_main;
	sched->thr_cur = NULL;

	signal(SIGALRM, scheduler_handler);
	scheduler_handler();
}

void sched_addThread(mypthread_t * thr_node, int priority) {
	/*
		This function adds a node to a particular queue. This function is
			used to make schedule insertion easy. The number of scheduled
			threads is increased by 1. Threads that are added to the
			scheduler have their states changed.
	*/
	if (priority < 0 || priority >= NUM_LEVELS) {
		printf("The priority is not within the Multi-Level Priority Queue.\n");
	} else {
		// printf("Adding thread %d to level %d\n", thr_node->thr_id, priority);
		thr_node->thr_state = READY;
		thr_node->priority = priority; // keeptrack of the priority of the thread
		thr_node->time_runs = 0; // reset the running time of the thread
		enqueue(&(sched->mlpq[priority]), thr_node);
		sched->num_sched++;
	}
}

mypthread_t * sched_pickThread() {
	/*
		This function picks a thread to be scheduled from the scheduler,
			returning the one in the lowest index queue (which is the
			highest priority queue by convention).
	*/
	int i;
	for (i = 0; i < NUM_LEVELS; i++) {
		if (sched->mlpq[i].head != NULL) {
			mypthread_t * chosen = dequeue(&(sched->mlpq[i])); 
			// printf("Found a thread to schedule in level %d, thread id: %d\n", i, chosen->thr_id);
			sched->num_sched--;
			return chosen;
		}
	}
	printf("Nothing to schedule. return NULL;\n");

	// Print Everything 
	end = get_time_stamp();
    printf("Start timestamp: %ld\n", start);
    printf("End timestamp: %ld\n", end);
    printf("Total timestamp: %ld\n", end - start);

    
    //for (i = 0; i < NUM_THREADS; i++) {
		//printf("Response time of %d is : %ld\n", i, ( (&thr_list[i])->first_exe_tt  - (&thr_list[i])->start_tt) );
	//}



	// exit(EXIT_SUCCESS);
	return NULL;
}

void run_thread(mypthread_t * thr_node, void *(*f)(void *), void * arg) {
	/*
		This function takes a thread and executes the function (with parameters arg).
			Any return value will be stored in retval. If a state is newly terminated,
			then it will not be scheduled any longer, and the number of scheduled
			threads is reduced by 1. The scheduler will now point to the currently
			running thread
	*/
	thr_node->thr_state = RUNNING;
	sched->thr_cur = thr_node;
	thr_node->retval = f(arg);
	if (thr_node->thr_state != TERMINATED) {
		thr_node->thr_state = TERMINATED;
//		sched->num_sched--;
	}
	if(sched->thr_cur != NULL){
		sched->thr_cur->end_tt=get_time_stamp();
	}
	free_pages(thr_node->thr_id);
	scheduler_handler();
}

int my_pthread_create(mypthread_t * thread, mypthread_attr_t * attr, void *(*function)(void *), void * arg) {
	/*
		This function takes a thread that has already been malloc'd, gives the thread
			a stack, a successor, and creates a context that runs the function
			run_thread. run_thread is a function that 4handles the running of the
			function with the arg fed to make the context when scheduled to run
	*/
	//ucontext_t sched_ctx;

	if(getcontext(&(thread->ucp)) == -1) {
		printf("getcontext error\n");
		return -1;
	}

	//if(getcontext(&sched_ctx) == -1) {
		//printf("getcontext error\n");
		//return -1;
	//}
	
	//makecontext(&sched_ctx, (void *)run_thread, 0);

	thread->thr_id = ++thr_id; //assign thr_id first

	thread->ucp.uc_stack.ss_sp = initialize_new_thread(thread->thr_id, STACK_SIZE); //create both stack and heap and return 
																				    //heap address 
	thread->ucp.uc_stack.ss_size = STACK_SIZE;
	//myallocate(-1, __FILE__, __LINE__, CREATE_HEAP); //func_heap
	//thread->ucp.uc_link = &sched_ctx;//&(sched->thr_main->ucp);
	thread->start_tt = get_time_stamp();
	thread->first_exe_tt = 0;
	printf("Allocating the stack\n");
	makecontext(&(thread->ucp), (void *)run_thread, 3, thread, function, arg);
	printf("Made Context\n");
	sched_addThread(thread, 0);
	printf("Added Thread to the Scheduler.\n");
	return 0;
}

void my_pthread_yield() {
	/*
		This function swaps the current thread and runs another thread from the scheduler.
			The current function waits. 
	*/
	//mypthread_t * tmp;
	printf("Printing Scheduler Attributes\n");
	//tmp = sched->thr_cur;

	// call the scheduler
    sched->thr_cur->thr_state = YIELD;
	scheduler_handler();

	//degrade and put back to the running queue
	//int new_priority = (tmp->priority+1)>NUM_LEVELS ? NUM_LEVELS:(tmp->priority+1);
	//sched_addThread(tmp, new_priority);

	//sched->thr_cur = sched_pickThread();
	
	//sched->thr_cur->thr_state = RUNNING;
	//swapcontext(&(tmp->ucp), &(sched->thr_cur->ucp));
}

void my_pthread_exit(void * value_ptr) {
	/*
		This function forcibly shuts down the current thread. It does so by setting
			the current state to TERMINATED. This function first checks if the
			thread is already dead. When the thread is terminated, the thread
			perpetually yields.
	*/
	if (sched->thr_cur->thr_state == TERMINATED) {
		printf("This thread has already exited.\n");
	}
	sched->thr_cur->thr_state = TERMINATED;
	sched->thr_cur->retval = value_ptr;
	sched->thr_cur->end_tt=get_time_stamp();

	free_pages(sched->thr_cur->thr_id);
	// call the scheduler
	scheduler_handler();

//	sched->num_sched--;
	//my_pthread_yield();
}

int my_pthread_join(mypthread_t * thread, void ** value_ptr) {
	/*
		This function takes in the a thread pointer and has the current thread to the
			argument thread. Any return value
	*/
	while (thread->thr_state != TERMINATED) {
		my_pthread_yield();
	}
	thread->retval = value_ptr;
}

int my_pthread_mutex_init(my_pthread_mutex_t *mutex, const pthread_mutexattr_t *mutexattr){
    int result = 0;

    if(mutex == NULL)
        return EINVAL;

    mutex->flag  = 0;
    mutex->guard = 0;
    mutex->wait = malloc(sizeof(queue));
    queue_init(mutex->wait);

    return result;
}

int my_pthread_mutex_lock(my_pthread_mutex_t *mutex) {
    /*while (__sync_lock_test_and_set(mutex->guard, 1) == 1)
        ; //acquire guard lock by spinning
    if (mutex->flag == 0) {
        mutex->flag = 1; //
        mutex->guard = 0; 
    }else{
        //queue_add(m->q,
        m->guard = 0;
        setPark();
    }
    //yield()*/
    /*while(mutex->flag == 1){
        //my_pthread_yield();
    }*/
    while (__sync_lock_test_and_set(&(mutex->flag), 1) == 1){
    	//my_pthread_yield();
    	sched->thr_cur->thr_state = WAITING;
    	printf("The thread is waiting for a mutex, put it to the waiting list\n");
    	enqueue(mutex->wait, sched->thr_cur);
    	scheduler_handler();
    }
}

int my_pthread_mutex_unlock(my_pthread_mutex_t *mutex){
    /*while (TestAndSet(&m->guard, 1) == 1)
        ; //acquire guard lock by spinning
    if (queue_empty(m->q))
        m->flag = 0; // let go of lock; no one wants it
    else
gettid());
        unpark(queue_remove(m->q)); // hold lock (for next thread!)
    m->guard = 0;*/
    mypthread_t * chosen;
    if (mutex->wait->head != NULL) {
		chosen = dequeue(mutex->wait); 
		printf("Mutex is available, select one thread from the waiting list and put it back to the running queue\n");
		sched_addThread(chosen, chosen->priority);
	}
    mutex->flag = 0;
}

int my_pthread_mutex_destroy(my_pthread_mutex_t *mutex){
    int result = 0;

    if(mutex == NULL)
        return EINVAL;
    if(mutex->flag != 0)
        return EBUSY;
    return result;
}

#if TESTING

void f0(void) {
	printf("Function f0 start\n");
	//char *s="That's good news";   
    int i=0;   
    FILE *fp;  
    fp=fopen("test.dat", "w"); 

    while(i<104857600){
    	fputs("a",fp);
    	i++;
    }

    fflush(fp);
    fclose(fp); 
	printf("Function f0 done\n");
}

void f1(void) {

	int j;

	for(j = 0; j < 10; j++) {
		printf("Number: %d\n", j);
	}
	printf("Function f1 Done\n");
}

void f2(void) {

	int j;

	for(j = 100; j < 125; j++) {
		printf("Number: %d\n", j);
		if (j == 115) {
			my_pthread_exit(NULL);
		}
	}
	printf("Function f2 done\n");
}

void mutexTestOne() {
	//char *s="That's good news";   
    int i=0;
    int localCopy;   
    FILE *fp;  
    fp=fopen("test1.dat", "w"); 

    my_pthread_mutex_lock(mutex1);
    localCopy = sharedVariable;
    printf("mutexTestOne read the sharedVariable, the value is %d\n", localCopy);
    while(i<104857600){
    	fputs("a",fp);
    	i++;
    }
    fflush(fp);
    localCopy = localCopy+10;
    sharedVariable = localCopy;
    printf("mutexTestOne update the sharedVariable, the value now is %d\n", sharedVariable);
    my_pthread_mutex_unlock(mutex1);

    fclose(fp); 
}

void mutexTestTwo() {
	//char *s="That's good news";   
    int i=0;  
    int localCopy;    
    FILE *fp;  
    fp=fopen("test2.dat", "w"); 

    my_pthread_mutex_lock(mutex1);
    localCopy = sharedVariable;
    printf("mutexTestTwo read the sharedVariable, the value is %d\n", localCopy);
    while(i<104857600){
    	fputs("a",fp);
    	i++;
    }
    fflush(fp);
    localCopy = localCopy-5;
    sharedVariable = localCopy;
    printf("mutexTestTwo update the sharedVariable, the value now is %d\n", sharedVariable);
    my_pthread_mutex_unlock(mutex1);

    fclose(fp); 
}

void noMutexTestOne() {
	//char *s="That's good news";   
    int i=0;
    int localCopy;   
    FILE *fp;  
    fp=fopen("test3.dat", "w"); 

    localCopy = sharedVariable1;
    printf("noMutexTestOne read the sharedVariable, the value is %d\n", localCopy);
    while(i<104857600){
    	fputs("a",fp);
    	i++;
    }
    fflush(fp);
    localCopy = localCopy+10;
    sharedVariable1 = localCopy;
    printf("noMutexTestOne update the sharedVariable, the value now is %d\n", sharedVariable1);

    fclose(fp); 
}

void noMutexTestTwo() {
	//char *s="That's good news";   
    int i=0;  
    int localCopy;    
    FILE *fp;  
    fp=fopen("test4.dat", "w"); 

    localCopy = sharedVariable1;
    printf("noMutexTestTwo read the sharedVariable, the value is %d\n", localCopy);
    while(i<104857600){
    	fputs("a",fp);
    	i++;
    }
    fflush(fp);
    localCopy = localCopy-5;
    sharedVariable1 = localCopy;
    printf("noMutexTestTwo update the sharedVariable, the value now is %d\n", sharedVariable1);

    fclose(fp); 
}

void test(int cap) {

	int i, j;
	int test;
	test = 1;
	char* test_char = malloc(100);
	int* test_int = malloc(100*4);
	test_char[0]='a';
	test_int[0]=1;
	for (i = 1; i < cap; i++) {
		for (j = 1; j < i; j++) {
			if (i % j == 0) {
				continue;
			}
			if (j == i - 1) {
				test = i;
			}
		}
	}

	printf("test_char[0]: %c\n", test_char[0]);
	printf("test_int[0]: %d\n", test_int[0]);
	printf("Final Test: %d\n", test);
	free(test_char);
	free(test_int);
}

void test_normal_bench(){
	printf("Starting Testing\n");


	/*printf("Allocating space for the thread array\n");
	thr_list = malloc(NUM_THREADS * sizeof(mypthread_t));
	printf("Initializing the Scheduler\n");
	
	printf("Initializing the Mutex\n");
	mutex1 = malloc(sizeof(my_pthread_mutex_t));

	my_pthread_mutex_init(mutex1,NULL);

	printf("Initializing thread\n");

	// NUM_THREADS = 50;*/
	time_t t;
	long int i;
	long int base = 100;
	long int random[NUM_THREADS];
	long int random_sec[NUM_THREADS];

	start = get_time_stamp();
	sched_init();
	
	srand((unsigned) time(&t));

	for (i = 0; i < NUM_THREADS; i++) {
		random[i] = rand() % 1000 * base;
		printf("Random Number %li\n", random[i]);
	}	


	mypthread_attr_t * thread_attr = NULL;

	//thr_list = myallocate(sizeof(mypthread_t), __FILE__, __LINE__, SCHED_THREAD);
	thr_list = myallocate(sizeof(mypthread_t), __FILE__, __LINE__, SCHED_THREAD);
	my_pthread_create(&thr_list[0], thread_attr, (void *(*)(void *))test, (void *)random[0]);
	
	/*for (i = 0; i < NUM_THREADS-4; i++) {
		if (my_pthread_create(&thr_list[i], thread_attr, (void *(*)(void *))test, (void *)random[i]) != 0) {
			printf("Error Creating Thread %li\n", i);
		}
	}
	if (my_pthread_create(&thr_list[NUM_THREADS-4], thread_attr, (void *(*)(void *))mutexTestOne, NULL)) {
			printf("Error Creating Thread %li\n", NUM_THREADS-4);
	}
	if (my_pthread_create(&thr_list[NUM_THREADS-3], thread_attr, (void *(*)(void *))mutexTestTwo, NULL)) {
			printf("Error Creating Thread %li\n", NUM_THREADS-3);
	}
	if (my_pthread_create(&thr_list[NUM_THREADS-2], thread_attr, (void *(*)(void *))noMutexTestOne, NULL)) {
			printf("Error Creating Thread %li\n", NUM_THREADS-2);
	}
	if (my_pthread_create(&thr_list[NUM_THREADS-1], thread_attr, (void *(*)(void *))noMutexTestTwo, NULL)) {
			printf("Error Creating Thread %li\n", NUM_THREADS-1);
	}*/
}


int main() {

	//	Code to test queue class
	test_normal_bench();
	while(1);

	return 0;
}

#endif
