#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include <unistd.h>
#include <dirent.h>
#include <limits.h>
#include <assert.h>
#include <time.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <fcntl.h>
#include <signal.h>
#include <sys/mman.h>

int num_found = 0; //will store the total amount of files found by all threads
int active_searching = 0; //will store cuuent number of threads doing active searching on a popped directory
int num_threads = 0; //will store the total number of threads
pthread_mutex_t count_mutex; // will be used to increment global int total_found
pthread_mutex_t queue_mutex; // will be used when threads use queue
pthread_mutex_t active_mutex; // will be used to ++ or -- global int active_searching
pthread_cond_t	c_empty; //condition variable - will be signaled when new directory is pushed to queue
pthread_t* threads = NULL; // will store threads id's when main calls create
void ** retvals; // will store threads return values
pthread_attr_t attr; //will be used to make threads joinable
int error_happened = 0; //flag which will tell if any thread exited with an error

//SIGINT action handler
void intHandler(int w) {
    int i=0;
    for(i=0;i<num_threads;i++){
    	pthread_cancel(threads[i]);
    }
    printf("Search stopped, found %d files.\n", num_found);
    exit(0);
}

// functions that uses mutexes to increment / decrement global integers.
int increment_total_found (void){
	int rc;
	rc = pthread_mutex_lock(&count_mutex); //lock
	if (0!=rc){
		perror("error: locking mutex failed\n");
		return -1;
	}

	++ num_found; //increment

	rc = pthread_mutex_unlock(&count_mutex); //unlock
	if (0!=rc){
		perror("error: unlocking mutex failed\n");
		return -1;
	}
	return 0;
}

int increment_active_searching (void){
	int rc;
	rc = pthread_mutex_lock(&active_mutex); //lock
	if (0!=rc){
		perror("error: locking mutex failed\n");
		return -1;
	}

	++ active_searching; //increment

	rc = pthread_mutex_unlock(&active_mutex); //unlock
	if (0!=rc){
		perror("error: unlocking mutex failed\n");
		return -1;
	}
	return 0;
}

int decrement_active_searching (void){
	int rc;
	rc = pthread_mutex_lock(&active_mutex); //lock
	if (0!=rc){
		perror("error: locking mutex failed\n");
		return -1;
	}

	-- active_searching; //increment

	rc = pthread_mutex_unlock(&active_mutex); //unlock
	if (0!=rc){
		perror("error: unlocking mutex failed\n");
		return -1;
	}
	return 0;
}

//construct queue
typedef struct node{
	char path[PATH_MAX+1];
	struct node * next;
}node;

static node * queue_head = NULL;

int pop(char *returned){ // will work on the global queue_head: pop a directory from queue into returned
	int rc;
	//wait for queue to not be empty
	while(queue_head==NULL){

		rc = pthread_cond_wait(&(c_empty), &(queue_mutex)); //if queue is empty, wait for signal
		if (0!=rc) return -1;
	}
	strcpy(returned,queue_head->path);
	node *tmp = queue_head;
	queue_head = queue_head->next;
	free(tmp);
	return 0;
}

int push(char * path){ // will work on the global queue_head: push a directory "path" to queue
	int rc;
	node * new = malloc(sizeof(node));
	if(new==NULL) return-1;
	strcpy(new->path,path);
	// case first in queue
	if(queue_head==NULL){
		queue_head = new;
		new->next = NULL;
		return 0;
	}
	// case not first in queue
	new->next = queue_head;
	queue_head = new;
	rc = pthread_cond_signal(&c_empty); // signal that the queue is not empty
	if (0!=rc) return -1;
	return 0;
}

