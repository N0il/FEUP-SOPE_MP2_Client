/**
 * @file client.c
 * @brief Client side of a local client-server system, implemented using named pipes
 * @version 0.1
 * @date 2021-04-11
 * 
 * @copyright Copyright (c) 2021
 * 
 */

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <fcntl.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <pthread.h>
#include <time.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <errno.h>

#define ERROR -1
#define MILLISEC_TO_NANOSEC_MULTIPLIER 1000000  //1 millisecond = 1,000,000 nanoseconds
#define IWANT "IWANT"
#define RECVD "RECVD"
#define TSKEX "TSKEX"
#define TSKDN "TSKDN"
#define GOTRS "GOTRS"
#define TLATE "2LATE"
#define CLOSD "CLOSD"
#define GAVUP "GAVUP"
#define FAILD "FAILD"

/*-------------------------GLOBAL VARIABLES-------------------------*/

pthread_mutex_t lock; //structure used to implement mutex

char * public_fifo_path;  //public fifo file path

bool public_fifo_closed = false; //check if public fifo was closed on the read side (server)

bool time_is_up = false; //keep track of execution time


/*-------------------------UTIL FUNCTIONS-------------------------*/

/**
 * @brief register the program logs in format "inst ; i ; t ; pid ; tid ; res ; oper\n" to stdout
 * 
 * @param i the unique identifier of thread request
 * @param t random value between 1-9
 * @param pid process id
 * @param tid thread id
 * @param res res from client or server
 * @param oper the operation that is being registered
 */
void reg(char *i, char *t, char *pid, char  *tid, char *res, char *oper){
    time_t inst;
    time(&inst);

    char reg_string[160];

    char inst_s[20];

    sprintf(inst_s, "%ld", inst);

    strcat(reg_string, inst_s);
    strcat(reg_string, " ; ");
    strcat(reg_string, i);
    strcat(reg_string, " ; ");
    strcat(reg_string, t);
    strcat(reg_string, " ; ");
    strcat(reg_string, pid);
    strcat(reg_string, " ; ");
    strcat(reg_string, tid);
    strcat(reg_string, " ; ");
    strcat(reg_string, res);
    strcat(reg_string, " ; ");
    strcat(reg_string, oper);
    strcat(reg_string, "\n");

    fprintf(stdout, reg_string);
}

//The parameters aren't all necessary
void handle_server_response(char *response, char * i_s, char * t_s, char * process_id_s, char * thread_id_s){
    
    char * p[5];
    p[0] = strtok(response,"; ");

    for(int i = 0; i < 5; i++)
    {
        printf("%s\n",p[i]);
        p[i] = strtok(response, "; ");
    }

    if(p[4] == "-1"){
        printf("The request wasn't attended, because server service is closed\n");
        reg(i_s, t_s, process_id_s, thread_id_s, "-1", CLOSD);
    }
    else{
        reg(i_s, t_s, process_id_s, thread_id_s, p[4], GOTRS);
    }
}

/**
 * @brief sends requests to server through client's threads
 * 
 * @param arg 
 * @return void* 
 */
void *send_request_and_wait_response(void * arg)
{
    //locking code wiht mutex
    pthread_mutex_lock(&lock);

    //message info variables
    int t = rand() % 9 + 1;
    int i = (long int) arg;
    long int thread_id = pthread_self();
    int process_id = getpid();

    printf("t: %d\t i: %d\t thread id: %lu\t process id: %u", t, i, thread_id, process_id); //DEBUG

    //converting integers to strings and making i (universal identifier -> <i><.><process id>)
    char t_s[10], i_s[10], thread_id_s[20], process_id_s[10];

    sprintf(t_s, "%d", t);
    sprintf(thread_id_s, "%ld", thread_id);
    sprintf(process_id_s, "%d", process_id);
    sprintf(i_s, "%d", i);
    strcat(i_s, ".");
    strcat(i_s, process_id_s);
    
    printf("\tid: %s", i_s);  //DEBUG

    //creating request message
    char request[80];

    strcat(request, i_s);
    strcat(request, " ");
    strcat(request, t_s);
    strcat(request, " ");
    strcat(request, process_id_s);
    strcat(request, " ");
    strcat(request, thread_id_s);
    strcat(request, " ");
    strcat(request, "-1");

    printf("\trequest: %s\n", request);  //DEBUG

    //opening public fifo and sending request
    int public_fd = open(public_fifo_path, O_WRONLY);

    if(public_fd == ERROR){ 
        printf("Error while opening public fifo file!\n");
    }

    write(public_fd, request, strlen(request) + 1);

    //the server has closed the read side of pipe
    if(errno == EPIPE){
        public_fifo_closed = true;
        return NULL;
    }

    //registering operation to stdout
    reg(i_s, t_s, process_id_s, thread_id_s, "-1", IWANT);
    
    close(public_fd);

    //creating private fifo file path ("/tmp/pid.tid") to receive server response
    char priv_fifo_path[40];

    strcat(priv_fifo_path, "/tmp/");
    strcat(priv_fifo_path, process_id_s);
    strcat(priv_fifo_path, ".");
    strcat(priv_fifo_path, thread_id_s);

    printf("\t private path: %s\n", priv_fifo_path);  //DEBUG

    // Creating private fifo file
    mkfifo(priv_fifo_path, 0666);

    //getting the response from the server
    int private_fd = open(priv_fifo_path, O_RDONLY);

    if(private_fd == ERROR){ 
        printf("Error while opening private fifo file!\n");
    }

    char response[80];

    //reading the response until the name pipe is closed and still remains execution time in client
    int r = 1;
    while(!time_is_up && r > 0){
        r = read(private_fd, response, 80);
    }

    if(time_is_up){
        reg(i_s, t_s, process_id_s, thread_id_s, "", GAVUP);
        return NULL;
    }

    //handle_server_response(response, i_s, t_s, process_id_s, thread_id_s);
    
    printf("Server response: %s\n", response); //DEBUG

    //closing private fifo file
    close(private_fd);

    //unlocking code with mutex
    pthread_mutex_unlock(&lock);
}

