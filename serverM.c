/* Once the main server receives the request, 
** it decides which backend server the participants' availability is stored in 
** and sends a request to the responsible backend server for the time intervals 
** that works for all participants 
*/
/*Main server(serverM): coordinate with the backend servers.*/
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

#define UDP_PORT "23119" // UDP PORT
#define TCP_PORT "24119" // TCP PORT
#define serverA_PORT "21119" // serverA UDP PORT NUMBER
#define serverB_PORT "22119" // serverB UDP PORT NUMBER

#define BACKLOG 10 // How many pending connections queue will hold

#define MAXDATASIZE 50000 // max number of bytes we can get at once
#define MAX_USERS 250 // max number of users that can be stored in 
#define MAX_USERNAME_STR 200 // max size of input for usernames including spaces
#define TIME_MAX_SZ 50 // max number of available time slots for a user

void remove_element(char *arr[], int *size, char *element);
void replace_char(char *str, char find, char replace);

int parse_availability_to_arr(char intersection_result[], int availability[][2]);
int find_intersections(int availability_A[][2], int availability_A_size, int availability_B[][2], int availability_B_size, int intersections[][2]);
void availability_copy(int dest[][2], int src[][2], int size);
void intersections_to_str(int intersections[][2], int intersections_size, char intersections_str[]);

int pass_time_to_backServer(char register_time_str[], char *matched_A[], int matched_A_size, char *matched_B[], int matched_B_size,
    int *update_res_A, int *update_res_B);

void sigchld_handler(int s){
    int saved_errno = errno;
    // waitpid() might overwrite errno, so we save and restore it.
    while(waitpid(-1,NULL,WNOHANG)>0);
    errno=saved_errno;
}