void* thread_job (void* t){ // this is the function given to every thread
	int rc;
	struct stat path_stat;
	char * is_substring_found = NULL;
	DIR *dir;
	struct dirent *dp;
	char tested_path [PATH_MAX+1];
	char full_path [PATH_MAX+1];

	while(1){
		//check if threads finished searching togethger. if so - free things and exit the program
		if(queue_head==NULL && active_searching==0){
			pthread_mutex_destroy(&count_mutex);
			pthread_mutex_destroy(&queue_mutex);
			pthread_mutex_destroy(&active_mutex);
			pthread_attr_destroy(&attr);
			pthread_cond_destroy(&c_empty);
			//release dinamically allocated arrays
			free(threads);
			free(retvals);

			printf("Done searching, found %d files\n", num_found);
			if(error_happened) exit(1); // meaning at least one thread exited due to an error
			exit(0); // no errors had happened
		}
		//if we got here, it means there might be still job to be done
		rc = pthread_mutex_lock(&queue_mutex); // Acquire lock before pop
		if (0!=rc){
			error_happened = 1;
			perror("error: locking queue mutex failed\n");
			pthread_exit((void*)1);
		}
		rc = pop(tested_path);// dequeue one directory from queue. if queue empty - sleep until not empty (implemented in pop)
		if(rc<0){
			error_happened = 1;
			perror("error: popping queue failed\n");
			pthread_exit((void*)1);
		}
		rc = pthread_mutex_unlock(&queue_mutex); // release lock
		if(rc){
			error_happened = 1;
			perror("error: unlocking queue mutex failed\n");
			pthread_exit((void*)1);
		}
		rc = increment_active_searching(); // thread is currently searching
		if(rc<0){
			error_happened = 1;
			pthread_exit((void*)1);
		}
		dir = opendir(tested_path);

		while ((dp=readdir(dir)) != NULL) { // iterate through files and folders in popped path
			if ( !strcmp(dp->d_name, ".") || !strcmp(dp->d_name, "..") )
			{ // do nothing for directories "." and ".."
			}
			else {
				strcpy(full_path,tested_path);
				strcat(full_path,"/");
				strcat(full_path,dp->d_name); //make a full path for tested file/directory
				stat(full_path, &path_stat);
				if(S_ISDIR(path_stat.st_mode)){ // check if file is folder - if so - push to queue
					rc = pthread_mutex_lock(&queue_mutex);
					if(rc){
						perror("error: locking queue mutex failed\n");
						error_happened = 1;
						pthread_exit((void*)1);
					}
					rc = push(full_path); // push discovered directory to queue
					if(rc<0){
						perror("error: pushing directory to queue failed\n");
						error_happened = 1;
						pthread_exit((void*)1);
					}
					rc = pthread_mutex_unlock(&queue_mutex);
					if(rc){
						perror("error: unlocking queue mutex failed\n");
						error_happened = 1;
						pthread_exit((void*)1);
					}
				}
				else{ // else - file is not a folder - check if term is in the file's name
					is_substring_found = strstr(dp->d_name, (char*)t);
					if (is_substring_found){ // found path with the name
						pthread_setcancelstate(PTHREAD_CANCEL_DISABLE, NULL); // cancel gracefully - don't exit just yet
						rc = increment_total_found();
						if(rc<0){
							error_happened = 1;
							pthread_exit((void*)1);
						}
						// now print to stdout the newly found full path
						printf("%s\n", full_path);
						pthread_setcancelstate(PTHREAD_CANCEL_ENABLE, NULL); // cancel gracefully
					}
				}
			}
		} // end of iteration through given directory
		rc = decrement_active_searching(); // mark as not actively searching
		if(rc<0){
			error_happened = 1;
			pthread_exit((void*)1);
		}
		closedir(dir);
	} // when done, repeat  - go back to while
	pthread_exit((void*)0);
}
//-------------------------------------------------------------------------------//

int main(int argc, char **argv ){
	int i = 0, rc;
	char * gpath = argv[1]; // given path
	char * term = argv[2]; // term that will be searched
	num_threads = atoi(argv[3]); //number of threads

	// dealing with SIGINT
	struct sigaction act;
	act.sa_handler = intHandler;
	sigaction(SIGINT, &act, NULL);

	//Initialize mutex
	rc = pthread_mutex_init( &queue_mutex, NULL );
	if(rc) exit(1);
	rc = pthread_mutex_init( &count_mutex, NULL );
	if(rc) exit(1);
	rc = pthread_mutex_init( &active_mutex, NULL );
	if(rc) exit(1);

	// Initialize attr for threads, to ensure they will be joinable
	pthread_attr_init(&attr);
	pthread_attr_setdetachstate(&attr, PTHREAD_CREATE_JOINABLE);
	//Initialize condition variable
	pthread_cond_init(&c_empty,NULL);

	// Dynamically allocate array of thread pointers in the given size
	threads = malloc((num_threads)*sizeof(pthread_t));
	if(threads == NULL) exit(1);

	// Dynamically allocate return values array for threads
	retvals = malloc((num_threads)*sizeof(void*));
	if(retvals == NULL) exit(1);

	// put search root in queue
	  rc = push(gpath);
	  if (rc<0) exit(1);

	// create num_threads threads.

	for(i=0; i<num_threads ; i++){
		rc = pthread_create(&threads[i], &attr, thread_job, (void *)term);
		if (rc) exit(1);
	}

	// main thread waits for all the threads it created
	for(i=0; i<num_threads; i++)
	{
		rc = pthread_join(threads[i], &retvals[i]);
		if (rc) exit(1);
	}

	// main thread destroys mutex
	pthread_mutex_destroy(&count_mutex);
	pthread_mutex_destroy(&queue_mutex);
	pthread_mutex_destroy(&active_mutex);

	pthread_attr_destroy(&attr);
	pthread_cond_destroy(&c_empty);

	//release dinamically allocated arrays
	free(threads);
	free(retvals);

	// exit main thread - case all threads exited with error:
	printf("Done searching, found %d files\n", num_found);
	exit(1);
}
