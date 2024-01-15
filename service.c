#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <sys/types.h>

#include "util.h"

typedef struct Node
{
    int* fd;
    struct Node* prev;
    struct Node* next;
}Node;

#define ERR_EXIT(s) perror(s), exit(errno);

static unsigned long secret;
static char service_name[MAX_SERVICE_NAME_LEN];

static inline bool is_manager() {
    return strcmp(service_name, "Manager") == 0;
}

void print_not_exist(char *service_name) {
    printf("%s doesn't exist\n", service_name);
}

void print_receive_command(char *service_name, char *cmd) {
    printf("%s has received %s\n", service_name, cmd);
}

void print_spawn(char *parent_name, char *child_name) {
    printf("%s has spawned a new service %s\n", parent_name, child_name);
}

void print_kill(char *target_name, int decendents_num) {
    printf("%s and %d child services are killed\n", target_name, decendents_num);
}

void print_acquire_secret(char *service_a, char *service_b, unsigned long secret) {
    printf("%s has acquired a new secret from %s, value: %lu\n", service_a, service_b, secret);
}

void print_exchange(char *service_a, char *service_b) {
    printf("%s and %s have exchanged their secrets\n", service_a, service_b);
}

void  exchange_msg(char *service_a, char *service_b, long unsigned int *secret, int router){
    umask(0);
    char file_name[MAX_FIFO_NAME_LEN];
    bzero(file_name, MAX_FIFO_NAME_LEN);
    sprintf(file_name, "%s_to_%s.fifo", service_b, service_a);
    // if(mkfifo(file_name, 0777) == -1){
    //     fprintf(stderr, "file alreay exists\n");
    // }
    int fd = open(file_name, O_RDWR);
    char msg_secret[512];
    int service_secret = *secret;
    memset(msg_secret, '\0', 512);
    fprintf(stderr, "%d handle ing \n",getpid());
    if(router == 1){
        sprintf(msg_secret,"%d\n", service_secret);
        int error_check = write(fd, msg_secret, 512);
        fprintf(stderr, "write by second service %d\n", error_check);
    }
    else{
        int error_check = read(fd, msg_secret, 512); 
        fprintf(stderr, "read by first service %d\n", error_check);
        int tmp_secret = atoi(msg_secret);
        long unsigned int  orig_secret = *secret;
        *secret = tmp_secret;
        print_acquire_secret(service_a, service_b, tmp_secret);
        //print_acquire_secret(service_b, service_a, orig_secret);
    }
    char file_name2[MAX_FIFO_NAME_LEN];
    bzero(file_name2 ,MAX_FIFO_NAME_LEN);
    sprintf(file_name2, "%s_to_%s.fifo", service_a, service_b);
    // if(mkfifo(file_name2, 0777) == -1){
    //     fprintf(stderr, "file alreay exists\n");
    // }
    //printf("stage 2 %s\n", file_name2);
    int fd2 = open(file_name2, O_RDWR);
    if(router == 1){
        fprintf(stderr, "stage 2: %d here second doing %d\n", getpid(), fd2);
        int error_check = read(fd2, msg_secret, 512); 
        fprintf(stderr, "stage 2: read by second service %d\n", error_check);
        int tmp_secret = atoi(msg_secret);    
        *secret = tmp_secret;
        print_acquire_secret(service_b, service_a, tmp_secret);
        write(fd, "ACK\0", 4);
    }
    else{
        fprintf(stderr, "stage 2: %d here first doing %d\n", getpid(), fd2);
        bzero(msg_secret, 512);
        sprintf(msg_secret,"%d\n", service_secret);
        int error_check = write(fd2, msg_secret, 512);
        fprintf(stderr, "stage 2: write by first service %d\n", error_check);
        char handshake[512];
        bzero(handshake, 512);
        read(fd , handshake , 512);
    }

    close(fd);
    close(fd2);
    if(unlink(file_name) == -1){
        fprintf(stderr, "file alreay remove\n");
    }
    if(unlink(file_name2) == -1){
        fprintf(stderr, "file alreay remove\n");
    }
    return;
}

