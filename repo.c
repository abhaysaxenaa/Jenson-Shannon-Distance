#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <ctype.h>
#include <dirent.h>
#include <string.h>
#include <math.h>
#include <sys/types.h>
#include <pthread.h>
#include <errno.h>
#include "strbuf.c"

#ifndef AQSIZE
#define AQSIZE 20
#endif
#define SIZE 8

typedef struct List{
	char* word;
	struct List *next;
	int frequency;
	double WFD;
}List;

typedef struct {
    char *filepath;
    List *list;
} FileAndList;

typedef struct {
    FileAndList **fal;
    int size;
    int nextIndex;
    pthread_mutex_t arrayLock;
} repository;

//---------------------------------------------------------------------
//Linked list basic functions
//---------------------------------------------------------------------

/*
 * How to create a new list:
 * List * myList = NULL;
 * 
 * Initializing a list: 
 * this will be done automatically the first time insert is called.
 * 
 * inserting a value into a list:
 * insert(&myList, some_string);
 * 
 * computing the WFG: note that count is not a pointer here
 * computeWFD(myList, count);
 * 
 * printing the list:
 * printList(myList);
 * 
 * Deallocating a list:
 * destroy_list(&myList);
 */ 

int destroy_list(List **node){
   List* currentNode = *node;
   List* nextNode;
 
   while (currentNode != NULL){
       nextNode = currentNode->next;
       free(currentNode->word);
       free(currentNode);
       currentNode = nextNode;
   }
   *node = NULL;
    return 0;
}

void printList(List *list){
    List *temp = list;
    while (temp != NULL){
        printf("%s: %d %f\n", temp->word, temp->frequency, temp->WFD);
        temp = temp->next;
    }
}

void insert(List **head_ref, char * new_word){

    //check to see if the list is empty
    if (*head_ref == NULL){
        //reassign the list to an initialized node
        List *new_node = malloc(sizeof(List));
        new_node->word = malloc(strlen(new_word) + 1);
        strcpy(new_node->word, new_word);
        new_node->next = NULL;
        new_node->frequency = 1;
        new_node->WFD = 0;
        *head_ref = new_node;
        return;
    } 

    int word_not_found = 1;
    int list_not_finished = 1;

    List *last_node = *head_ref;

	while (word_not_found && list_not_finished){

        if(last_node->word == NULL){
            fprintf(stderr, "last_node->word = NULL\n");
        } else if(new_word == NULL){
            fprintf(stderr, "new_word = NULL\n");
        }     

        if (strcmp(last_node->word, new_word) == 0){
            //we found the word in the list: increment the count and trip the flag
            last_node->frequency++;
            word_not_found = 0;

        } else {

            if(last_node->next == NULL){
                //set flag to false so that the loop stops
                list_not_finished = 0;

                //we've got a new word so add it to the list
                List *new_node = malloc(sizeof(List));
                new_node->word = malloc(strlen(new_word) + 1);
                strcpy(new_node->word, new_word);
                new_node->next = NULL;
                new_node->frequency = 1;
                new_node->WFD = 0;
                last_node->next = new_node;

            } else {
                //reassign the node
                last_node = last_node->next;
            }

        }

    }

}

void computeWFD(List *list, int word_count){
    List *temp = list;
    while (temp != NULL){
        temp->WFD = (double) temp->frequency / word_count;
        temp = temp->next;
    }
    free(temp);
}

double checkWFD(List *list){
    double sum = 0;
    List *temp = list;
    while(temp != NULL){
        sum += temp->WFD;
        temp = temp->next;
    }
    free(temp);
    return sum;
}

void bubbleSort(List *sortlist){

    if(sortlist != NULL){
        
        List *temp;
        List *ptr;

        for (temp = sortlist; temp != NULL; temp = temp->next){
            for (ptr = temp->next; ptr != NULL; ptr = ptr->next){
                if (strcmp(temp->word, ptr->word) > 0){
                    char *temp_word = temp->word;
                    int temp_frequency = temp->frequency;
                    double temp_WFD = temp->WFD;
                    temp->word = ptr->word;
                    temp->frequency = ptr->frequency;
                    temp->WFD = ptr->WFD;
                    ptr->word = temp_word;
                    ptr->frequency = temp_frequency;
                    ptr->WFD = temp_WFD;
                }
            }
        }

        free(ptr);
        free(temp);

    }

}

//---------------------------------------------------------------------
// WFD repository basic use functions
//---------------------------------------------------------------------

/**
 * HOW TO: repositorys
 * 
 * declaration              repository myRepo;
 * 
 * initialization           init_repository(&myRepo, numFiles);
 * 
 * appending from thread    append_repository(argsPointer, &currentList);
 * 
 * reading values           access the array directly fromt he main thread
 * 
 * deallocation             destroy_repository(&myRepo);
 * 
 */ 
int init_repository(repository * repos, int startSize){
    repos->fal = malloc(sizeof(FileAndList *) * startSize);
    if(repos->fal == NULL){
        perror("repo init, malloc failed!");
        return 1;
    } 
    repos->size = startSize;
    repos->nextIndex = 0;
    if(pthread_mutex_init(&repos->arrayLock, NULL)){
        perror("lock init failed");
        return 1;

    }
    return 0;
}

