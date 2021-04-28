#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#ifndef UBQSIZE
#define UBQSIZE 10
#endif

typedef struct {
	int dataLength;
	int activeThreads;
	char ** data;
	unsigned count;
	unsigned head;
	int open;
	pthread_mutex_t lock;
	pthread_cond_t read_ready;
	pthread_cond_t write_ready;
} unbounded_queue_t;

int init_unbounded(unbounded_queue_t *Q)
{
	Q->activeThreads = 0;
	Q->dataLength = UBQSIZE;
	Q->data = malloc(sizeof(char*) * Q->dataLength);
	Q->count = 0;
	Q->head = 0;
	Q->open = 1;
	pthread_mutex_init(&Q->lock, NULL);
	pthread_cond_init(&Q->read_ready, NULL);
	pthread_cond_init(&Q->write_ready, NULL);
	
	return 0;
}

int destroy_unbounded(unbounded_queue_t *Q)
{
	free(Q->data);
	pthread_mutex_destroy(&Q->lock);
	pthread_cond_destroy(&Q->read_ready);
	pthread_cond_destroy(&Q->write_ready);

	return 0;
}


// add item to end of queue
// if the queue is full, block until space becomes available
int enqueue_unbounded(unbounded_queue_t *Q, char * item)
{
	pthread_mutex_lock(&Q->lock);

	if (!Q->open) {
		pthread_mutex_unlock(&Q->lock);
		return -1;
	}

	//check to see if the queue needs to be resized
	if(Q->count == Q->dataLength){
		//resize the queue array
		char **p = realloc(Q->data, sizeof(char*) * Q->dataLength * 2);

		//reorganize the queue if needed.
		//this preserves the first in, first out nature of the queue
		for(int i = 0; i < Q->head; i++){
			p[i + Q->dataLength] = p[i];
		}

		Q->dataLength = 2 * Q->dataLength;
		Q->data = p;
	}
	
	unsigned i = Q->head + Q->count;
	if (i >= Q->dataLength) i -= Q->dataLength;

	Q->data[i] = malloc(1 + strlen(item));
	strcpy(Q->data[i], item);
	++Q->count;
	
	pthread_cond_signal(&Q->read_ready);
	
	pthread_mutex_unlock(&Q->lock);
	
	return 0;
}

//must deallocate the word on the client side!!!!
int dequeue_unbounded(unbounded_queue_t *Q, char ** item)
{
	pthread_mutex_lock(&Q->lock);
	
	if (Q->count == 0) {
		Q->activeThreads--;
		if(Q->activeThreads == 0){
			pthread_mutex_unlock(&Q->lock);
			pthread_cond_broadcast(&Q->read_ready);
			return 1;
		}
		while(Q->count == 0 && Q->activeThreads != 0){
			pthread_cond_wait(&Q->read_ready, &Q->lock);
		}
		if(Q->count == 0){
			pthread_mutex_unlock(&Q->lock);
			return 1;
		}
		Q->activeThreads++;
	}
	
	*item = Q->data[Q->head];
	--Q->count;
	++Q->head;
	if (Q->head == Q->dataLength) Q->head = 0;
	
	pthread_cond_signal(&Q->write_ready);
	
	pthread_mutex_unlock(&Q->lock);
	
	return 0;
}

int qclose_unbounded(unbounded_queue_t *Q)
{
	pthread_mutex_lock(&Q->lock);
	Q->open = 0;
	pthread_cond_broadcast(&Q->read_ready);
	pthread_cond_broadcast(&Q->write_ready);
	pthread_mutex_unlock(&Q->lock);	

	return 0;
}