service* refillNode(int pid, int read_fd, int write_fd, char* name, service* prev){
    service* result = (service*) malloc(sizeof(service));
    result->pid = pid;
    result->read_fd =read_fd;
    result->write_fd = write_fd;
    result->prev = prev;
    strncpy(result->name, name, MAX_SERVICE_NAME_LEN);
    result->next = NULL;
    return result;
}

int getSecert(int pid){
    srand(pid);
    return rand();
}
void deleteNode(service* target){
    if(target->next){
        service* prev_node = target->prev;
        service* next_node = target->next;
        prev_node->next = next_node;
        next_node->prev = prev_node;
    }
    else
    {
        service* prev_node = target->prev;
        prev_node->next = NULL;
    }
    return;
}

void traversalNode(service* target){
    while(target){
        printf("%s ", target->name);
        target = target->next;
    }
    printf("\n");
    return;
}

int main(int argc, char *argv[]) {
    pid_t pid = getpid();        

    if (argc != 2) {
        fprintf(stderr, "Usage: ./service [service_name]\n");
        return 0;
    }


    secret = getSecert(pid);
    /* 
     * prevent buffered I/O
     * equivalent to fflush() after each stdout
     */
    setvbuf(stdout, NULL, _IONBF, 0);
    strncpy(service_name, argv[1], MAX_SERVICE_NAME_LEN);
    printf("%s has been spawned, pid: %d, secret: %lu\n", service_name, pid, secret);


    char buf[512];
    bzero(buf, 512);
    service head = (service){ .pid = 0, .read_fd = -1, .write_fd = -1, .name = "foo", .prev = NULL, .next = NULL};
    //service* tail = & head;

/*********************************************************************Manager*************************************************************************************/
    if(strcmp(service_name, "Manager") == 0){
        char input_buffer[MAX_CMD_LEN];
        bzero(input_buffer ,MAX_CMD_LEN);
        while(fgets(input_buffer, MAX_CMD_LEN, stdin)){
            service* tmp_head = head.next;
            char copy_buffer[MAX_CMD_LEN];
            strncpy(copy_buffer, input_buffer, MAX_CMD_LEN);
            char* token = strtok(copy_buffer," ");
            char* hostname = strtok(NULL," ");
            char cmd_arg1[MAX_SERVICE_NAME_LEN];
            char cmd_arg2[MAX_SERVICE_NAME_LEN];
            // 先確認是不是這個host 不是就要 preorder traversal
            int input_cmd_size = strlen(input_buffer);
            input_buffer[input_cmd_size-1] = '\0';
            print_receive_command(service_name, input_buffer);
            input_buffer[input_cmd_size-1] = '\n';
            
            // 真正進入handling
            if(strcmp(token, "spawn") == 0){
                sscanf(input_buffer,"spawn %s %s", cmd_arg1, cmd_arg2);                
                if(strcmp(hostname, service_name) == 0){
                    int parent_write_fd[2];
                    int parent_read_fd[2];
                    pipe(parent_write_fd); // 這邊要error handle 哈哈
                    pipe(parent_read_fd); // 這邊要error handle 哈哈
                    int child_pid;
                    if((child_pid = fork()) > 0){
                        /*********parent*********/
                        close(parent_read_fd[1]);
                        close(parent_write_fd[0]);
                        //service child_service = (service){ .pid = child_pid, .read_fd = parent_read_fd[0], .write_fd = parent_write_fd[1], .name = "", .prev = NULL, .next = NULL};
                        //strncpy(child_service.name, cmd_arg2, MAX_SERVICE_NAME_LEN);
                        service* tail = & head;
                        while(tail && tail->next){
                            tail = tail->next;
                        }
                        // if(tail->next)
                        //     fprintf(stderr,"DEBUG %s %s\n",tail->name, tail->next->name);
                        // else
                        //     fprintf(stderr,"DEBUG %s\n",tail->name);
                        service* child_service = refillNode(pid, parent_read_fd[0], parent_write_fd[1], cmd_arg2, tail);
                        tail -> next = child_service;
                        tail = tail->next;
                        // if(tail->next)
                        //     fprintf(stderr,"AFTER DEBUG %s %s\n",tail->name, tail->next->name);
                        // else
                        //     fprintf(stderr,"AFTER DEBUG %s\n",tail->name);
                        //fprintf(stderr,"traversal Node\n");
                        //traversalNode(head.next);
                        char msg[MAX_CMD_LEN];
                        bzero(msg ,MAX_CMD_LEN);
                        int tmptmp = read(tail->read_fd, msg, MAX_CMD_LEN);
                        if(strcmp(msg, "ACK") == 0)
                            print_spawn(service_name, cmd_arg2);
                    }
                    else{
                        /*********child*********/
                        service* remove_fd = head.next;
                        while (remove_fd)
                        {
                            close(remove_fd->read_fd);
                            close(remove_fd->write_fd);
                            remove_fd = remove_fd->next;
                        }
                        
                        close(parent_read_fd[0]);
                        close(parent_write_fd[1]);
                        dup2(parent_read_fd[1], PARENT_WRITE_FD);
                        dup2(parent_write_fd[0], PARENT_READ_FD);
                        if(parent_read_fd[1] != 3 && parent_read_fd[1] != 4)
                            close(parent_read_fd[1]); // 3 4已經redirect過去
                        if(parent_write_fd[0] != 3 && parent_write_fd[0] != 4)
                            close(parent_write_fd[0]);
                        execl("service", "service", cmd_arg2, NULL);
                    }
                }
                else{
                    // 不是這個host preorder traversal
                    int key = 1;
                    while(tmp_head){
#if  DEBUG                 
                    fprintf(stderr,"this is Manager tempt to find other host %s\n", tmp_head->name);
#endif
                        write(tmp_head->write_fd, input_buffer, MAX_CMD_LEN);
                        char msg[MAX_CMD_LEN];
                        bzero(msg, MAX_CMD_LEN);
                        read(tmp_head->read_fd, msg, MAX_CMD_LEN);
                        if(strcmp(msg, "ACK") == 0) {
                            key = 0;
                            break;
                        }
                        else{
                            tmp_head = tmp_head->next;
                        }
                    }
                    if(key){
                        print_not_exist(cmd_arg1);
                    }
                }
            }
            else if(strcmp(token, "exchange") == 0){
                /********* Manager : exchange *********/
                umask(0);
                sscanf(input_buffer, "exchange %s %s", cmd_arg1, cmd_arg2);
                char fifo_1[MAX_FIFO_NAME_LEN];
                char fifo_2[MAX_FIFO_NAME_LEN];
                sprintf(fifo_1, "%s_to_%s.fifo", cmd_arg1, cmd_arg2);
                sprintf(fifo_2, "%s_to_%s.fifo", cmd_arg2, cmd_arg1);
                mkfifo(fifo_1, 0777);
                mkfifo(fifo_2, 0777);
                int ex_result = 0;
                if(strcmp(cmd_arg1, service_name) == 0){ //exchange有含Manager
                    ex_result = 1; 
                    tmp_head = head.next;
                    char ex_msg[MAX_CMD_LEN];
                    char res_msg[MAX_CMD_LEN];
                    while(tmp_head){
                        bzero(ex_msg, MAX_CMD_LEN);
                        bzero(res_msg, MAX_CMD_LEN);
                        sprintf(ex_msg, "exchange %s %s %d\n", cmd_arg1, cmd_arg2, ex_result);
                        write(tmp_head->write_fd, ex_msg, MAX_CMD_LEN);
                        read(tmp_head->read_fd, res_msg, MAX_CMD_LEN);
                        ex_result = atoi(res_msg);
                        fprintf(stderr, "recevie from %s and get %d\n", tmp_head->name, ex_result);
                        if(ex_result == 2)
                            break;            
                        tmp_head = tmp_head->next;
                    }
                    exchange_msg(cmd_arg1, cmd_arg2, &secret, 0);
                    //print_acquire_secret(cmd_arg1, cmd_arg2, secret);
                    // unlink(fifo_1);
                    // unlink(fifo_2);
                    while(access(fifo_1,F_OK) == 0){
                        // fprintf(stderr, "fifo_1 exists\n");
                    }
                    while(access(fifo_2,F_OK) == 0){
                        // fprintf(stderr, "fifo_2 exists\n");
                    }
                    print_exchange(cmd_arg1, cmd_arg2);
                }
                else if(strcmp(cmd_arg2, service_name) == 0){
                    ex_result = 1; 
                    tmp_head = head.next;
                    char ex_msg[MAX_CMD_LEN];
                    char res_msg[MAX_CMD_LEN];
                    while(tmp_head){
                        bzero(ex_msg, MAX_CMD_LEN);
                        bzero(res_msg, MAX_CMD_LEN);
                        sprintf(ex_msg, "exchange %s %s %d\n", cmd_arg1, cmd_arg2, ex_result);
                        write(tmp_head->write_fd, ex_msg, MAX_CMD_LEN);
                        read(tmp_head->read_fd, res_msg, MAX_CMD_LEN);
                        ex_result = atoi(res_msg);
                        if(ex_result == 2)
                            break;
                        tmp_head = tmp_head->next;
                    }
                    exchange_msg(cmd_arg1, cmd_arg2, &secret, 1);
                    //print_acquire_secret(cmd_arg2, cmd_arg1, secret);
                    // unlink(fifo_1);
                    // unlink(fifo_2);
                    while(access(fifo_1,F_OK) == 0){ //busy waiting !
                        // fprintf(stderr, "fifo_1 exists\n");
                    }
                    while(access(fifo_2,F_OK) == 0){
                        // fprintf(stderr, "fifo_2 exists\n");
                    }
                    print_exchange(cmd_arg1, cmd_arg2);
                }
                else{
                    tmp_head = head.next;
                    char ex_msg[MAX_CMD_LEN];
                    char res_msg[MAX_CMD_LEN];
                    while(tmp_head){
                        bzero(ex_msg ,MAX_CMD_LEN);
                        bzero(res_msg ,MAX_CMD_LEN);
                        sprintf(ex_msg, "exchange %s %s %d\n", cmd_arg1, cmd_arg2, ex_result);
                        write(tmp_head->write_fd, ex_msg, MAX_CMD_LEN);
                        read(tmp_head->read_fd, res_msg, MAX_CMD_LEN);
                        ex_result = atoi(res_msg);
                        fprintf(stderr,"EXCHANGE receive from %s get %d\n", tmp_head->name, ex_result);
                        if(ex_result == 2)
                            break;
                        tmp_head = tmp_head->next;
                    }
                    //unlink(fifo_1); //unlink 交給他們來做好了因為會發生race condition
                    //unlink(fifo_2);
                    while(access(fifo_1,F_OK) == 0){
                        // fprintf(stderr, "fifo_1 exists\n");
                    }
                    while(access(fifo_2,F_OK) == 0){
                        // fprintf(stderr, "fifo_2 exists\n");
                    }
                    print_exchange(cmd_arg1, cmd_arg2);
                }
            }
            else{
                /*********Manager : kill*********/
                sscanf(input_buffer,"kill %s", cmd_arg1);
                //printf("%s %s %d\n",cmd_arg1, service_name, strcmp(cmd_arg1, service_name));
                //sleep(100);
                // 如果是這個host要被砍的話
                if(strcmp(cmd_arg1, service_name) == 0){
                    //應該是 沿著你的linked list 依序送 kill msg.
                    // char kill_msg[512];
                    // 去讀結果
                    char res_msg[MAX_CMD_LEN];
                    int result = 0;
                    tmp_head = head.next;
                    while(tmp_head){
                        // 換成他底下child 依序去砍
                        //sprintf(kill_msg,"kill %s\n",tmp_head->name);
                        bzero(res_msg ,MAX_CMD_LEN);
                        write(tmp_head->write_fd,"DIE\0",4);
                        read(tmp_head->read_fd, res_msg, MAX_CMD_LEN);
                        wait(NULL);
                        result += atoi(res_msg);
                        // fprintf(stderr,"send msg %s", kill_msg);
                        fprintf(stderr,"RECEIVE FROM %s and get num %d\n", tmp_head->name, atoi(res_msg));
                        // traversalNode(head.next);
                        tmp_head = tmp_head->next;
                    }
                    // sprintf(res_msg,"%d\n", result);
                    // write(PARENT_WRITE_FD, res_msg, 512); // 用512 抖抖的
                    print_kill(cmd_arg1, result);
                    exit(0);
                }
                // 不是這個service 要被killed 繼續 preorder traversal
                else{
                    //去找 找不到要回0
                    char res_msg[MAX_CMD_LEN];
                    int result = 0;
                    while(tmp_head){
                        bzero(res_msg, MAX_CMD_LEN);
                        write(tmp_head->write_fd, input_buffer, MAX_CMD_LEN);
                        read(tmp_head->read_fd, res_msg, MAX_CMD_LEN); //等回傳結果.
                        result += atoi(res_msg);
                        fprintf(stderr,"KILL: receive from %s and get num %d\n", tmp_head->name, atoi(res_msg));
                        if(strcmp(tmp_head->name, cmd_arg1) == 0){
                            wait(NULL);
                            close(tmp_head->read_fd);
                            close(tmp_head->write_fd);
                            deleteNode(tmp_head);
                            free(tmp_head);
                            break;
                        }
                        else if(result > 0){
                            break;
                        }
                        tmp_head = tmp_head->next;
                    }
                    if(result == 0){
                        print_not_exist(cmd_arg1);
                    }
                    else{
                        print_kill(cmd_arg1, result-1);
                    }
                }
            }

        }
    }
/*********************************************************************Others*************************************************************************************/
    else{  // Manager 以外的server
        write(PARENT_WRITE_FD,"ACK\0",4);
        char input_buffer[MAX_CMD_LEN];
        bzero(input_buffer, MAX_CMD_LEN);
        while(read(PARENT_READ_FD, input_buffer, MAX_CMD_LEN)){
            if(strcmp(input_buffer,"DIE") == 0){
                char res_msg[MAX_CMD_LEN];
                int result = 0;
                service* tmp_head = head.next;
                while(tmp_head){
                    // 換成他底下child 依序去砍
                    //sprintf(kill_msg,"kill %s\n",tmp_head->name);
                    write(tmp_head->write_fd,"DIE\0",4);
                    read(tmp_head->read_fd, res_msg, MAX_CMD_LEN);
                    wait(NULL);
                    result += atoi(res_msg);
                    tmp_head = tmp_head->next;
                }
                result += 1;
                //fprintf(stderr, "this is DIE node %s send %d\n", service_name, result);
                sprintf(res_msg,"%d\n", result);
                write(PARENT_WRITE_FD, res_msg, MAX_CMD_LEN); // 用512 抖抖的
                exit(0);
            }
            service* tmp_head = head.next;
            char copy_buffer[MAX_CMD_LEN];
            strncpy(copy_buffer, input_buffer, MAX_CMD_LEN);
            char* token = strtok(copy_buffer," ");
            char* hostname = strtok(NULL," ");
            char cmd_arg1[MAX_SERVICE_NAME_LEN];
            char cmd_arg2[MAX_SERVICE_NAME_LEN];
            int input_cmd_size = strlen(input_buffer);
            input_buffer[input_cmd_size-1] = '\0';
            if(strcmp(token, "exchange") == 0){
                char ex_copy[MAX_CMD_LEN];
                strncpy(ex_copy, input_buffer, MAX_CMD_LEN);
                char arg1[MAX_SERVICE_NAME_LEN];
                char arg2 [MAX_SERVICE_NAME_LEN];
                int arg_int;
                sscanf(ex_copy, "exchange %s %s %d", arg1, arg2, &arg_int);
                bzero(ex_copy, MAX_CMD_LEN);
                sprintf(ex_copy, "exchange %s %s", arg1, arg2);
                print_receive_command(service_name, ex_copy);
            }
            else{
                print_receive_command(service_name, input_buffer);
            }
            
            input_buffer[input_cmd_size-1] = '\n';            
            // 真正進入handling
            /*****************************************Spawn Handle************************************************/
            if(strcmp(token, "spawn") == 0){
                // 先確認是不是這個host 不是就要 preorder traversal
                if(strcmp(hostname, service_name) == 0){
                    sscanf(input_buffer,"spawn %s %s", cmd_arg1, cmd_arg2);                
                    int parent_write_fd[2];
                    int parent_read_fd[2];
                    pipe(parent_write_fd); // 這邊要error handle 哈哈
                    pipe(parent_read_fd); // 這邊要error handle 哈哈
                    int child_pid;
                    if((child_pid = fork()) > 0){
                        close(parent_read_fd[1]);
                        close(parent_write_fd[0]);
                        //service child_service = (service){ .pid = child_pid, .read_fd = parent_read_fd[0], .write_fd = parent_write_fd[1], .name = "", .prev = NULL, .next = NULL};
                        //strncpy(child_service.name, cmd_arg2, MAX_SERVICE_NAME_LEN);
                        service* tail = & head;
                        while(tail->next){
                            tail = tail->next;
                        }
                        service* child_service = refillNode(pid, parent_read_fd[0], parent_write_fd[1], cmd_arg2, tail);
                        tail -> next = child_service;
                        tail = tail->next;
#if DEBUG
                        fprintf(stderr, "get new service %s, now tail at %s\n",cmd_arg2, tail->name);
                        service* www_head  =  head.next;
                        while(www_head){
                            printf("%s ",www_head->name);
                            www_head = www_head->next;
                            sleep(5);
                        }
                        printf("\n");
#endif
                        char msg[MAX_CMD_LEN];
                        bzero(msg, MAX_CMD_LEN);
                        int tmptmp = read(tail->read_fd, msg, MAX_CMD_LEN);
                        if(strcmp(msg, "ACK") == 0)
                            print_spawn(service_name, cmd_arg2);
                        write(PARENT_WRITE_FD, "ACK\0", 4); //代表完成了 回傳給server 讓server去接收別的stdin cmd
                    }
                    else{
                        service* remove_fd = head.next;
                        while (remove_fd)
                        {
                            close(remove_fd->read_fd);
                            close(remove_fd->write_fd);
                            remove_fd = remove_fd->next;
                        }
                        close(parent_read_fd[0]);
                        close(parent_write_fd[1]);
                        dup2(parent_read_fd[1], PARENT_WRITE_FD);
                        dup2(parent_write_fd[0], PARENT_READ_FD);
                        if(parent_read_fd[1] != 3 && parent_read_fd[1] != 4)
                            close(parent_read_fd[1]);
                        if(parent_write_fd[0] != 3 && parent_write_fd[0] != 4)
                            close(parent_write_fd[0]);
                        execl("service", "service", cmd_arg2, NULL);
                    }
                }
                else{
                    // 不是這個host preorder traversal
                    int key = 1;
                    while(tmp_head){
#if DEBUG
                    fprintf(stderr,"this is host %s tempt to find other host %s\n",service_name ,tmp_head->name);
                    sleep(5);
#endif
                        write(tmp_head->write_fd, input_buffer, MAX_CMD_LEN);
                        char msg[MAX_CMD_LEN];
                        read(tmp_head->read_fd, msg, MAX_CMD_LEN);
                        if(strcmp(msg, "ACK") == 0) {
                            key = 0;
                            write(PARENT_WRITE_FD, "ACK\0", 4); //代表完成了 回傳給Parent 讓server去接收別的stdin cmd
                            break;
                        }
                        else{
                            tmp_head = tmp_head->next;
                        }
                    }
                    if(key){
                        write(PARENT_WRITE_FD, "NAK\0", 4);
                    }
                }
            }
            /******************OTHER***********************Spawn Handle above************************************************/
            else if(strcmp(token, "exchange") == 0){
                int ex_result = 0;
                sscanf(input_buffer,"exchange %s %s %d", cmd_arg1, cmd_arg2, &ex_result);
                if(strcmp(cmd_arg1, service_name) == 0){ //exchange有含Manager
                    ex_result += 1;
                    if(ex_result == 2){
                        char res_msg[MAX_CMD_LEN];
                        bzero(res_msg, MAX_CMD_LEN);
                        if(strcmp(cmd_arg2, "Manager") == 0){
                            sprintf(res_msg, "%d\n", ex_result);
                            write(PARENT_WRITE_FD, res_msg, MAX_CMD_LEN);                            
                            exchange_msg(cmd_arg1, cmd_arg2, &secret, 0);
                            //print_acquire_secret(cmd_arg1, cmd_arg2, secret);
                        }
                        else{
                            //print_acquire_secret(cmd_arg1, cmd_arg2, secret);
                            sprintf(res_msg, "%d\n", ex_result);
                            fprintf(stderr, "send %d by %s\n", ex_result, service_name);
                            write(PARENT_WRITE_FD, res_msg, MAX_CMD_LEN);
                            exchange_msg(cmd_arg1, cmd_arg2, &secret,  0);
                        }
                    }
                    else{
                        tmp_head = head.next;
                        char ex_msg[MAX_CMD_LEN];
                        char res_msg[MAX_CMD_LEN];
                        while(tmp_head){
                            bzero(ex_msg, MAX_CMD_LEN);
                            bzero(res_msg, MAX_CMD_LEN);
                            sprintf(ex_msg, "exchange %s %s %d\n", cmd_arg1, cmd_arg2, ex_result);
                            write(tmp_head->write_fd, ex_msg, MAX_CMD_LEN);
                            read(tmp_head->read_fd, res_msg, MAX_CMD_LEN);
                            ex_result = atoi(res_msg); // 嘗試看看應該要八 不是+= 而是 =
                            if(ex_result == 2)
                                break;            
                            tmp_head = tmp_head->next;
                        }
                        bzero(res_msg, MAX_CMD_LEN);
                        sprintf(res_msg, "%d\n", ex_result);
                        fprintf(stderr, "send %d by %s\n", ex_result, service_name);
                        write(PARENT_WRITE_FD, res_msg, MAX_CMD_LEN);
                        exchange_msg(cmd_arg1, cmd_arg2, &secret, 0);
                        //print_acquire_secret(cmd_arg1, cmd_arg2, secret);
                    }
                }
                else if(strcmp(cmd_arg2, service_name) == 0){
                    ex_result += 1;
                    if(ex_result == 2){
                        char res_msg[MAX_CMD_LEN];
                        bzero(res_msg, MAX_CMD_LEN);
                        if(strcmp(cmd_arg1, "Manager") == 0){
                            sprintf(res_msg, "%d\n", ex_result);
                            fprintf(stderr, "send %d by %s\n", ex_result, service_name);
                            write(PARENT_WRITE_FD, res_msg, MAX_CMD_LEN);
                            exchange_msg(cmd_arg1, cmd_arg2, &secret, 1);
                            //print_acquire_secret(cmd_arg2, cmd_arg1, secret);
                        }
                        else{
                            sprintf(res_msg, "%d\n", ex_result);
                            fprintf(stderr, "send %d by %s\n", ex_result, service_name);
                            write(PARENT_WRITE_FD, res_msg, MAX_CMD_LEN);
                            exchange_msg(cmd_arg1, cmd_arg2, &secret, 1);
                            //print_acquire_secret(cmd_arg2, cmd_arg1, secret);
                        }
                    }
                    else{
                        tmp_head = head.next;
                        char ex_msg[MAX_CMD_LEN];
                        char res_msg[MAX_CMD_LEN];
                        while(tmp_head){
                            bzero(ex_msg, MAX_CMD_LEN);
                            bzero(res_msg, MAX_CMD_LEN);
                            sprintf(ex_msg, "exchange %s %s %d\n", cmd_arg1, cmd_arg2, ex_result);
                            write(tmp_head->write_fd, ex_msg, MAX_CMD_LEN);
                            read(tmp_head->read_fd, res_msg, MAX_CMD_LEN);
                            ex_result = atoi(res_msg);
                            if(ex_result == 2){
                                break;
                            }
                                
                            tmp_head = tmp_head->next;
                        }
                        bzero(res_msg, MAX_CMD_LEN);
                        sprintf(res_msg, "%d\n", ex_result);
                        fprintf(stderr, "send %d by %s\n", ex_result, service_name);
                        write(PARENT_WRITE_FD, res_msg, MAX_CMD_LEN);
                        exchange_msg(cmd_arg1, cmd_arg2, &secret, 1);
                        //print_acquire_secret(cmd_arg2, cmd_arg1, secret);
                    }
                }
                else{
                    tmp_head = head.next;
                    char ex_msg[MAX_CMD_LEN];
                    char res_msg[MAX_CMD_LEN];
                    while(tmp_head){
                        bzero(ex_msg, MAX_CMD_LEN);
                        bzero(res_msg, MAX_CMD_LEN);
                        sprintf(ex_msg, "exchange %s %s %d\n", cmd_arg1, cmd_arg2, ex_result);
                        write(tmp_head->write_fd, ex_msg, MAX_CMD_LEN);
                        read(tmp_head->read_fd, res_msg, MAX_CMD_LEN);
                        ex_result = atoi(res_msg);
                        if(ex_result == 2)
                            break;
                        tmp_head = tmp_head->next;
                    }
                    bzero(res_msg, MAX_CMD_LEN);
                    sprintf(res_msg, "%d\n", ex_result);
                    write(PARENT_WRITE_FD, res_msg, MAX_CMD_LEN);
                }
            }
            else{
                sscanf(input_buffer,"kill %s", cmd_arg1);
                // 如果是這個host要被砍的話
#if DEBUG
                fprintf(stderr,"DEBUG MODE%s %s %d\n",cmd_arg1, service_name, strcmp(cmd_arg1, service_name));
#endif 
                if(strcmp(cmd_arg1, service_name) == 0){
                    //應該是 沿著你的linked list 依序送 kill msg.
                    // char kill_msg[512];
                    // 去讀結果
                    char res_msg[MAX_CMD_LEN];
                    int result = 0;
                    tmp_head = head.next;
                    while(tmp_head){
                        // 換成他底下child 依序去砍
                        //sprintf(kill_msg,"kill %s\n",tmp_head->name);
                        bzero(res_msg, MAX_CMD_LEN);
                        write(tmp_head->write_fd, "DIE\0", 4);
                        read(tmp_head->read_fd, res_msg, MAX_CMD_LEN);
                        wait(NULL);
                        close(tmp_head->read_fd);
                        close(tmp_head->write_fd);
                        result += atoi(res_msg);
                        tmp_head = tmp_head->next;
                    }
                    result += 1;
                    fprintf(stderr, "this is node %s send %d\n", service_name, result);
                    bzero(res_msg, MAX_CMD_LEN);
                    sprintf(res_msg,"%d\n", result);
                    write(PARENT_WRITE_FD, res_msg, MAX_CMD_LEN); // 用512 抖抖的
                    exit(0);
                }
                // 不是這個service 要被killed 繼續 preorder traversal
                else{
                    //去找 找不到要回0
                    char res_msg[MAX_CMD_LEN];
                    int result = 0;
                    while(tmp_head){
                        bzero(res_msg, MAX_CMD_LEN);
                        write(tmp_head->write_fd, input_buffer, MAX_CMD_LEN);
                        read(tmp_head->read_fd, res_msg, MAX_CMD_LEN); //等回傳結果.
                        result += atoi(res_msg);
                        if(strcmp(tmp_head->name, cmd_arg1) == 0){
                            wait(NULL);
                            close(tmp_head->read_fd);
                            close(tmp_head->write_fd);
                            deleteNode(tmp_head);
                            free(tmp_head);
                            break;
                        }
                        else if(result > 0){
                            break;
                        }
                        tmp_head = tmp_head->next;
                    }
                    bzero(res_msg, MAX_CMD_LEN);
                    sprintf(res_msg,"%d\n", result);
                    write(PARENT_WRITE_FD, res_msg, MAX_CMD_LEN); // 用512 抖抖的
                }
            }
        }
    }
    return 0;
}
/*********************************************************************Others*************************************************************************************/



