#include <stdio.h>
#include <stdlib.h>
#include <pthread.h>
#include <unistd.h>
#include <errno.h>
#include <assert.h>
#include <dirent.h>
#include <string.h>
#include <sys/types.h>
#include <sys/stat.h>
#include <fcntl.h>
#include <ctype.h>
#include "unboundedQ.c"
#include "boundedQ.c"
#include "repo.c"

int exit_status;

typedef struct {
    unbounded_queue_t *dQ;
    bounded_queue_t *fQ;
    char* fileSuffix;
    int id;
} dirThreadArgs;

typedef struct {
    bounded_queue_t *fQ;
    repository *repos;
    char* fileSuffix;
    int id;
} fileThreadArgs;

typedef struct {
    char *filepath1;
    char *filepath2;
    int totalWords;
    double JSD;
} final_struct;

typedef struct {
    analysis_queue_t *aQ;
    final_struct *fs;
    int id;
} analysisThreadArgs;

/**
 * purpose: retrieve the data passed in with the option(s).
 */ 
void obtainSuffix(char* input, char** outputLocation){
    int suffixLength = strlen(input) - 1; // -2 for option text, +1 for null terminator
    char storage[suffixLength];
    for(int i = 0; i < suffixLength; i++){
        storage[i] = input[i+2];
    }
    strcpy(*outputLocation, storage); 
}

/**
 * Purpose: see if the specified suffix is at the end of the 
 * given string
 * 
 * Return values:
 * 1 if suffix is at the end of str
 * 0 if suffix not at the end of str
 * -1 if error occured
 */ 
int strSuffixCmp(char* str, char* suffix){
    int result = 1;

    int suffixLength = strlen(suffix);
    assert(suffixLength >= 0);
    int strLength = strlen(str);
    assert(strLength >= 0);
    int expectedSuffixLocation = strLength - suffixLength;
    for(int i = expectedSuffixLocation; i < strLength; i++){
        if( (tolower(str[i])) == (tolower(suffix[i - expectedSuffixLocation])) ){
            //current character match, so keep going
        } else {
            //the current characters match, we can stop and return false (0).
            return 0;
        }
    }

    return result;
}

void* dirThreadTask(void* arg){
    dirThreadArgs *args = arg;

    //dequeue the directory
    unbounded_queue_t * Q = arg;
    bounded_queue_t fQ;
    init_bounded(&fQ);

    while(args->dQ->activeThreads > 0){
        char* dirPath;

        if(dequeue_unbounded(args->dQ, &dirPath)){
            //the work is done, so terminate the threads
            return NULL;

        } else {

            //obtain data about the directory
            struct stat dirData;
            if (stat(dirPath, &dirData)){
                perror("error, unable to obtain stat struct");
                abort();
            }

            //make sure we actually got a directory from the queue
            if(!(S_ISDIR(dirData.st_mode))){
                fprintf(stderr, "ERROR: a non directory was pulled from the directory queue!\n");
                abort();
            }

            DIR *dirStruct;
            struct dirent *dirEntry;
            dirStruct = opendir(dirPath);

            if(dirStruct){

                while((dirEntry = readdir(dirStruct))){

                    char* directoryPath = malloc(strlen(dirPath) + 1);
                    strcpy(directoryPath, dirPath);

                    //skip any files beginning with a period
                    if ((strncmp(dirEntry->d_name, ".", 1) == 0)){
                        free(directoryPath);
                        continue;
                    }

                    //check what type of file were dealing with
                    if(dirEntry->d_type == DT_DIR){
                        //its a directory
                        int path_size = strlen(directoryPath) + strlen(dirEntry->d_name) + 2;
                        char* new_path = malloc(sizeof(char)*path_size);
                        strcpy(new_path, directoryPath);
                        strcat(new_path, "/");
                        strcat(new_path, dirEntry->d_name);
                        enqueue_unbounded(args->dQ, new_path);
                        free(new_path);

                    } else if(dirEntry->d_type == DT_REG){
                        //regular file
                        if(strSuffixCmp(dirEntry->d_name, args->fileSuffix)){
                            char* temp = malloc(strlen(dirEntry->d_name) + strlen(directoryPath) + 2);
                            strcpy(temp, directoryPath);
                            strcat(temp, "/");
                            strcat(temp, dirEntry->d_name);
                            enqueue(args->fQ, temp);
                        }

                    } else {
                        //unexpected file type: so ignore it
                        continue;
                    }

                    free(directoryPath);

                }

                if (closedir(dirStruct)){
                    perror("ERROR: Directory specified could not be closed");
                    abort();
                }

            } else {
                perror("ERROR: Directory specified could not be opened.");
                //let this iteration end...
            }

        }

        free(dirPath);        
        
    }

    return NULL;
}

