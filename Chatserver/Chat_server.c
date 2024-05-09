#include <stdio.h>
#include <stdlib.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <stdbool.h>
#include <pthread.h>
#include <string.h>
#include <time.h>
#include <fcntl.h>

#define BUFFER_SIZE 1024

int PORT = 0;
int Max_no_of_clients = 0;
int timeout_time = 0;
int Count = 0;

pthread_mutex_t mutex = PTHREAD_MUTEX_INITIALIZER;
pthread_mutex_t mutex_send = PTHREAD_MUTEX_INITIALIZER;


struct client_list{
    char* name;
    int Client_sock_id;
    time_t last_msg_time;
    struct client_list* next;
};

struct client_list* head = NULL;
struct timeval timeout;

void reset_string(char *string){
    memset(string, 0, BUFFER_SIZE);
}

/*-------------------------------Adds the user to list---------------------------------------------*/
void add_user_to_list(struct client_list** head, char* message,int id){
    pthread_mutex_lock(&mutex);
    struct client_list* temp = (struct client_list*)malloc(sizeof(struct client_list));
    temp->name = message;
    temp->Client_sock_id = id;
    temp->last_msg_time = time(NULL);
    printf("%s has connected to the socket on socket number %d\n",message,id);
    temp->next = *head;
    *head = temp;
    pthread_mutex_unlock(&mutex);
    return;
}

/*-----------------Checks if a username already exists in the users_list---------------------------*/
bool username_check(char* message){
    pthread_mutex_lock(&mutex);
    struct client_list* temp = head;
    while(temp!=NULL){
        if(strcmp(temp->name,message) == 0){
            pthread_mutex_unlock(&mutex);
            return false;
        }
        temp = temp->next;
    }
    pthread_mutex_unlock(&mutex);
    return true;
}

/*-----This functions helps in sending the message to everyone except the person who sent it-------*/
void send_all_except_sender(int Client_sock_fd, char* reply){
    pthread_mutex_lock(&mutex_send);
    struct client_list* temp = head;
    while(temp!=NULL){
        if(temp->Client_sock_id != Client_sock_fd){
            send(temp->Client_sock_id,reply, strlen(reply), 0);
        }
        temp = temp->next;
    }
    pthread_mutex_unlock(&mutex_send);
    return;
}

/*---------------------  This function is used to delete the user from the list---------------------*/ 
void delete_user(int sock){
    pthread_mutex_lock(&mutex);
    if(head->Client_sock_id == sock){
        head = head->next;
    }
    else{
        struct client_list* temp = head;
        while(temp!=NULL && temp->next!=NULL){
            if(temp->next->Client_sock_id == sock){
                struct client_list* tmp = temp->next->next;
                temp->next = temp->next->next;
                break;
            }
            temp = temp->next;
        }
    }
    pthread_mutex_unlock(&mutex);
    return;
}

/*-------------------This function is used to close the socket notify other clients------------------*/
void close_this_sock_fd(int sock){
    pthread_mutex_lock(&mutex);
    Count--;
    //Checking if the socket is already closed 
    int flags = fcntl(sock, F_GETFL);
    if(flags != -1)close(sock);
    char* user = (char*)malloc(BUFFER_SIZE*sizeof(char));
    struct client_list* temp = head;
    while(temp!=NULL){
        if(temp->Client_sock_id == sock){
            user = strdup(temp->name);
            break;
        }
        temp = temp->next;
    }
    char* reply = (char*)malloc(BUFFER_SIZE*sizeof(char));
    /*------------Notifying all the other clients that this client has left the chatroom--------------*/
    reply = strcat(strcat(strcpy(reply,"Server: Client "),user)," left the chatroom");
    send_all_except_sender(sock,reply);
    pthread_mutex_unlock(&mutex);
    delete_user(sock);
    return;
}

int create_socket(){
    int sock = socket(AF_INET, SOCK_STREAM, 0);
    if (sock < 0){
        perror("[create_socket]: Socket creation error \n");
        exit(EXIT_FAILURE);
    }
    return sock;
}

