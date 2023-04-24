/* Once the backend server receives the request from the main server, 
** it searches in the database to get all requested users' availability, 
** runs an algorithm to find the intersection among them and 
** sends the result back to the main server.
*/
/*Backend server(A and B): store the availability of all users and get the time slots that work for all meeting participants once receiving requests.*/
/* This code was refered to Beej's guide to Network Program especially chapter 6.*/


#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <errno.h>
#include <string.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <netdb.h>
#include <arpa/inet.h>
#include <sys/wait.h>
#include <signal.h>
#include <ctype.h>


#define serverM_PORT "23119" // server M UDP PORT
#define PORT "22119" // serverB UDP PORT NUMBER, 22000 + 3 digits of my ID

#define MAXDATASIZE 50000 // max number of bytes we can get at once

#define LINE_MAX_SZ 200 // max number of bytes per line in the input file
#define NAME_MAX_SZ 50 // max length of each name
#define TIME_MAX_SZ 50 // max number of available time slots for a user
#define MAX_USERS 250 // max number of users that can be stored in 

#define MAX_USERNAME_STR 200 // max size of input for usernames including spaces


typedef struct {
    char name[NAME_MAX_SZ]; // User's name as a string
    int availability[TIME_MAX_SZ][2]; // User's availability.
    int num_timeslots; // number of available timeslots of each user
} user_t;

//int read_usernames(char *filename, char **names);
user_t* read_users(char *filename, int* num_users);

int split_str(char *matched_usernames_arr[], char matched_usernames[], char *delimeter);
user_t* copy_matched_users(user_t *users, int num_users, char *matched_usernames[], int matched_users_size);
void get_time_intersections(user_t* users, int num_users, int time_intersections[][2], int *time_intersections_size);
void availability_copy(int dest[][2], int src[][2], int size);
int max(int a, int b);
int min(int a, int b);

void replace_char(char *str, char find, char replace);

int update_availability(user_t *matched_users, int matched_users_size, int register_time[], user_t *users, int num_users);
int remove_element(int arr[][2], int arr_size, int element[]);
int insert_element(int arr[][2], int arr_size, int idx, int element[]);
void intersections_to_str(int intersections[][2], int intersections_size, char intersections_str[]);

