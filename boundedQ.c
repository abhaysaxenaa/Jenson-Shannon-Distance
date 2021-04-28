#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <pthread.h>

#ifndef BQSIZE
#define BQSIZE 10
#endif

typedef struct {
	char * data[BQSIZE];
	unsigned count;
	unsigned head;
	int open;
	pthread_mutex_t lock;
	pthread_cond_t read_ready;
	pthread_cond_t write_ready;
} bounded_queue_t;

int init_bounded(bounded_queue_t *Q)
{
	Q->count = 0;
	Q->head = 0;
	Q->open = 1;
	pthread_mutex_init(&Q->lock, NULL);
	pthread_cond_init(&Q->read_ready, NULL);
	pthread_cond_init(&Q->write_ready, NULL);
	
	return 0;
}

int destroy_bounded(bounded_queue_t *Q)
{
	pthread_mutex_destroy(&Q->lock);
	pthread_cond_destroy(&Q->read_ready);
	pthread_cond_destroy(&Q->write_ready);

	return 0;
}

// add item to end of queue
// if the queue is full, block until space becomes available
int enqueue(bounded_queue_t *Q, char * item)
{

	pthread_mutex_lock(&Q->lock);
	
	while ( (Q->count == BQSIZE) && Q->open) {
		pthread_cond_wait(&Q->write_ready, &Q->lock);
	}

	if (!Q->open) {
		pthread_mutex_unlock(&Q->lock);
		return -1;
	}

	unsigned i = Q->head + Q->count;
	if (i >= BQSIZE) i -= BQSIZE;

	Q->data[i] = malloc(1 + strlen(item));
	strcpy(Q->data[i], item);
	free(item);
	++Q->count;

	pthread_cond_signal(&Q->read_ready);
	
	pthread_mutex_unlock(&Q->lock);

	return 0;
}


int dequeue(bounded_queue_t *Q, char ** item)
{
	pthread_mutex_lock(&Q->lock);
	while (Q->count == 0 && Q->open) {
		pthread_cond_wait(&Q->read_ready, &Q->lock);
	}
	if (Q->count == 0) {
		pthread_mutex_unlock(&Q->lock);
		return -1;
	}

	*item = malloc(strlen(Q->data[Q->head]) + 1);
	strcpy(*item, Q->data[Q->head]);
	free(Q->data[Q->head]);
	--Q->count;
	++Q->head;
	if (Q->head == BQSIZE) Q->head = 0;
	
	pthread_cond_signal(&Q->write_ready);
	
	pthread_mutex_unlock(&Q->lock);
	
	return 0;
}

int qclose(bounded_queue_t *Q)
{
	pthread_mutex_lock(&Q->lock);
	Q->open = 0;
	pthread_cond_broadcast(&Q->read_ready);
	pthread_cond_broadcast(&Q->write_ready);
	pthread_mutex_unlock(&Q->lock);	

	return 0;
}