int destroy_repository(repository * repos){
    
    for(int i = 0; i < repos->nextIndex; i++){
        destroy_list(&repos->fal[i]->list);
        free(&repos->fal[i]->filepath);
    }
    
    free(repos->fal); 
    pthread_mutex_destroy(&repos->arrayLock);
    return 0;
}

int append_repository(repository * repos, FileAndList *fal){

    //aquire the lock
    int err = pthread_mutex_lock(&repos->arrayLock);
    if(err){
        errno = err;
        perror("lock failure in append");
        abort();
    }

    //reallocate if the array is full
    if(repos->nextIndex == repos->size){
        repos->size = 2 * repos->size;
        FileAndList** temp = realloc(repos->fal, sizeof(FileAndList *) * repos->size);
        if (!temp) return 1;
        repos->fal = temp;
    }

    repos->fal[repos->nextIndex] = fal;
    repos->nextIndex++;

    pthread_mutex_unlock(&repos->arrayLock);

    return 0;

}

//---------------------------------------------------------------------
// Linked list application functions
//---------------------------------------------------------------------

void fillList(List **listOne, int fd){

    char * buf = malloc(SIZE);
    strbuf_t sb;
    strbuf_init(&sb, SIZE);
    int word_count = 0;

    //read from file
    int bytes_read = read(fd, buf, SIZE);
    while (bytes_read > 0){

        for (int i = 0; i < bytes_read; i++){

            if (!isspace(buf[i])){

                //Uppercase/Lowercase alphabet
                if (isalpha(buf[i])){
                    strbuf_append(&sb, tolower(buf[i]));
                }
                //Digits
                if (isdigit(buf[i])){
                    strbuf_append(&sb, buf[i]);
                }
                //Hyphen
                if (buf[i]==45){
                    strbuf_append(&sb, buf[i]);
                }

            } else {

                //Insert only if sb is not empty
                if (sb.used != 0){
                    insert(listOne, sb.data);
                    //clear the strbuf
                    sb.used = 0;
                    sb.data[0] = '\0';
                    word_count++;
                }

                continue;

            }
        }

        bytes_read = read(fd, buf, SIZE);

    }

    //Insert only if sb is not empty
    if (sb.used != 0){
        insert(listOne, sb.data);
        //clear the strbuf
        sb.used = 0;
        sb.data[0] = '\0';
        word_count++;
    }

    //compute WFD
    computeWFD(*listOne, word_count);

    //deallocate local resources
    free(buf);
    strbuf_destroy(&sb);

    return;

}

List *searchList(List *list, char *word){
    List *temp = list;
    while (temp != NULL){
        if (strcmp(temp->word, word) == 0){
            return temp;
        }
        temp = temp->next;
    }
    free(temp);
    return NULL;
}

int countLength(List *list){
    List *temp = list;
    int counter = 0;
    while (temp != NULL){
        counter++;
        temp = temp->next;
    }
    free(temp);
    return counter;
}

List *mergeLists(List *listOne, List *listTwo){
    //NOTE: Check for updating the frequency properly, in order to have WFD = 1
    List *result = NULL;
    List *temp1 = listOne;
    List *temp2 = listTwo;

    while (temp1 != NULL && temp2 != NULL){
        //If both lists have the same word
        if (strcmp(temp1->word, temp2->word) == 0){
            insert(&result, temp1->word);
            List *newTemp = searchList(result, temp1->word);
            newTemp->WFD = (temp1->WFD+temp2->WFD)/2;
            temp1 = temp1->next;
            temp2 = temp2->next;
        } else if (strcmp(temp1->word, temp2->word) > 0){
            //If word in temp2 is before temp1 lexographically
            insert(&result, temp2->word);
            List *newTemp = searchList(result, temp2->word);
            newTemp->WFD = temp2->WFD/2;
            temp2 = temp2->next;
        } else if (strcmp(temp1->word, temp2->word) < 0){
            //If word in temp1 is before temp2 lexographically
            insert(&result, temp1->word);
            List *newTemp = searchList(result, temp1->word);
            newTemp->WFD = temp1->WFD/2;
            temp1 = temp1->next;
        }

    }

    //If temp1 is not iterated through completely
    if (temp1 != NULL){
        while (temp1 != NULL){
            insert(&result, temp1->word);
            List *newTemp = searchList(result, temp1->word);
            newTemp->WFD = temp1->WFD/2;
            temp1 = temp1->next;
        }
    }

    //If temp2 is not iterated through completely
    if (temp2 != NULL){
        while (temp2 != NULL){
            insert(&result, temp2->word);
            List *newTemp = searchList(result, temp2->word);
            newTemp->WFD = temp2->WFD/2;
            temp2 = temp2->next;
        }
    }

    //Calculate new WFD for the third list
    // int counter = countLength(result);
    // computeWFD(result, counter);

    bubbleSort(result);

    return result;
}