/**
 * @brief stops execution for a random number of milliseconds from 0 to 500 milliseconds
 * 
 * @return ERROR value when error occurred in nanosleep system call, else 0
 */
int random_mili_sleep(){
    //used to set the srand function
    time_t t;
    
    //setting seed for thread random creation
    srand((unsigned) time(&t));

    //stores the pseudo random time between thread creation
    long int rand_nano;

    //structure needed to use nanosleep
    struct timespec req, rem;

    rand_nano = (rand() % 500 + 1) * MILLISEC_TO_NANOSEC_MULTIPLIER;  //interval of (0 - 0.5 seconds or 0 - 500 milliseconds

    req.tv_sec = 0;  //number of seconds                        
    req.tv_nsec = rand_nano;  //number of nanoseconds 
    //req.tv_nsec = 500000000; //DEBUG   
  
    return nanosleep(&req , &rem);
}

/**
 * @brief Checks if the given string is a existing file and of fifo type
 *
 * @param file_name, the string representing the directory or the file to be checked
 * @return true if it is exists, false otherwise
 */
bool fifo_file_checker(char file_name[]){

    bool file_exists = false;

    struct stat buffer;

    if(stat(file_name, &buffer) == 0) file_exists = true;

    return file_exists && ((buffer.st_mode & S_IFMT) == S_IFIFO);
}

/**
 * @brief error on user input
 * 
 * @return ERROR value (-1)
 */
int error_on_input(){
    printf("Usage: ./c <-t nsecs> fifoname\n");
    return ERROR;
}

/*-------------------------MAIN THREAD-------------------------*/

/**
 * @brief main thread (c0)
 * 
 * @param argc number of arguments passed to program
 * @param argv actual argmuents passed to the program
 * @return 0 when succeeded, -1 otherwise
 */
int main(int argc, char* argv[]){

    /*-------------------------PROGRAM INITIALIZATION-------------------------*/

    //getting initial time of program execution
    time_t initial_time;
    time(&initial_time);

    //checking if the coreect number of arguments was given
    if(argc != 4){
        return error_on_input();
    }

    //used to get current seconds in main thread loop
    time_t cur_secs;
        
    //ARG 1: nº of seconds which the program should run in
    char *secs_identifier = argv[1]; 

    if(strcmp(secs_identifier, "-t") != 0){
        return error_on_input();
    }
    
    //ARG 2: nº of seconds which the program should run in
    int nsecs = atoi(argv[2]);  

    //ARG 3: public fifo file path (relative or absolute)
    public_fifo_path = argv[3];

    if(!fifo_file_checker(public_fifo_path)){
        return error_on_input();
    }

    //printf("public fifo path: %s\n", public_fifo_path); //DEBUG

    //initializing pthread mutex structure
    if (pthread_mutex_init(&lock, NULL) != 0) {
        printf("Mutex init has failed\n");
        return ERROR;
    }

    //thread unique identifier
    long int id = 1;

    /*-------------------------CREATING REQUEST THREADS-------------------------*/

    //main thread (c0) continously creates new threads with server requests
    while(true && !public_fifo_closed) {
        //sleeping for a few milliseconds
        if(random_mili_sleep() == ERROR) return ERROR;

        //creating threads
        pthread_t thread_id;

        if(pthread_create(&thread_id, NULL, &send_request_and_wait_response, (void*)id) != 0) return ERROR;

        //checking if nsecs have passed already
        time(&cur_secs);

        if (cur_secs - initial_time >= nsecs){
            //clean threads memory and exit everything here
            printf("Execution time has ended!\n");
            time_is_up = true;
            break;
        }

        id++;
    }

    /*-------------------------ENDING PROGRAM-------------------------*/

    //releasing pthread mutex structure
    pthread_mutex_destroy(&lock);

    //main thread waits for all threads to exit
    pthread_exit(NULL);

    return 0;
}