/* When a user wants to schedule a meeting, the user will start a client program, 
* enter all names involved in the meeting and request the main server for the time intervals
* that works for every participant 
*/
/*Client: used to access the meeting scheduling system*/
/* This code was refered to Beej's guide to Network Program especially chapter 6.*/

#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h> 
#include <sys/socket.h> 
#include <netdb.h>
#include <arpa/inet.h> 
#include <netinet/in.h>
#include <string.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/select.h>



#define SERVER_PORT "24119" // serverM TCP static port number. 24000 + last 3 digits of my student number.

#define MAXDATASIZE 50000 // max number of bytes we can get at once

#define MAX_USERS 250 // max number of users that can be stored in 
#define MAX_USERNAME_STR 200 // max size of input for usernames including spaces
#define TIME_MAX_SZ 50 // max number of available time slots for a user


int split_str(char *arr[], char str[], char *delimeter);
int parse_availability_to_arr(char intersection_result[], int availability[][2]);
int check_validation(int register_time[], int final_time[][2], int final_time_size);

void *get_in_addr(struct sockaddr *sa){
    void *addr;
    char *ipver;

    if(sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    
    printf("caution: we support only IPv4");
    return &(((struct sockaddr_in*)sa)->sin_addr);
    
    
}

void replace_char(char *str, char find, char replace) {
    int length = strlen(str);
    for (int i = 0; i < length; i++) {
        if (str[i] == find) {
            str[i] = replace;
        }
    }
}

void remove_substring(char *str, const char *sub) {
    size_t len = strlen(sub);
    char *p = str;
    while ((p = strstr(p, sub)) != NULL) {
        memmove(p, p + len, strlen(p + len) + 1);
    }
}

int main(){
    // Prepare to launch.
    int status, sockfd, numbytes, portno;
    struct addrinfo hints, *res, *p;// points to the results

    char ipstr[INET6_ADDRSTRLEN];

    memset(&hints, 0, sizeof hints); // set initial value to zero. This is to make sure the struct is empty.
    hints.ai_family = AF_INET; // IPv4.
    hints.ai_socktype = SOCK_STREAM; // TCP.
    // hints.ai_flags = AI_PASSIVE; // fill in my IP. assign the address of my localhost to the socket structures.
    const char* myIP = "127.0.0.1"; // server IP. It can be a domain(www.google.com) or IP addr.


    if((status = getaddrinfo(myIP, SERVER_PORT, &hints, &res))!=0){
        fprintf(stderr, "getaddrinfo error: %s\n", gai_strerror(status));
        return 1;
    }

    // loop through all the results and connect to the first we can
    for(p = res; p!=NULL; p = p->ai_next){
        if((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol))==-1){
            // return -1 == error
            perror("client: socket");
            continue;
        }
        // if socket was created, connect to the main server(serverM).
        if(connect(sockfd, p->ai_addr, p->ai_addrlen) == -1){
            close(sockfd);
            perror("client: connect");
            continue;
        }

        break;
        
    }

    if(p==NULL){
        fprintf(stderr, "client: failed to connect to the server.\n");
        return 2;
    }

    // // convert the IP to a string and print it
    // inet_ntop(p->ai_family, get_in_addr((struct sockaddr *)p->ai_addr), ipstr, sizeof ipstr);
    // printf("client: connecting to %s\n", ipstr);

    /* Event: Booting Up. completed. */
    printf("Client is up and running.\n");  // ONSCREEN MESSAGE

    // get the dynamic port number
    struct sockaddr_in sin;
    socklen_t len = sizeof(sin);
    if (getsockname(sockfd, (struct sockaddr *)&sin, &len) == -1) {
        perror("Error getting socket name");
        exit(1);
    }
    portno = ntohs(sin.sin_port); // client's port

    // printf("Dynamic port number: %d\n", portno);
    

    freeaddrinfo(res); // all done with this structure. free the linked list

    /* Event: Asking user to enter username. */
    while(1){
        // Enter all names involved in the meeting and request the main server
        // for the time intervals that works for every participant.
        // The user can enter up to 10 usernames and all of which are seperated by a single space.
        char usernames[MAX_USERS];
        char non_exist_usernames[MAXDATASIZE];
        memset(&usernames, 0, sizeof usernames);
        memset(&non_exist_usernames, 0, sizeof non_exist_usernames);
        
        printf("Please enter the usernames to check schedule availability: \n"); // ONSCREEN MESSAGE
        fgets(usernames, MAX_USERNAME_STR, stdin); // allows strings with spaces
        usernames[strcspn(usernames, "\n")] = 0; // remove the newline character

        if(send(sockfd, usernames, strlen(usernames) + 1, 0) == -1){
            perror("send");
        }
        
        /* Event: After sending the usernames to the main server. */
        printf("Client finished sending the usernames to Main Server.\n"); // ONSCREEN MESSAGE
        
        /* Event: After receiving the reply from main server if some usernames do not exist.*/
        // seperator: ';'
        char *non_exist_usernames_arr[MAX_USERS];
        int non_exist_usernames_size=0;
        if((numbytes = recv(sockfd, non_exist_usernames, MAXDATASIZE-1, 0))==-1){
            perror("recv");
            exit(1);
        }
        // printf("non_exist_usernames: %s\n",non_exist_usernames);
        if(strcmp(non_exist_usernames, "null")!=0){
            char msg_non_exist_usernames[MAXDATASIZE];
            strcpy(msg_non_exist_usernames, non_exist_usernames);
            replace_char(msg_non_exist_usernames, ';', ',');
            msg_non_exist_usernames[strlen(msg_non_exist_usernames)-1] = '\0';
            
            printf("Client received the reply from Main Server using TCP over port %d:\n%s do not exist.\n", portno, msg_non_exist_usernames); // ONSCREEN MESSAGE

            // split non_exist_usernames into array(seperator: ';')
            non_exist_usernames_size = split_str(non_exist_usernames_arr, msg_non_exist_usernames, ",");

        }

        /* Event: After receiving the final result (i.e., meeting time recommendations) from main server. */
        char final_result[MAXDATASIZE];
        char *usernames_arr[MAX_USERS];
        int usernames_arr_size = split_str(usernames_arr, usernames, " ");
        // char *matched_usernames_arr[MAX_USERS];
        // int matched_usernames_arr_size=0;
        char matched_usernames[MAXDATASIZE];
        memset(&matched_usernames, 0, sizeof matched_usernames);

        int idx=0;
        for(int i=0; i<usernames_arr_size; i++){
            int is_exist = 1;
            for(int j=0; j<non_exist_usernames_size;j++){
                if(strcmp(non_exist_usernames_arr[j], usernames_arr[i])==0){
                    is_exist = 0;
                    break;
                }
            }
            if(is_exist){
                // strcpy(matched_usernames_arr[idx], usernames[i]);
                strcat(matched_usernames, usernames_arr[i]);
                strcat(matched_usernames, ",");
                // idx++;
            }
        }
        matched_usernames[strlen(matched_usernames)-1] = '\0';

        // printf("matched_usernames: %s\n", matched_usernames);
        // printf("strlen(matched_usernames): %lu\n", strlen(matched_usernames));

        int received = 0;
        if(strlen(matched_usernames)>0){
            // Set a timeout of 3 seconds
            struct timeval timeout;
            timeout.tv_sec = 3;
            timeout.tv_usec = 0;

            // Set up the file descriptor set to monitor for activity
            fd_set read_fds;
            FD_ZERO(&read_fds);
            FD_SET(sockfd, &read_fds); // Add your socket file descriptor to the set


            /* The below code snippet referred to https://phoenixnap.com/kb/linux-select */
            // Use select() to wait for activity on the socket with a timeout
            int result = select(sockfd + 1, &read_fds, NULL, NULL, &timeout);
            if (result == 0) {
                // Timeout occurred
                printf("Timed out to receive msg from TCP socket. Please try again.\n");
                printf("\n-----Start a new request-----\n\n"); // ONSCREEN MESSAGE
                continue;
            } else if (result < 0) {
                // Error occurred
                printf("Error occured while receiving msg from TCP socket. Please try again.\n");
                printf("\n-----Start a new request-----\n\n"); // ONSCREEN MESSAGE
                continue;
            } else {
                // Data is available to read
                if((numbytes = recv(sockfd, final_result, MAXDATASIZE-1, 0))==-1){
                    perror("recv");
                    exit(1);
                }
                received = 1;
                replace_char(matched_usernames, ' ', ',');
                printf("Client received the reply from Main Server using TCP over port %d:\nTime intervals %s works for %s.\n", portno, final_result, matched_usernames); // ONSCREEN MESSAGE
            }

            
        }
        
        // if(!received){
        //     printf("\n-----Start a new request-----\n\n"); // ONSCREEN MESSAGE
        //     continue;
        // }

        if(strlen(matched_usernames) > 0 && strlen(final_result)>2){


            /* Extra event: Register the meeting time. */
            char register_time_str[100]; // Register time should be the following format "[t1,t2]"
            printf("Please enter the final meeting time to register an meeting:\n"); // ONSCREEN MESSAGE
            fgets(register_time_str, 100, stdin); // allows strings with spaces
            register_time_str[strcspn(register_time_str, "\n")] = 0; // remove the newline character

            int final_time[TIME_MAX_SZ][2];
            int final_time_size;
            final_time_size = parse_availability_to_arr(final_result, final_time);

            
            while(1){
                // check if entered register time is one of the intervals in the recommendations.
                // e.g. [[1,3],[8,10]] -> [1,2] or [1,3] or [9,10] ...

                int register_time[2];
                sscanf(register_time_str, "[%d,%d]", &register_time[0], &register_time[1]);

                if(check_validation(register_time, final_time, final_time_size)){
                    // Valid case
                    // send register_time
                    if(send(sockfd, register_time_str, strlen(register_time_str) + 1, 0) == -1){
                        perror("send");
                    }
                    printf("Sent the request to register %s as the meeting time for %s.\n",register_time_str, matched_usernames); //ONSCREEN MESSAGE

                    // receive update is completed
                    char update_result[20];
                    if((numbytes = recv(sockfd, update_result, MAXDATASIZE-1, 0))==-1){
                        perror("recv");
                        exit(1);
                    }

                    // printf("update_result: %s\n", update_result);
                    printf("Received the notification that registration has finished.\n"); //ONSCREEN MESSAGE

                    break;


                }else{
                    // Invalid case
                    printf("Time interval %s is not valid. Please enter again:\n", register_time_str); //ONSCREEN MESSAGE
                    fgets(register_time_str, 100, stdin); // allows strings with spaces
                    register_time_str[strcspn(register_time_str, "\n")] = 0; // remove the newline character
                    continue;
                }

            }
        
        }else{
            printf("Register unavailable because there is no available time intersections.\n");
        }
        


        /* Event: Upon finishing all operations and starting a new request.*/
        printf("\n-----Start a new request-----\n\n"); // ONSCREEN MESSAGE
        
        
    }

    close(sockfd);

    return 0;


    // connect
}

int split_str(char *arr[], char str[], char *delimeter){
    int size=0;
    // split 
    char *token = strtok(str, delimeter);

    int i = 0;
    while (token != NULL) {
        arr[i] = malloc(MAX_USERNAME_STR * sizeof(char));
        
        strcpy(arr[i], token);

        token = strtok(NULL, delimeter);
        i++;
        size++;
    }

    return size;
}


int parse_availability_to_arr(char intersection_result[], int availability[][2]){
    // time slot
    int timeslot_cnt = 0;
    char *token;
    char *rest = intersection_result;
    
    int i = 0, j = 0;
    while((token = strtok_r(rest, "[],", &rest))){
        availability[i][j] = atoi(token);
        j++;
        if(j==2){
            timeslot_cnt++;
            j=0;
            i++;
        }
    }
    
    return timeslot_cnt;
    
}

int check_validation(int register_time[], int final_time[][2], int final_time_size){
    for(int i=0; i < final_time_size; i++){
        if((register_time[0] >= final_time[i][0]) && (register_time[1] <= final_time[i][1])){
            // it is valid
            return 1;
        }
    }

    return 0; // not valid
}