void* fileThreadTask(void* arg){
    fileThreadArgs *args = arg;
    repository *repos = args->repos;

    //dequeue will indicate when to break from this loop and function
    while(1){   
        char* fileName;     
        

        if(dequeue(args->fQ, &fileName)){
            //all the work is done, so end this thread
            return NULL;
        }

        //create a new list & open the file from the queue
        List * listOne = NULL;
        FileAndList *fal = malloc(sizeof(FileAndList));
        int file = open(fileName, O_RDONLY, 0);
        if(file == -1)  {  
            perror("File 1 error");
            free(fileName);
            exit_status = 1;
            continue;
        } 

        //fill the list
        fillList(&listOne, file);
        if (listOne != NULL){
            //add the list to the WFD repository
            fal->filepath = fileName; 
            fal->list = listOne;
            append_repository(repos, fal);
        } else {
            //empty files
            fal->filepath = fileName; 
            fal->list = listOne;
            append_repository(repos, fal);
        }

    }

    return NULL;
}

void* analysisThreadTask(void* arg){
    analysisThreadArgs *args = arg;

    FileAndList *temp1;
    FileAndList *temp2;
    int writeIndex;

    //continue looping until -1 is returned from the queue
    while(!dequeue_analysis(args->aQ, &temp1, &temp2, &writeIndex)){

        if( (temp1 == NULL) || (temp2 == NULL) || (writeIndex == -1) ){
            fprintf(stderr, "ERROR, dequeue failed for analysis thread %d\n", args->id);
            abort();
        }

        //store data into the specified index of the final structure array
        int wordCount;
        args->fs[writeIndex].filepath1 = temp1->filepath;
        args->fs[writeIndex].filepath2 = temp2->filepath;
        args->fs[writeIndex].JSD = calculateJSD(temp1->list, temp2->list, &wordCount);
        args->fs[writeIndex].totalWords = wordCount;

        //reset the structs just to be safe & for debugging
        writeIndex = -1;
        temp1 = NULL;
        temp2 = NULL;

    }

    return NULL;
}

void sortStruct(final_struct *fs, int size){
    for (int i = 0; i < size; i++){
        for (int j = i+1; j < size; j++){
            if (fs[i].totalWords < fs[j].totalWords){
                //swap
                char *fsi_path1 = fs[i].filepath1;
                char *fsi_path2 = fs[i].filepath2; 
                int temp_total = fs[i].totalWords;
                double temp_JSD = fs[i].JSD;

                fs[i].filepath1 = fs[j].filepath1;
                fs[i].filepath2 = fs[j].filepath2;
                fs[i].totalWords = fs[j].totalWords;
                fs[i].JSD = fs[j].JSD;

                fs[j].filepath1 = fsi_path1;
                fs[j].filepath2 = fsi_path2;
                fs[j].totalWords = temp_total;
                fs[j].JSD = temp_JSD;

                // free(fsi_path1);
                // free(fsi_path2);

            }
        }
    }
}