int main(){
    int sockfd, new_fd, numbytes, usernames_byte=0; // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    struct addrinfo serverB_hints, *servBinfo;
    socklen_t addr_len;
    struct sockaddr_storage their_addr; // connector's address info

    char s[INET6_ADDRSTRLEN];
    int rv, yes=1;

    memset(&hints, 0, sizeof hints); // make sure the struct is empty.
    hints.ai_family= AF_INET; // IPv4 only.
    hints.ai_socktype = SOCK_DGRAM; // DCP Connections.
    // hints.ai_flags = AI_PASSIVE; // use my IP.
    const char* myIP = "127.0.0.1"; // server IP. Here, it is the IP address of serverM.

    memset(&serverB_hints, 0, sizeof serverB_hints);
    serverB_hints.ai_family = AF_INET; // set to AF_INET to use IPv4
    serverB_hints.ai_socktype = SOCK_DGRAM;

    // get address info of serverA(UDP)
    if ((rv = getaddrinfo(myIP, serverM_PORT, &hints, &servinfo)) != 0) { 
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); 
        return 1;
    }
    
    // get address info of serverB(UDP)
    if((rv = getaddrinfo(myIP, PORT, &serverB_hints, &servBinfo)) != 0){
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); 
        return 1;
    }

    // loop through all the results and make a socket 
    for(p = servinfo; p != NULL; p = p->ai_next) {
        if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { 
            perror("talker: socket"); 
            continue;
        } 
        break;
    }


    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n"); 
        return 2;
    }

    /* Event: Booting Up (Only while starting). */
    printf("Server B is up and running using UDP on port %s.\n", PORT); // ONSCREEN MESSAGE

    // Read the input file(users' names and availability.)
    int num_users;
    user_t* users = read_users("b.txt", &num_users);

    char str_all_usernames[MAXDATASIZE];
    memset(&str_all_usernames, 0, sizeof str_all_usernames);


    if (users != NULL) {
        for (int i = 0; i < num_users; i++) {
            // printf("Name: %s\n", users[i].name);
            // printf("All Names: %s\n", str_all_usernames);
            strcat(str_all_usernames, users[i].name);
            strcat(str_all_usernames, ";"); // delimiter: ;
            // for (int j = 0; j < users[i].num_timeslots; j++) {
            //     printf("Timeslot %d: %d-%d\n", j + 1,
            //         users[i].availability[j][0],
            //         users[i].availability[j][1]);
            // }
        }
    }
    
    
    // Send the list of usernames to the main server(serverM).
    if ((numbytes = sendto(sockfd, str_all_usernames, strlen(str_all_usernames)+1, 0, 
        p->ai_addr, p->ai_addrlen) == -1)) {
        perror("Send failed at serverB");
        exit(EXIT_FAILURE);
    }
    /* Event: After sending the list of usernames to the main server.*/
    printf("Server B finished sending a list of usernames to Main Server.\n"); // ONSCREEN MESSAGE

    // loop through all the results and bind to the first we can. (This should be after sending a message)
    for(p = servBinfo; p != NULL; p = p->ai_next) {
       if ((new_fd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { 
            perror("listener: socket"); 
            continue;
        }
        
        if (bind(new_fd, p->ai_addr, p->ai_addrlen) == -1) { 
            close(new_fd);
            perror("listener: bind"); 
            continue;
        } 
        break;
    }


    if (p == NULL) {
        fprintf(stderr, "listener: failed to create socket\n"); 
        return 2;
    }


    while(1){
        /* Event: After receiving the usernames from the Main Server.*/
        char matched_usernames[MAXDATASIZE];
        char temp_matched_usernames[MAXDATASIZE];
        addr_len = sizeof their_addr;
    
        if ((numbytes = recvfrom(new_fd, matched_usernames, MAXDATASIZE-1 , 0, 
            (struct sockaddr *)&their_addr, &addr_len)) == -1) { 
                perror("recvfrom");
                exit(1); 
        }

        /* Event: After receiving the usernames from the Main Server.*/
        printf("Server B received the usernames from Main Server using UDP over port %s.\n", PORT); // ONSCREEN MESSAGE

        /* Run scheduling algorithm and get common availability.*/
        char *matched_usernames_arr[MAX_USERS];
        int matched_usernames_arr_size;

        strcpy(temp_matched_usernames, matched_usernames);
        matched_usernames_arr_size = split_str(matched_usernames_arr, temp_matched_usernames, ";");
        
        // !!DELETE
        // printf("matched_usernames_arr_size: %d\n",matched_usernames_arr_size);
        // for(int i=0; i<matched_usernames_arr_size; i++){
        //     printf("matched_usernames_arr[%d]: %s\n", i, matched_usernames_arr[i]);
        // }
        
        user_t *matched_users = copy_matched_users(users, num_users, matched_usernames_arr, matched_usernames_arr_size);
        
        // !!DELETE
        // printf("matched_usernames_arr_size: %d\n",matched_usernames_arr_size);
        // for (int i = 0; i < matched_usernames_arr_size; i++) {
        //     printf("matched_users[%d].name: %s\n", i, matched_users[i].name);
        //     for (int j = 0; j < matched_users[i].num_timeslots; j++) {
        //         printf("Timeslot %d: %d-%d\n", j + 1,
        //             matched_users[i].availability[j][0],
        //             matched_users[i].availability[j][1]);
        //     }
        // }


        int time_intersections[TIME_MAX_SZ][2];
        int time_intersections_size=0;
        get_time_intersections(matched_users, matched_usernames_arr_size, time_intersections, &time_intersections_size);

        // printf("time_intersections_size: %d\n",time_intersections_size);
        // for(int i =0; i<time_intersections_size;i++){
        //     printf("%d: [%d, %d]\n",i, time_intersections[i][0], time_intersections[i][1]);
        // }

        /* Event: After running the algorithm to find the intersections of time availability among all participants.*/
        char msg_matched_usernames[MAXDATASIZE];
        char msg_time_intersections[MAXDATASIZE];
        
        strcpy(msg_matched_usernames, matched_usernames);
        replace_char(msg_matched_usernames, ';', ',');
        msg_matched_usernames[strlen(msg_matched_usernames)-1] = '\0'; // remove the last comma

        int index = 0;
        index += sprintf(&msg_time_intersections[index], "["); // add opening bracket for entire array
        for (int i = 0; i < time_intersections_size; i++) {
            index += sprintf(&msg_time_intersections[index], "["); // add opening bracket for row
            for (int j = 0; j < 2; j++) {
                int n = sprintf(&msg_time_intersections[index], "%d,", time_intersections[i][j]); // add comma between elements
                index += n;
            }
            msg_time_intersections[index-1] = ']'; // replace last comma with closing bracket for row
            index += sprintf(&msg_time_intersections[index], ","); // add newline after row
        }
        if(index == 1){
            // if there is no intersection.
            msg_time_intersections[index] = ']'; // add closing bracket for entire array
            msg_time_intersections[index+1] = '\0'; // terminate the string
        }else if(index > 1){
            // if there are intersections, overwrite the last comma with closing bracket
            msg_time_intersections[index-1] = ']'; // add closing bracket for entire array
            msg_time_intersections[index] = '\0'; // terminate the string
        }


        printf("Found the intersection result: %s for %s.\n", msg_time_intersections, msg_matched_usernames); // ONSCREEN MESSAGE

        
        /* Event: After sending the results to the main server.*/
        // Send the time intersections to the main server(serverM).
        if ((numbytes = sendto(new_fd, msg_time_intersections, strlen(msg_time_intersections)+1, 0, 
                (struct sockaddr *)&their_addr, addr_len)) == -1) {
            perror("Send failed at serverB");
            exit(EXIT_FAILURE);
        }
        printf("Server B finished sending the response to Main Server.\n"); // ONSCREEN MESSAGE


        if(strcmp(msg_time_intersections, "[]")!=0){

            /* Extra event: Receive register time and participants' names and update their availability.
            Don't have to overwrite input files, but update users' availability on memory.*/
            // receive register time
            char register_time_str[MAXDATASIZE];
            if ((numbytes = recvfrom(new_fd, register_time_str, MAXDATASIZE-1 , 0, 
                (struct sockaddr *)&their_addr, &addr_len)) == -1) { 
                    perror("recvfrom");
                    exit(1); 
            }
            // printf("register_time_str: %s\n",register_time_str);

            //update availability
            int register_time[2];
            sscanf(register_time_str, "[%d,%d]", &register_time[0], &register_time[1]);
            printf("Register a meeting at %s and update the availability for the following users:\n", register_time_str); // ONSCREEN MESSAGE
            update_availability(matched_users, matched_usernames_arr_size, register_time, users, num_users);

            //notify update is completed
            if ((numbytes = sendto(new_fd, "1", strlen("1")+1, 0, 
                (struct sockaddr *)&their_addr, addr_len)) == -1) {
                    perror("Send failed at serverB");
                    exit(EXIT_FAILURE);
            }
            
            printf("Notified Main Server that registration has finished.\n"); // ONSCREEN MESSAGE
        }
    }

    freeaddrinfo(servinfo); // all done with this structure
    close(sockfd);


    return 0;
}


user_t* read_users(char *filename, int* num_users){
    FILE* fp;
    
    fp = fopen(filename, "r");
    if(fp==NULL){
        printf("Could not open the file.\n");
        return NULL;
    }

    
    // allocate space for user array
    user_t* users = malloc(sizeof(user_t)*MAX_USERS);
    int cnt_users = 0;

    char line[LINE_MAX_SZ];

    // Read input file line by line
    while(fgets(line, LINE_MAX_SZ, fp)!= NULL){
        // remove trailing newline character
        line[strcspn(line, "\n")] = '\0';

        // split line into name and timeslots
        char* name = strtok(line, ";");
        char* timeslots = strtok(NULL, ";");

        // printf("%s\t%s\n", name, timeslots);

        // allocate space for user
        user_t* user = &users[cnt_users];
        strcpy(user->name, name);

        // time slot
        int timeslot_cnt = 0;
        char *token;
        char *rest = timeslots;
        int i = 0, j = 0;

        while((token = strtok_r(rest, "[],", &rest))){
            user->availability[i][j] = atoi(token);
            j++;
            if(j==2){
                timeslot_cnt++;
                j=0;
                i++;
            }
        }
        
        user->num_timeslots = timeslot_cnt;
        cnt_users++;
    }

    *num_users = cnt_users;

    fclose(fp);

    return users;
}

int split_str(char *matched_usernames_arr[], char matched_usernames[], char *delimeter){
    int size=0;
    // split 
    char *token = strtok(matched_usernames, delimeter);

    int i = 0;
    while (token != NULL) {
        matched_usernames_arr[i] = malloc(MAX_USERNAME_STR * sizeof(char));
        
        strcpy(matched_usernames_arr[i], token);

        token = strtok(NULL, delimeter);
        i++;
        size++;
    }

    return size;
}

user_t* copy_matched_users(user_t *users, int num_users, char *matched_usernames[], int matched_users_size){

    user_t *matched_users = malloc(sizeof(user_t)*(matched_users_size));

    for(int i=0; i < matched_users_size; i++){
        user_t* matched_user = &matched_users[i];
        memset(matched_user, 0, sizeof *matched_user);
        
        for(int j=0; j < num_users; j++){
            if(strcmp(matched_usernames[i], users[j].name)==0){
                strcpy(matched_user->name, users[j].name);
                availability_copy(matched_user->availability, users[j].availability, users[j].num_timeslots);
                matched_user->num_timeslots = users[j].num_timeslots;
                break;
            }
        
        }       
    }

    return matched_users;
}


void get_time_intersections(user_t *users, int num_users,
                            int time_intersections[][2],
                            int *time_intersections_size) {
    if (num_users == 0) {
        printf("The number of users should be bigger than 0 to get intersection.\n");
        return;
    }

    if (num_users == 1) {
        availability_copy(time_intersections, users[0].availability, users[0].num_timeslots);
        *time_intersections_size = users[0].num_timeslots;

        return;
    }

    int prev_availability[TIME_MAX_SZ][2];
    int prev_availability_size = users[0].num_timeslots;
    availability_copy(prev_availability, users[0].availability,
                    users[0].num_timeslots);
    int i = 1;
    int temp_availability[TIME_MAX_SZ][2];
    int temp_availability_size = 0;
    while (i < num_users) {
        int j = 0, k = 0;
        memset(&temp_availability, 0, sizeof temp_availability);
        temp_availability_size = 0;

        while (j < prev_availability_size && k < users[i].num_timeslots) {
            int first = max(prev_availability[j][0], users[i].availability[k][0]);
            int second = min(prev_availability[j][1], users[i].availability[k][1]);

            if (first < second) {
                temp_availability[temp_availability_size][0] = first;
                temp_availability[temp_availability_size][1] = second;
                temp_availability_size++;
            }

            if (prev_availability[j][1] < users[i].availability[k][1]){
                j++;
            }
            else if (prev_availability[j][1] > users[i].availability[k][1]){
                k++;
            }
            else {
                j++;
                k++;
            }
        }

        memset(&prev_availability, 0, sizeof prev_availability);
        availability_copy(prev_availability, temp_availability,
                        temp_availability_size);
        prev_availability_size = users[i].num_timeslots;

        i++;
  }

  availability_copy(time_intersections, temp_availability, temp_availability_size);
  *time_intersections_size = temp_availability_size;

}

void availability_copy(int dest[][2], int src[][2], int size){
    for(int i=0; i < size; i ++){
        dest[i][0] = src[i][0];
        dest[i][1] = src[i][1];
    }
}

int max(int a, int b) {
    if (a > b) {
        return a;
    } else {
        return b;
    }
}

int min(int a, int b) {
    if (a > b) {
        return b;
    } else {
        return a;
    }
}

void replace_char(char *str, char find, char replace) {
    int length = strlen(str);
    for (int i = 0; i < length; i++) {
        if (str[i] == find) {
            str[i] = replace;
        }
    }
}

int update_availability(user_t *matched_users, int matched_users_size, int register_time[], user_t *users, int num_users){
    // update matched users' availability. e.g. if register_time==[1,2] and availability==[[1,5],[7,8]]
    // availability should be updated to [[2,5],[7,8]]
    
    
    for(int i=0; i < matched_users_size; i++){
        char prev_availability_str[MAXDATASIZE];
        char new_availability_str[MAXDATASIZE];
        int updated = 0;
        intersections_to_str(matched_users[i].availability, matched_users[i].num_timeslots, prev_availability_str);
        
        for(int j=0; j < matched_users[i].num_timeslots; j++){
            int start = matched_users[i].availability[j][0];
            int end = matched_users[i].availability[j][1];

            // when register_time==[2,3]
            if(start == register_time[0] && end == register_time[1]){
                //[2,3] -> remove
                if(!remove_element(matched_users[i].availability, matched_users[i].num_timeslots, register_time)){
                    printf("Cannot find [%d,%d].\n", register_time[0],register_time[1]);
                }
                matched_users[i].num_timeslots--;
                updated = 1;
            }else if(start == register_time[0] && end > register_time[1]){
                //[2,5] -> [3,5]
                matched_users[i].availability[j][0] = register_time[1];
                updated = 1;
            }else if(start < register_time[0] && end == register_time[1]){
                //[1,3] -> [1,2]
                matched_users[i].availability[j][1] = register_time[0];
                updated = 1;
            }else if(start < register_time[0] && end > register_time[1]){
                //[1,5] -> [1,2], [3,5]
                matched_users[i].availability[j][1] = register_time[0];
                int temp[2] = {register_time[1],end};
                insert_element(matched_users[i].availability,matched_users[i].num_timeslots, j+1, temp);
                matched_users[i].num_timeslots++;
                updated = 1;
            }
            if(updated){
                break;
            }
            
            
        }

        intersections_to_str(matched_users[i].availability, matched_users[i].num_timeslots, new_availability_str);

        printf("%s: updated from %s to %s\n", matched_users[i].name, prev_availability_str, new_availability_str); //ONSCREEN MESSAGE


        // update users
        for(int j=0; j < num_users; j++){
            if(strcmp(users[j].name, matched_users[i].name)==0){
                availability_copy(users[j].availability, matched_users[i].availability, matched_users[i].num_timeslots);
                users[j].num_timeslots = matched_users[i].num_timeslots;
                break;
            }
        }


    }

}


int remove_element(int arr[][2], int arr_size, int element[]){
    int found=0;
    int row;
    // search for the element
    for(int i=0; i < arr_size;i++){
        if(arr[i][0] == element[0] && arr[i][1] == element[1]){
            row=i;
            found=1;
            break;
        }

    }

    // shift the element to fill the gap caused by removal.
    for(int i=row; i < arr_size-1; i++){
        arr[i][0] = arr[i+1][0];
        arr[i][1] = arr[i+1][1];
    }
    
    return found;
}

int insert_element(int arr[][2], int arr_size, int idx, int element[]){
    // insert element into arr at idx.
    if(idx > arr_size){
        printf("Index to be inserted should be less than or equal to arr_size.\n");
        return 0;
    }


    // shift the elements to make room for the new elemenet
    for(int i=arr_size; i > idx; i--){
        arr[i][0] = arr[i-1][0];
        arr[i][1] = arr[i-1][1];
    }

    // insert the new element
    arr[idx][0] = element[0];
    arr[idx][1] = element[1];

    return 1;
}

void intersections_to_str(int intersections[][2], int intersections_size, char intersections_str[]){
    int index = 0;
    index += sprintf(&intersections_str[index], "["); // add opening bracket for entire array
    for (int i = 0; i < intersections_size; i++) {
        index += sprintf(&intersections_str[index], "["); // add opening bracket for row
        for (int j = 0; j < 2; j++) {
            int n = sprintf(&intersections_str[index], "%d,", intersections[i][j]); // add comma between elements
            index += n;
        }
        intersections_str[index-1] = ']'; // replace last comma with closing bracket for row
        index += sprintf(&intersections_str[index], ","); // add newline after row
    }
    if(index == 1){
        // if there is no intersection.
        intersections_str[index] = ']'; // add closing bracket for entire array
        intersections_str[index+1] = '\0'; // terminate the string
    }else if(index > 1){
        // if there are intersections, overwrite the last comma with closing bracket
        intersections_str[index-1] = ']'; // add closing bracket for entire array
        intersections_str[index] = '\0'; // terminate the string
    }
}