void *get_in_addr(struct sockaddr *sa){
    void *addr;
    char *ipver;

    if(sa->sa_family == AF_INET){
        return &(((struct sockaddr_in*)sa)->sin_addr);
    }
    
    printf("caution: we support only IPv4");
    return &(((struct sockaddr_in*)sa)->sin_addr);
    
    
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

int listen_to_backServer(char serverName[], char *all_usernames[], char *each_usernames[], int *curr_num_usernames, int *each_num_usernames){
    int sockfd;
    struct addrinfo hints_serverM={0}, *servMinfo=NULL, *p=NULL;
    int rv;
    int numbytes;
    struct sockaddr_storage their_addr;
    char buf[MAXDATASIZE];
    socklen_t addr_len;
    char s[INET6_ADDRSTRLEN];
    
    const char* myIP = "127.0.0.1"; // server IP. It can be a domain(www.google.com) or IP addr.

    memset(&hints_serverM, 0, sizeof hints_serverM);
    hints_serverM.ai_family = AF_INET; // set to AF_INET to use IPv4
    hints_serverM.ai_socktype = SOCK_DGRAM;


    // Get address info for the serverM UDP port and address to bind
    if ((rv = getaddrinfo(myIP, UDP_PORT, &hints_serverM, &servMinfo)) != 0) {
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv));
        return 1;
    }

    // loop through all the results and bind to the first we can 
    for(p = servMinfo; p != NULL; p = p->ai_next) {
       if ((sockfd = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { 
            perror("listener: socket"); 
            continue;
        }
        
        if (bind(sockfd, p->ai_addr, p->ai_addrlen) == -1) { 
            close(sockfd);
            perror("listener: bind"); 
            continue;
        } 
        break;
    }
    
    if (p == NULL) {
        fprintf(stderr, "listener: failed to bind socket\n"); 
        return 2;
    }
    
    freeaddrinfo(servMinfo);

    // printf("listener: waiting to recvfrom...\n"); 

    
    addr_len = sizeof their_addr;
    
    if ((numbytes = recvfrom(sockfd, buf, MAXDATASIZE-1 , 0, 
        (struct sockaddr *)&their_addr, &addr_len)) == -1) { 
            perror("recvfrom");
            exit(1); 
    }
    
    // printf("listener: got packet from %s\n", 
    
    // inet_ntop(their_addr.ss_family, get_in_addr((struct sockaddr *)&their_addr), s, sizeof s));
    // printf("listener: packet is %d bytes long\n", numbytes); 
    
    buf[numbytes] = '\0';
    // printf("listener: packet contains \"%s\"\n", buf); 

    // add received usernames into all_usernames
    char *token = strtok(buf, ";");

    int i = *curr_num_usernames;
    int num_each = *each_num_usernames;
    int j = 0;
    while (token != NULL && i < MAX_USERS) {
        all_usernames[i] = malloc(MAX_USERNAME_STR * sizeof(char));
        // all_usernames[i++] = token;
        /*Be careful! not to do like above(all_usernames[i++] = token;).
        It causes overwriting the pointer to the allocated memory with the token value, 
        which points to a substring of buf returned by strtok. 
        This means that all_usernames is not actually storing copies of the usernames, but rather pointers to substrings of buf.*/
        strcpy(all_usernames[i], token);

        each_usernames[j] = malloc(MAX_USERNAME_STR * sizeof(char));
        strcpy(each_usernames[j], token);
        num_each++;

        token = strtok(NULL, ";");
        i++;
        j++;
    }

    *curr_num_usernames = i;
    *each_num_usernames=num_each;

    close(sockfd);
 
    return numbytes;
}

int connect_to_backServer(char *matched_usernames_A[], int matched_usernames_size_A, char *matched_usernames_B[], int matched_usernames_size_B,
    char intersection_result_A[], char intersection_result_B[]){
    // backserver is a server. binding happens there.
    int sockfd_A, sockfd_B;
    struct addrinfo *p;
    struct addrinfo hintsA, *servinfoA, *pA;
    struct addrinfo hintsB, *servinfoB, *pB;
    int rvA, rvB;
    int numbytes;
    struct sockaddr_storage their_addr_A;
    struct sockaddr_storage their_addr_B;
    char buf[MAXDATASIZE];
    socklen_t addr_len_A, addr_len_B;

    memset(&hintsA, 0, sizeof hintsA);
    hintsA.ai_family = AF_INET; // set to AF_INET to use IPv4
    hintsA.ai_socktype = SOCK_DGRAM;
    // hints.ai_flags = AI_PASSIVE; // use my IP
    const char* myIP = "127.0.0.1"; // server IP. It can be a domain(www.google.com) or IP addr.

    memset(&hintsB, 0, sizeof hintsB);
    hintsB.ai_family = AF_INET; // set to AF_INET to use IPv4
    hintsB.ai_socktype = SOCK_DGRAM;


    // get addr info of back server(UDP).
    if ((rvA = getaddrinfo(myIP, serverA_PORT, &hintsA, &servinfoA)) != 0) { 
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rvA)); 
        return 1;
    }

    // loop through all the results and make a socket 
    for(p = servinfoA; p != NULL; p = p->ai_next) {
        if ((sockfd_A = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { 
            perror("talker: socket"); 
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n"); 
        return 2;
    }else{
        pA = p;
    }

    // get addr info of back server(UDP).
    if ((rvB = getaddrinfo(myIP, serverB_PORT, &hintsB, &servinfoB)) != 0) { 
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rvB)); 
        return 1;
    }

    // loop through all the results and make a socket 
    for(p = servinfoB; p != NULL; p = p->ai_next) {
        if ((sockfd_B = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { 
            perror("talker: socket"); 
            continue;
        }
        break;
    }

    if (p == NULL) {
        fprintf(stderr, "talker: failed to create socket\n"); 
        return 2;
    }else{
        pB = p;
    }


    
    // Send the matched usernames to each server A or B.
    char msg_matched_usernames_A[MAXDATASIZE];
    char msg_matched_usernames_B[MAXDATASIZE];
    
    
    memset(&msg_matched_usernames_A, 0, sizeof msg_matched_usernames_A); // make sure the struct is empty.
    for(int i=0; i < matched_usernames_size_A; i++){
        strcat(msg_matched_usernames_A, matched_usernames_A[i]);
        strcat(msg_matched_usernames_A, ";");
    }

    memset(&msg_matched_usernames_B, 0, sizeof msg_matched_usernames_B); // make sure the struct is empty.
    for(int i=0; i < matched_usernames_size_B; i++){
        strcat(msg_matched_usernames_B, matched_usernames_B[i]);
        strcat(msg_matched_usernames_B, ";");
    }

    

    

    char msg_matched_A[MAXDATASIZE];
    char msg_matched_B[MAXDATASIZE];
    if(matched_usernames_size_A>0){
        // create a message
        memset(&msg_matched_A, 0 , sizeof msg_matched_A);
        for(int i=0; i < matched_usernames_size_A; i++){
            strcat(msg_matched_A, matched_usernames_A[i]);
            if(i < matched_usernames_size_A - 1){
                strcat(msg_matched_A, ",");
            }
        }
        printf("Found %s located at Server A. Send to Server A.\n", msg_matched_A); // ONSCREEN MESSAGE
        
        if ((numbytes = sendto(sockfd_A, msg_matched_usernames_A, strlen(msg_matched_usernames_A)+1, 0, 
            pA->ai_addr, pA->ai_addrlen)) == -1) {
            perror("Send failed at serverM");
            exit(EXIT_FAILURE);
        }
    }

    if(matched_usernames_size_B>0){
        // create a message
        memset(&msg_matched_B, 0 , sizeof msg_matched_B);
        for(int i=0; i < matched_usernames_size_B; i++){
            strcat(msg_matched_B, matched_usernames_B[i]);
            if(i < matched_usernames_size_B - 1){
                strcat(msg_matched_B, ",");
            }
        }
        printf("Found %s located at Server B. Send to Server B.\n", msg_matched_B); // ONSCREEN MESSAGE

        if ((numbytes = sendto(sockfd_B, msg_matched_usernames_B, strlen(msg_matched_usernames_B)+1, 0, 
            pB->ai_addr, pB->ai_addrlen)) == -1) {
            perror("Send failed at serverM");
            exit(EXIT_FAILURE);
        }

    }
    
    
    // receive time intersections from A
    addr_len_A = sizeof their_addr_A;
    addr_len_B = sizeof their_addr_B;
    if(matched_usernames_size_A>0){
        if ((numbytes = recvfrom(sockfd_A, intersection_result_A, MAXDATASIZE-1 , 0, 
            (struct sockaddr *)&their_addr_A, &addr_len_A)) == -1) { 
                perror("recvfrom");
                exit(1); 
        }
        
        /* Event: After receiving the intersection result from the Main server.*/
        printf("Main Server received from server A the intersection result using UDP over port %s:\n%s.\n", UDP_PORT, intersection_result_A); // ONSCREEN MESSAGE
    }

    
    // receive time intersections from B
    if(matched_usernames_size_B>0){
        if ((numbytes = recvfrom(sockfd_B, intersection_result_B, MAXDATASIZE-1 , 0, 
            (struct sockaddr *)&their_addr_B, &addr_len_B)) == -1) { 
                perror("recvfrom");
                exit(1); 
        }

        /* Event: After receiving the intersection result from the Main server.*/
        printf("Main Server received from server B the intersection result using UDP over port %s:\n%s.\n",UDP_PORT, intersection_result_B); // ONSCREEN MESSAGE
    }

    close(sockfd_A);
    close(sockfd_B);
 
    return numbytes;
}

int main(void){
    int sockfd, new_fd, usernames_byte=0; // listen on sock_fd, new connection on new_fd
    struct addrinfo hints, *servinfo, *p;
    socklen_t sin_size;
    struct sockaddr_storage client_addr; // connector's address info

    char usernames[MAXDATASIZE];    //received usernames from client
    char *client_usernames[MAX_USERS]; // split received usernames from client into the array.
    int num_cl_usernames=0; // number of usernames from client's input
    char not_exist_usernames[MAXDATASIZE]; // received usernames from the client, but not existing on back servers. delimeter: ', '.


    struct sigaction sa;
    char s[INET6_ADDRSTRLEN];
    int rv, yes=1;

    int usernamesA_byte;

    memset(&hints, 0, sizeof hints); // make sure the struct is empty.
    hints.ai_family= AF_INET; // IPv4 only.
    hints.ai_socktype = SOCK_STREAM; // TCP Connections.
    // hints.ai_flags = AI_PASSIVE; // use my IP.
    const char* myIP = "127.0.0.1"; // server IP. It can be a domain(www.google.com) or IP addr.

    if ((rv = getaddrinfo(myIP, TCP_PORT, &hints, &servinfo)) != 0) { 
        fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rv)); 
        return 1;
    }

    // loop through all the results and bind to the first we can.
    for(p = servinfo; p!=NULL; p=p->ai_next){
        // create a socket
        if((sockfd=socket(p->ai_family, p->ai_socktype, p->ai_protocol))==-1){
            perror("server: socket");
            continue;
        }

        // reuse the port
        if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof(int)) == -1){
            perror("setsockopt");
            exit(1);
        }

        // bind
        if(bind(sockfd, p->ai_addr, p->ai_addrlen)==-1){
            close(sockfd);
            perror("server: bind");
            continue;
        }

        break;

    }

    freeaddrinfo(servinfo); // all done with this structure

    if(p==NULL){
        fprintf(stderr, "server: failed to bind\n");
        exit(1);
    }

    // listening (TCP)
    if(listen(sockfd, BACKLOG) == -1){
        perror("listen");
        exit(1);
    }

    sa.sa_handler = sigchld_handler; // kill app dead processes
    sigemptyset(&sa.sa_mask);
    sa.sa_flags = SA_RESTART;
    if(sigaction(SIGCHLD, &sa, NULL) == -1){
        perror("sigaction");
        exit(1);
    }

    /* Event: Booting Up (only while starting). */
    printf("Main Server is up and running.\n");
    
    /* Event: After receiving the username list from server A or B. */
    char *all_usernames[MAX_USERS];
    char *A_usernames[MAX_USERS];
    char *B_usernames[MAX_USERS];
    char *matched_A[MAX_USERS];
    char *matched_B[MAX_USERS];
    int num_all_usernames = 0;
    int num_A_usernames = 0;
    int num_B_usernames = 0;
    
    listen_to_backServer("A", all_usernames, A_usernames, &num_all_usernames, &num_A_usernames);
    printf("Main Server received the username list from server A using UDP over port %s.\n", UDP_PORT); // ONSCREEN MESSAGE

    listen_to_backServer("B", all_usernames, B_usernames, &num_all_usernames, &num_B_usernames);
    printf("Main Server received the username list from server B using UDP over port %s.\n", UDP_PORT); // ONSCREEN MESSAGE

    // !!DELETE
    // printf("num_all_usernames: %d\n",num_all_usernames);
    // printf("num_A_usernames: %d\n",num_A_usernames);
    // printf("num_B_usernames: %d\n",num_B_usernames);
    // for(int i=0;i <num_A_usernames; i++){
    //     printf("A_usernames[%d]: %s\n", i, A_usernames[i]);
    // }
    // for(int i=0;i <num_B_usernames; i++){
    //     printf("B_usernames[%d]: %s\n", i, B_usernames[i]);
    // }
    // for(int i=0;i <num_all_usernames; i++){
    //     printf("all_usernames[%d]: %s\n", i, all_usernames[i]);
    // }

    // after receiving initial usernames stored in server A/B, client will start.
    
    
    while(1){
        
        
        /* main accept() loop */
        if(usernames_byte == 0){
            // call accept only when the client was connected for the first time.
            sin_size = sizeof client_addr;
            // the operating system will automatically assign a temporary port for the client.
            new_fd = accept(sockfd, (struct sockaddr *)&client_addr, &sin_size); // child socket to send and receive messages.
            if(new_fd == -1){
                perror("accept");
                continue;
            }

        }
        

        // inet_ntop(client_addr.ss_family, get_in_addr((struct sockaddr *)&client_addr), s, 
        //     sizeof s);
        // printf("server: got connection from %s\n",s);

                
        
        /* Event: After receiving the request from client. */
        // Receive the usernames which can have up to 10 names and seperated by a single space.
        if((usernames_byte = recv(new_fd, usernames, MAXDATASIZE-1, 0))==-1){
            perror("recv");
            exit(1);
        }
        printf("Main Server received the request from client using TCP over port %s.\n", TCP_PORT); //ONSCREEN MESSAGE
        
        // printf("received usernames from the client: %s\n", usernames);

        // split usernames into an array
        num_cl_usernames=0;
        char *token = strtok(usernames, " ");
        int i = 0;
        while (token != NULL) {
            client_usernames[i] = malloc(MAX_USERNAME_STR * sizeof(char));
            strcpy(client_usernames[i], token);
            num_cl_usernames++;
            token = strtok(NULL, " ");
            i++;
        }

        // for(int i=0; i<num_cl_usernames; i++){
        //     printf("cl_usernames %d: %s\n", i, client_usernames[i]);
        // }

        /* Check if the received usernames from the client are the name list(all_usernames) received from server A or B. */
        int k =0;
        int matched_A_size = 0, matched_B_size = 0;
        int is_exist = 0;
        memset(&not_exist_usernames, 0, sizeof not_exist_usernames);
        for(int i=0; i < num_cl_usernames; i++){
            is_exist = 0;
            for(int j=0; j < num_all_usernames; j++){
                if(strcmp(client_usernames[i], all_usernames[j])==0){
                    // username given by the client exists in back servers.
                    is_exist=1;
                    break;    
                }
            }
            if(is_exist==0){
                // username given by the client does NOT exists in back servers.
                if(strstr(not_exist_usernames, client_usernames[i])==NULL){
                    strcat(not_exist_usernames, client_usernames[i]);
                    strcat(not_exist_usernames, ";");
                }

            }
        }

        for(int i=0; i < num_A_usernames; i++){
            for(int j=0; j < num_cl_usernames; j++){
                if(strcmp(A_usernames[i], client_usernames[j])==0){
                    matched_A[k] = malloc(MAX_USERNAME_STR * sizeof(char));
                    // username given by the client exists in back servers.
                    strcpy(matched_A[k], client_usernames[j]);
                    matched_A_size++;
                    k++;
                    continue;    
                }
            }
        }

        k=0;
        for(int i=0; i < num_B_usernames; i++){
            for(int j=0; j < num_cl_usernames; j++){
                if(strcmp(B_usernames[i], client_usernames[j])==0){
                    matched_B[k] = malloc(MAX_USERNAME_STR * sizeof(char));
                    // username given by the client exists in back servers.
                    strcpy(matched_B[k], client_usernames[j]);
                    matched_B_size++;
                    k++;
                    continue;    
                }
            }
        }

        // !!DELETE
        // printf("matched_A_size: %d\n",matched_A_size);
        // printf("matched_B_size: %d\n",matched_B_size);
        // for(int i=0;i<matched_A_size;i++){
        //     printf("A %d: %s\n", i, matched_A[i]);
        // }
        // for(int i=0;i<matched_B_size;i++){
        //     printf("B %d: %s\n", i, matched_B[i]);
        // }
        // printf("not_exist_usernames: %s\n",not_exist_usernames);

        
        
        char msg_not_exist_usernames[MAXDATASIZE];
        memset(&msg_not_exist_usernames, 0, sizeof msg_not_exist_usernames);
        /* Event: If some usernames do not exist in the username list. */
        if(strlen(not_exist_usernames)>0){
            strcpy(msg_not_exist_usernames, not_exist_usernames);
            replace_char(msg_not_exist_usernames, ';', ',');
            msg_not_exist_usernames[strlen(msg_not_exist_usernames)-1]='\0'; // remove last comma
            printf("%s do not exist. Send a reply to the client.\n", msg_not_exist_usernames); // ONSCREEN MESSAGE
            if(send(new_fd, not_exist_usernames, strlen(not_exist_usernames)+1, 0) == -1){
                perror("send");
            }
        }else{
            if(send(new_fd, "null", strlen("null")+1, 0) == -1){
                perror("send");
            }
        }

        // if(matched_A_size == 0 && matched_B_size == 0){
        //     //if no one matched, receive next request from the client.
        //     continue;
        // }

        char intersection_result_A[MAXDATASIZE];
        char intersection_result_B[MAXDATASIZE];
        /* Event: If the usernames exist in the username list, the main server sends the corresponding usernames to the responsible backend server */
        if(matched_A_size >0 || matched_B_size >0){
            connect_to_backServer(matched_A, matched_A_size, matched_B, matched_B_size, intersection_result_A, intersection_result_B);
        }
        

        // printf("intersection_result_A: %s\n", intersection_result_A);
        // printf("intersection_result_B: %s\n", intersection_result_B);
        
        /* Event: Upon finishing finding the intersection between the results from server A and B. */
        int availability_A[TIME_MAX_SZ][2]; // User's availability.
        int availability_B[TIME_MAX_SZ][2]; // User's availability.
        int availability_A_size=0, availability_B_size=0;
        int intersections_size=0;
        
        if(matched_A_size == 0){
            // if there was no username sent to server A, 
            // set size to -1 in order to distinguish with the case that there is no intersection of multiple users.
            availability_A_size = -1; 
        }else if(matched_A_size > 0){
            availability_A_size = parse_availability_to_arr(intersection_result_A, availability_A);
        }
        
        if(matched_B_size == 0){
            availability_B_size = -1;
        }else if (matched_B_size > 0){
            availability_B_size = parse_availability_to_arr(intersection_result_B, availability_B);
        }
        

        // printf("availability_A_size: %d\n",availability_A_size);
        // for(int i=0;i<availability_A_size;i++){
        //     printf("availability_A[%d]: [%d, %d]\n", i, availability_A[i][0], availability_A[i][1]);
        // }
        // printf("availability_B_size: %d\n",availability_B_size);
        // for(int i=0;i<availability_B_size;i++){
        //     printf("availability_B[%d]: [%d, %d]\n", i, availability_B[i][0], availability_B[i][1]);
        // }

        int intersections[TIME_MAX_SZ][2];
        char intersections_str[MAXDATASIZE];
        memset(&intersections_str, 0, sizeof intersections_str);
        if(availability_A_size == -1 && availability_B_size == -1){
            intersections_size = 0;
            strcpy(intersections_str, "[]");
        }else{
            intersections_size = find_intersections(availability_A, availability_A_size, availability_B, availability_B_size, intersections);
            intersections_to_str(intersections,intersections_size, intersections_str);
            printf("Found the intersection between the results from server A and B:\n%s.\n", intersections_str); // ONSCREEN MESSAGE
        }
        
        
        // printf("intersections_size: %d\n",intersections_size);
        // for(int i =0; i<intersections_size;i++){
        //     printf("intersections %d: [%d, %d]\n",i, intersections[i][0], intersections[i][1]);
        // }
        
        
        /* Event: After sending the final result (i.e., meeting time recommendations) to the client.*/
        // printf("intersections_str: %s\n", intersections_str);
        int nb;
        if((nb = send(new_fd, intersections_str, strlen(intersections_str)+1, 0)) == -1){
            perror("send");
        }
        printf("Main Server sent the result to the client.\n"); // ONSCREEN MESSAGE




        if(availability_A_size > 0 || availability_B_size>0){
            /* Extra event: Receive register time from the client and pass it to the corresponding backservers. */
            char register_time_str[MAXDATASIZE];
            if((nb = recv(new_fd, register_time_str, MAXDATASIZE-1, 0))==-1){
                perror("recv");
                exit(1);
            }
            // printf("register_time_str: %s\n",register_time_str);

            // pass register_time and matched_A_usernames to A, then receive msg that update is completed.
            // pass register_time and matched_B_usernames to B, then receive msg that update is completed.
            int update_result_A; // 1:success , 0:fail
            int update_result_B; // 1:success , 0:fail
            pass_time_to_backServer(register_time_str, matched_A, matched_A_size, matched_B, matched_B_size, &update_result_A, &update_result_B);

            // printf("update_result_A: %d\n",update_result_A);
            // printf("update_result_B: %d\n",update_result_B);

            // send "1" to notify update is completed 
            if((nb = send(new_fd, "1", strlen("1")+1, 0)) == -1){
                perror("send");
            }
        }
        
        

    }

    close(new_fd); 

    return 0;
}