int main(int argc, char ** argv){

    //---------------------------------------------------------
    // SET UP
    //---------------------------------------------------------

    int directory_threads = 1;
    int file_threads = 1;
    int analysis_threads = 1; 
    char *search_suffix;
    int suffixAssigned = 0;
    exit_status = 0;
    
    //read in options from command line
    int i = 1;
    while (i < argc){
        char *str;
        if (strncmp(argv[i], "-s", 2) == 0){
            search_suffix = malloc(strlen(argv[i]) + 1);
            obtainSuffix(argv[i], &search_suffix);
            suffixAssigned = 1;

        } else if (strncmp(argv[i], "-d", 2) == 0){
            char* temp = malloc(strlen(argv[i]) + 1);
            obtainSuffix(argv[i], &temp);
            directory_threads = atoi(temp);
            free(temp);

        } else if (strncmp(argv[i], "-a", 2) == 0){
            char* temp = malloc(strlen(argv[i]) + 1);
            obtainSuffix(argv[i], &temp);
            analysis_threads = atoi(temp);
            free(temp);

        } else if (strncmp(argv[i], "-f", 2) == 0){
            char* temp = malloc(strlen(argv[i]) + 1);
            obtainSuffix(argv[i], &temp);
            file_threads = atoi(temp);
            free(temp);
        }
        i++;
    }

    //if a suffix wasnt given, assign the default value
    if(!suffixAssigned){
        search_suffix = malloc(strlen(".txt") + 1);
        strcpy(search_suffix, ".txt");
    }

    //---------------------------------------------------------
    // COLLETION PAHSE
    //---------------------------------------------------------
 
    //Declare and initialize our queues and WFD repository
    unbounded_queue_t directoryQueue;
    bounded_queue_t fileQueue;
    repository repos;
    if (init_repository(&repos, 1)){
        perror("Repository failure");
        abort();
    }
    init_unbounded(&directoryQueue);
    init_bounded(&fileQueue);

    //set active threads = 1, to account for the main thread
    directoryQueue.activeThreads = 1;

    //set up thread argument arrays
    pthread_t *tids = malloc((file_threads + directory_threads) * sizeof(pthread_t));
    fileThreadArgs *fArgs = malloc(file_threads * sizeof(fileThreadArgs));
    dirThreadArgs *dArgs = malloc(directory_threads * sizeof(dirThreadArgs));

    //start the threads
    for(int loopIndex = 0; loopIndex < (file_threads + directory_threads); loopIndex++){

        if(loopIndex < directory_threads){

            //were making directory threads
            pthread_mutex_lock(&directoryQueue.lock);
	        directoryQueue.activeThreads++;
	        pthread_mutex_unlock(&directoryQueue.lock);
            dArgs[loopIndex].dQ = &directoryQueue;
            dArgs[loopIndex].fQ = &fileQueue;
            dArgs[loopIndex].fileSuffix = search_suffix;
            dArgs[loopIndex].id = loopIndex;
            pthread_create(&tids[loopIndex], NULL, dirThreadTask, &dArgs[loopIndex]);

        } else {

            //were making file threads
            fArgs[loopIndex - directory_threads].fQ = &fileQueue;
            fArgs[loopIndex - directory_threads].repos = &repos;
            fArgs[loopIndex - directory_threads].fileSuffix = search_suffix;
            fArgs[loopIndex - directory_threads].id = loopIndex;
            pthread_create(&tids[loopIndex], NULL, fileThreadTask, &fArgs[loopIndex - directory_threads]);

        }

    }

    //read in command line input, looking for files/directories
    for(int i = 1; i < argc; i++){
        if(strncmp(argv[i], "-s", 2) == 0 || strncmp(argv[i], "-d", 2) == 0 || strncmp(argv[i], "-a", 2) == 0|| strncmp(argv[i], "-f", 2) == 0){
            //were dealing with an option. ignore it.
            continue;

        } else {

            //then we are dealing with a directory or a file.
            struct stat dirData;

            if (stat(argv[i], &dirData)){
                //if theres an error report it and continue but exit will be a failure
                perror(argv[i]);
                exit_status = 1;
                continue;
            }

            //check the file type
            if (S_ISDIR(dirData.st_mode)){
                //we found a directory, so add it to the unbounded directory queue
                enqueue_unbounded(&directoryQueue, argv[i]);

            } else if (S_ISREG(dirData.st_mode)){
                //we found a file; checking its suffix
                if(strSuffixCmp(argv[i], search_suffix)){
                    //enqueue the file path
                    char* temp = malloc(strlen(argv[i]) + 1);
                    strcpy(temp, argv[i]);
                    enqueue(&fileQueue, temp);
                }

            } else {
                //for any other file type, just ignore it
                continue;
            }
        }
    }

    //"deactivate" the main thread by decrementing thee activeThread counter
    pthread_mutex_lock(&directoryQueue.lock);
    directoryQueue.activeThreads--;

    //if all the dir threads are dormant, wake them up so they can exit
	if(directoryQueue.activeThreads == 0){
		pthread_cond_broadcast(&directoryQueue.read_ready);
	}
	pthread_mutex_unlock(&directoryQueue.lock);

    //wait for all of the file & dir threads to finish
    for(int i = 0; i < (file_threads + directory_threads); i++){
        if(i == directory_threads){
            //close the fileQueue after all directory threads finish
            qclose(&fileQueue);
        }
        pthread_join(tids[i], NULL);
    }

    //free undeeded resources
    free(tids);
    free(dArgs);
    free(fArgs);

    //TODO: ERROR CONDITION: IF REPOS < 2 FILES
    if (repos.size < 2){
        fprintf(stderr, "ERROR: Arguments do not have enough files to compute JSD\n");
        for(int i = 0; i < repos.nextIndex; i++){
            free(repos.fal[i]->filepath);
        }
        destroy_unbounded(&directoryQueue);
        destroy_bounded(&fileQueue);
        destroy_repository(&repos);
        free(search_suffix);
        return EXIT_FAILURE;
    }

    //---------------------------------------------------------------------
    // STARTING ANALYSIS PHASE
    //---------------------------------------------------------------------

    //create the analysis queue
    analysis_queue_t analysisQueue;
    init_analysis(&analysisQueue);

    //calculate the number of file pairsing to be created
    int numPairings = (int) ( 0.5 * (repos.nextIndex) * ( repos.nextIndex - 1) );

    //create the thread arg struct
    final_struct *fs = malloc(sizeof(final_struct) * numPairings);
    pthread_t *analysisTid = malloc(analysis_threads * sizeof(pthread_t));
    analysisThreadArgs *analysisArgs =  malloc(analysis_threads * sizeof(analysisThreadArgs));

    //start up the analysis threads
    for(int i = 0; i < analysis_threads; i++){
        analysisArgs[i].aQ = &analysisQueue;
        analysisArgs[i].id = i;
        analysisArgs[i].fs = fs;
        pthread_create(&analysisTid[i], NULL, analysisThreadTask, &analysisArgs[i]);
    }

    //creating the file pairings
    int pairNumber = 0;   //used to dictate where in the final struct the threads should save their results to
    for(int i = 0; i < repos.nextIndex; i++){
        for(int j = i + 1; j < repos.nextIndex; j++){
            if(enqueue_analysis(&analysisQueue, repos.fal[i], repos.fal[j], pairNumber)){
                fprintf(stderr, "--- ERROR: cannot enqueue since the queue has closed too early ---\n");
            }
            pairNumber++;
        }
    }


    //close the analysis queue
    aQClose(&analysisQueue);

    //wait for the analysis threads to end
    for(int i = 0; i < analysis_threads; i ++){
        pthread_join(analysisTid[i], NULL);
    }

    //sort the contents of the final structure
    sortStruct(fs, numPairings);

    //print the contents of the final structure
    for(int i = 0; i < numPairings; i++){
        printf("%f     %s     %s\n", fs[i].JSD, fs[i].filepath1, fs[i].filepath2);
    }

    //free up all resources
    for(int i = 0; i < repos.nextIndex; i++){
        free(repos.fal[i]->filepath);
    }
    free(fs);
    free(analysisTid);
    free(analysisArgs);
    free(search_suffix);
    destroy_repository(&repos);
    destroy_unbounded(&directoryQueue);
    destroy_bounded(&fileQueue);
    destory_analysis(&analysisQueue);

    return exit_status;
}