/*-----------------This function handles the messages for client with socket-id = sock-----------------*/
void* handle_messages(void* sock){
    int Client_sock_fd = *(int*)sock;
    if(setsockopt(Client_sock_fd,SOL_SOCKET, SO_RCVTIMEO, (const char*)&timeout, sizeof(timeout))<0){
        close_this_sock_fd(Client_sock_fd);
        pthread_exit(NULL);
    }
    char* message = (char*)malloc(BUFFER_SIZE*sizeof(char));
    message = "Server: Enter your username:";
    send(Client_sock_fd, message, strlen(message), 0);
    message = (char*)malloc(BUFFER_SIZE*sizeof(char));
    int ok = 1;
    while(1){
        int l = read(Client_sock_fd, message, BUFFER_SIZE);
        if(l<1){
            /*----------------If client exited before(ctrl-c) before chossing a username---------------*/
            close(Client_sock_fd);
            ok = 0;break;
        }
        else{
            /*------------------------- Checking if the username already exists -----------------------*/
            if(username_check(message)){ok = 1;break;}
            else{
            /*-----------------If already exists request the client for another username---------------*/
                char* reply = (char*)malloc(BUFFER_SIZE*sizeof(char));
                reply = "Username already in use, Choose another";
                send(Client_sock_fd, reply, strlen(reply), 0);
                reset_string(message);
            }
        }
    }
    if(!ok){close(Client_sock_fd);pthread_exit(NULL);}
    char* user = (char*)malloc(BUFFER_SIZE*sizeof(char));
    strcpy(user,message);
    char* reply = (char*)malloc(BUFFER_SIZE*sizeof(char));
    /*-------------------------Sending the welcome message to the all the clients---------------------*/
    reply = strcat(strcat(strcpy(reply,"Server: Client "), message), " joined the chatroom");
    add_user_to_list(&head,strdup(message),Client_sock_fd);
    send_all_except_sender(Client_sock_fd,reply);
    reset_string(message);
    reset_string(reply);
    /*------------------------------Sending this message only to this client--------------------------*/
    reply = strcat(strcat(strcpy(reply,"Welcome "),user),"!!!!\n");
    send(Client_sock_fd,reply, strlen(reply), 0);
    reset_string(reply);
    strcpy(reply,"active users: ");
    struct client_list* temp = head;
    while(temp!=NULL){
        strcat(strcat(reply,temp->name),", ");
        temp = temp->next;
    }
    send(Client_sock_fd,reply, strlen(reply), 0);
    reset_string(reply);
    /*----This loop handles the messages sent and read by this client through the server's socket-----*/
    while(1){
        char* message = (char*)malloc(BUFFER_SIZE*sizeof(char));
        /*---------------If the client sends a message it is read in the server's socket--------------*/
        if(read(Client_sock_fd, message, BUFFER_SIZE) > 0){
            char* reply = (char*)malloc(BUFFER_SIZE*sizeof(char));
            struct client_list* temp = head;
            /*---------------Updating the last message time whenever a message is read----------------*/
            while(temp!=NULL){
                if(temp->Client_sock_id == Client_sock_fd){
                    user = temp->name;
                    temp->last_msg_time = time(NULL);
                    break;
                }
                temp = temp->next;
            }
            int breaks = 0;
            /*----------Handling the "\bye" condition immediately closes the Client socket------------*/
            if(strcmp(message,"\\bye")==0){
                close_this_sock_fd(Client_sock_fd);
                breaks = 1;
            }
            /*--------------Handling the "\list" condition which enlists all the users----------------*/
            else if(strcmp(message,"\\list")==0){
                temp = head;
                strcpy(reply,"active users: ");
                while(temp!=NULL){
                    strcat(strcat(reply,temp->name),", ");
                    temp = temp->next;
                }
                send(Client_sock_fd,reply, strlen(reply), 0);
                continue;
            }
            else{
                reply = strcat(strcat(strcpy(reply,user)," : "),message);
            }
            /*------This Functions sends the message entered by this client to all other clients------*/
            send_all_except_sender(Client_sock_fd,reply);
            reset_string(message);
            reset_string(reply);
            if(breaks){
                /*-------------------------Break when "\bye" is entered-------------------------------*/
                break;
            }
        }
        else{
            /*------------When no read is performed i.e, ctrl-C is executed by the Client-------------*/
            close_this_sock_fd(Client_sock_fd);
            break;
        }
    }
    pthread_exit(NULL);
}

int main(int argc,char** argv){

    if(argc < 4){
        perror("error: usage ./chat_server <PORT> <Max_connections> <Timeout_time>\n");
        return 0;
    }
    /*----------------------------------Taking input from command line--------------------------------*/
    Count = 0;
    PORT = atoi(argv[1]);
    Max_no_of_clients = atoi(argv[2]);
    /*-------------------------- Assigning a timeout time to handle timeout --------------------------*/
    timeout_time = atoi(argv[3]);
    timeout.tv_sec = timeout_time;
    timeout.tv_usec = 0;

    struct sockaddr_in serverAddr, clientAddr;
    serverAddr.sin_family = AF_INET;
    serverAddr.sin_port = htons(PORT);
    serverAddr.sin_addr.s_addr = INADDR_ANY;
    socklen_t clientAddrLen = sizeof(clientAddr);

    /*-----------------------------------------Creating a socket--------------------------------------*/
    int Server_sock_fd = create_socket();

    if(bind(Server_sock_fd, (struct sockaddr*)&serverAddr, sizeof(serverAddr)) != 0){
        perror("Server couldn't Bind\n");
        close(Server_sock_fd);
        return 0;
    }

    if (listen(Server_sock_fd, Max_no_of_clients) == -1) {
        perror("Error listening");
        close(Server_sock_fd);
        exit(EXIT_FAILURE);
    }

    printf("Server listening on PORT %d...\n", PORT);

    pthread_t handler[Max_no_of_clients];
    while(1){
        /*----------------------Checking if max connections are already accepted----------------------*/
        if(Count == Max_no_of_clients)continue;
        int Client_sock_fd = accept(Server_sock_fd, (struct sockaddr*)&clientAddr, &clientAddrLen);
        if(Client_sock_fd == -1){
            perror("Error accepting connection\n");
            continue;
        }
        Count++;
        /*--Assigning thread to handle the incoming and outgoing messages for each particular client--*/
        pthread_create(&handler[Client_sock_fd-4],NULL,handle_messages,&Client_sock_fd);
    }
    close(Server_sock_fd);
    return 0;
}