void remove_element(char *arr[], int *size, char *element) {
    int i, j;

    for (i = 0; i < *size; i++) {
        if (strcmp(arr[i], element) == 0) {
            // shift remaining elements down
            for (j = i; j < *size - 1; j++) {
                strcpy(arr[j], arr[j+1]);
            }
            (*size)--; // reduce size of array
            i--; // stay on same index as there could be more occurrences
        }
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

int find_intersections(int availability_A[][2], int availability_A_size, int availability_B[][2], int availability_B_size,
    int intersections[][2]){

    if(availability_A_size == -1){
        // this means the client did not input any name in server A.
        // we can just return B
        availability_copy(intersections, availability_B, availability_B_size);
        return availability_B_size;
    }else if(availability_B_size == -1){
        availability_copy(intersections, availability_A, availability_A_size);
        return availability_A_size;
    }
    
    if(availability_A_size == 0 || availability_B_size == 0){
        // one of availability size is zero, there is no intersection
        return 0;
    }

    int j = 0, k = 0;
    int intersections_size = 0;
    while (j < availability_A_size && k < availability_B_size) {
        int first = max(availability_A[j][0], availability_B[k][0]);
        int second = min(availability_A[j][1], availability_B[k][1]);

        if (first < second) {
            intersections[intersections_size][0] = first;
            intersections[intersections_size][1] = second;
            intersections_size++;
        }

        if (availability_A[j][1] < availability_B[k][1]){
            j++;
        }
        else if (availability_A[j][1] > availability_B[k][1]){
            k++;
        }
        else {
            j++;
            k++;
        }
    }

    return intersections_size;
  
}

void availability_copy(int dest[][2], int src[][2], int size){
    for(int i=0; i < size; i ++){
        dest[i][0] = src[i][0];
        dest[i][1] = src[i][1];
    }
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

int pass_time_to_backServer(char register_time_str[], char *matched_A[], int matched_A_size, char *matched_B[], int matched_B_size,
    int *update_res_A, int *update_res_B){
        // backserver is a server. binding happens there.
        int sockfd_A, sockfd_B;
        struct addrinfo *p;
        struct addrinfo hintsA, *servinfoA, *pA;
        struct addrinfo hintsB, *servinfoB, *pB;
        int rvA, rvB;
        int numbytes;
        struct sockaddr_storage their_addr_A;
        struct sockaddr_storage their_addr_B;
        char buf[MAXDATASIZE];
        socklen_t addr_len_A, addr_len_B;

        memset(&hintsA, 0, sizeof hintsA);
        hintsA.ai_family = AF_INET; // set to AF_INET to use IPv4
        hintsA.ai_socktype = SOCK_DGRAM;
        // hints.ai_flags = AI_PASSIVE; // use my IP
        const char* myIP = "127.0.0.1"; // server IP. It can be a domain(www.google.com) or IP addr.

        memset(&hintsB, 0, sizeof hintsB);
        hintsB.ai_family = AF_INET; // set to AF_INET to use IPv4
        hintsB.ai_socktype = SOCK_DGRAM;


        // get addr info of back server(UDP).
        if ((rvA = getaddrinfo(myIP, serverA_PORT, &hintsA, &servinfoA)) != 0) { 
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rvA)); 
            return 1;
        }

        // loop through all the results and make a socket 
        for(p = servinfoA; p != NULL; p = p->ai_next) {
            if ((sockfd_A = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { 
                perror("talker: socket"); 
                continue;
            }
            break;
        }

        if (p == NULL) {
            fprintf(stderr, "talker: failed to create socket\n"); 
            return 2;
        }else{
            pA = p;
        }

        // get addr info of back server(UDP).
        if ((rvB = getaddrinfo(myIP, serverB_PORT, &hintsB, &servinfoB)) != 0) { 
            fprintf(stderr, "getaddrinfo: %s\n", gai_strerror(rvB)); 
            return 1;
        }

        // loop through all the results and make a socket 
        for(p = servinfoB; p != NULL; p = p->ai_next) {
            if ((sockfd_B = socket(p->ai_family, p->ai_socktype, p->ai_protocol)) == -1) { 
                perror("talker: socket"); 
                continue;
            }
            break;
        }

        if (p == NULL) {
            fprintf(stderr, "talker: failed to create socket\n"); 
            return 2;
        }else{
            pB = p;
        }

        // Send register time to each server A or B.
        if(matched_A_size>0){    
            if ((numbytes = sendto(sockfd_A, register_time_str, strlen(register_time_str)+1, 0, 
                pA->ai_addr, pA->ai_addrlen)) == -1) {
                perror("Send failed at serverM");
                exit(EXIT_FAILURE);
            }
        }

        if(matched_B_size>0){
            if ((numbytes = sendto(sockfd_B, register_time_str, strlen(register_time_str)+1, 0, 
                pB->ai_addr, pB->ai_addrlen)) == -1) {
                perror("Send failed at serverM");
                exit(EXIT_FAILURE);
            }

        }
        
        
        char reply_A[MAXDATASIZE]; // 1: update success, 0: update fail
        char reply_B[MAXDATASIZE];
        // receive update completed message from A
        addr_len_A = sizeof their_addr_A;
        addr_len_B = sizeof their_addr_B;
        if(matched_A_size>0){
            if ((numbytes = recvfrom(sockfd_A, reply_A, MAXDATASIZE-1 , 0, 
                (struct sockaddr *)&their_addr_A, &addr_len_A)) == -1) { 
                    perror("recvfrom");
                    exit(1); 
            }
            // printf("reply_A: %s\n",reply_A);
            
            
        }

        // receive update completed message from B
        if(matched_B_size>0){
            if ((numbytes = recvfrom(sockfd_B, reply_B, MAXDATASIZE-1 , 0, 
                (struct sockaddr *)&their_addr_B, &addr_len_B)) == -1) { 
                    perror("recvfrom");
                    exit(1); 
            }
            // printf("reply_B: %s\n",reply_B);

        }

        if(strcmp(reply_A, "1")==0){
            *update_res_A = 1;
        }else{*update_res_A = 0;}
        if(strcmp(reply_B, "1")==0){
            *update_res_B = 1;
        }else{*update_res_B = 0;}

        close(sockfd_A);
        close(sockfd_B);
    
        return numbytes;

}