double calculateJSD(List* listOne, List* listTwo, int *countAddress){

    if(listOne == NULL && listTwo == NULL){
        *countAddress = 0;
        return 0;
    } else if(listOne == NULL){
        *countAddress = countLength(listTwo);
        return sqrt(0.5);
    } else if(listTwo == NULL){
        *countAddress = countLength(listOne);
        return sqrt(0.5);
    }

    //bubbleSort(listOne);
    //bubbleSort(listTwo);

    List *result = mergeLists(listOne, listTwo);
    List *temp1 = listOne;
    List *temp2 = listTwo;
    List *temp3 = result;
    double KLD1 = 0.0;
    double KLD2 = 0.0;
    double JSD = 0.0;

    while (temp1 != NULL && temp3 != NULL){
        //Calculate KLD1
        if (strcmp(temp1->word, temp3->word) == 0){
            KLD1 += (temp1->WFD * log2(temp1->WFD / temp3->WFD));
            temp1 = temp1->next;
            temp3 = temp3->next;
        } else if (strcmp(temp1->word, temp3->word) > 0){
            temp3 = temp3->next;
        } else {
            temp1 = temp1->next;
        }
    }

    //Reset temp3 for KLD2 calculation
    temp3 = result;

    while (temp2 != NULL && temp3 != NULL){
        //Calculate KLD2
        if (strcmp(temp2->word, temp3->word) == 0){
            KLD2 += (temp2->WFD * log2(temp2->WFD / temp3->WFD));
            temp2 = temp2->next;
            temp3 = temp3->next;
        } else if (strcmp(temp2->word, temp3->word) > 0){
            temp3 = temp3->next;
        } else {
            temp2 = temp2->next;
        }
    }

    JSD = sqrt((0.5*KLD1) + (0.5*KLD2));

    *countAddress = countLength(listOne) + countLength(listTwo);
 
    destroy_list(&result);

    return JSD;
}

//---------------------------------------------------------------------
// Analysis Queue Functions
//---------------------------------------------------------------------

typedef struct {
    FileAndList** falOne;
    FileAndList** falTwo;
    unsigned count;
    unsigned head;
    int open;
    int fsIndex[AQSIZE];
    pthread_mutex_t lock;
    pthread_cond_t read_ready;
    pthread_cond_t write_ready;
} analysis_queue_t;

int init_analysis(analysis_queue_t *q){
    q->falOne = malloc(sizeof(FileAndList*) * AQSIZE);
    q->falTwo = malloc(sizeof(FileAndList*) * AQSIZE);
    q->count = 0;
    q->head = 0;
    q->open = 1;
    if(pthread_mutex_init(&q->lock, NULL)){
        perror("lock init error");
        abort();
    }
    pthread_cond_init(&q->read_ready, NULL);
    pthread_cond_init(&q->write_ready, NULL);
    return 0;
}

int destory_analysis(analysis_queue_t *q){
    free(q->falOne);
    free(q->falTwo);
    pthread_mutex_destroy(&q->lock);
    pthread_cond_destroy(&q->read_ready);
    pthread_cond_destroy(&q->write_ready);
    return 0;
}

int enqueue_analysis(analysis_queue_t *q, FileAndList* l1, FileAndList* l2, int writeIndex){

    if(pthread_mutex_lock(&q->lock)){
        perror("lock error");
        abort();
    }

    while( (q->count == AQSIZE) && (q->open) ){
        pthread_cond_wait(&q->write_ready, &q->lock);
    }

    if(!q->open){
        pthread_mutex_unlock(&q->lock);
        return -1;
    }

    unsigned i = q->head + q->count;
    if(i >= AQSIZE) i -= AQSIZE;

    q->falOne[i] = l1;
    q->falTwo[i] = l2;
    q->fsIndex[i] = writeIndex;
    q->count++;

    pthread_cond_signal(&q->read_ready);

    pthread_mutex_unlock(&q->lock);

    return 0;

}

int dequeue_analysis(analysis_queue_t *q, FileAndList** l1_address, FileAndList** l2_address, int* indexAddress){

    if(pthread_mutex_lock(&q->lock)){
        perror("lock failed");
        abort();
    }

	while (q->count == 0 && q->open) {
		pthread_cond_wait(&q->read_ready, &q->lock);
	}
    
	if (q->count == 0) {
		pthread_mutex_unlock(&q->lock);
		return -1;
	}

	*l1_address = q->falOne[q->head];
    *l2_address = q->falTwo[q->head];
    *indexAddress = q->fsIndex[q->head];
	--q->count;
	++q->head;
	if (q->head == AQSIZE) q->head = 0;
	
	pthread_cond_signal(&q->write_ready);
	
	pthread_mutex_unlock(&q->lock);
	
	return 0;  

}

int aQClose(analysis_queue_t *q){
    pthread_mutex_lock(&q->lock);
	q->open = 0;
    if(pthread_cond_broadcast(&q->read_ready)) perror("broadcast failure");
    if(pthread_cond_broadcast(&q->write_ready)) perror("broadcast failure");
	pthread_mutex_unlock(&q->lock);	
	return 0;
}