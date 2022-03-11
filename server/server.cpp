#include <iostream>
#include <stdio.h>
#include <stdlib.h>
#include <stdbool.h>
#include <string>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <sys/epoll.h>
#include <sys/stat.h>
#include <sys/ioctl.h>
#include <linux/fs.h>
#include <linux/types.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>
#include <mutex>
#define STORK_SOCKET "/home/phucanh/Training code/Socket training/socket/socket"
#define STORK_SOCKET_LEN 58

struct cmd_req {
    int data;
};

struct cmd_res {
    int data;
};

struct pthread_arg_t {
    int client_socket;
};
static pthread_t main_thread;
std::mutex mtx;
static int wait_for_select(int client_socket, int operation) { //operation: 0 read, 1 write
    int epollfdr;
    struct epoll_event ev;
    struct epoll_event events[1];

    epollfdr = epoll_create(1);
    if(epollfdr < 0) {
        std::cout << "Epoll create fail" << std::endl;
        return -1;
    }

    if(operation == 0) {
        ev.events = EPOLLIN;
    } else {
        ev.events = EPOLLOUT;
    }
    ev.data.fd = client_socket;
    if(epoll_ctl(epollfdr, EPOLL_CTL_ADD, client_socket, &ev) < 0) {
        std::cout << "Epoll ctl add error" << std::endl;
        close(epollfdr);
        return -1;
    }

    const int select_result = epoll_wait(epollfdr, events, 1, 5000);

    if(select_result <= 0) {
        std::cout << "Waiting time has expired. Close connection." << std::endl;
        close(epollfdr);
        return -1;
    }
    std::cout << "Server epoll select ok " << std::endl;
    if(epoll_ctl(epollfdr, EPOLL_CTL_DEL, client_socket, &ev) < 0) {
        std::cout << "Epoll ctl delete error" << std::endl;
        close(epollfdr);
        return -1;
    }
    close(epollfdr);
    return 0;
}

void *exec_stork(void *arg) {
    mtx.lock();
    std::cout << "Mutex lock on " << std::endl;
    int ret=-1;
    pthread_arg_t *pthread_arg = (pthread_arg_t *) arg;
    int new_client_socket = pthread_arg->client_socket;
    struct cmd_req req_buf;
    struct cmd_res res_buf;

    memset((void*)&res_buf, 0, sizeof(res_buf));
    memset((void*)&req_buf, 0, sizeof(req_buf));
    std::cout << "Exec_stork run " << std::endl;
    ret = wait_for_select(new_client_socket, 0);
    if(ret==-1) {
        std::cout << "Waiting time has expired" << std::endl;
    }

    const int req_len = sizeof(struct cmd_req);
    std::cout << "Request data before read: " << req_buf.data << std::endl;
    if(read(new_client_socket, &req_buf, req_len) != req_len) {
        std::cout << "Server read fail " << std::endl;
        res_buf.data = -1;
        goto send_reply;
    }
    std::cout << "Server read ok " << std::endl;
    std::cout << "Request data : " << req_buf.data << std::endl;
    res_buf.data = 1;
send_reply:
    ret = wait_for_select(new_client_socket,1);
    if(ret == -1) {
        std::cout << "Waiting time has expired" << std::endl;
    }

    const int res_buf_len = sizeof(struct cmd_res);
    std::cout << "res_buf_len : " << res_buf_len << std::endl;
    std::cout << "res_buf data : " << res_buf.data << std::endl;
    const int written = send(new_client_socket, &res_buf, res_buf_len, MSG_EOR | MSG_NOSIGNAL);
    std::cout << "Written : " << written << std::endl;
    if(written != res_buf_len) {
         std::cout << "Failed to send reply :, " << errno << "str : " << strerror(errno) << std::endl;
    }
    std::cout << "Server close socket" << std::endl;
    close(new_client_socket);

exit_thr:
    mtx.unlock();
    std::cout << "Mutex lock off " << std::endl;
    std::cout << "Thread exit" << std::endl;
    pthread_exit(0);
    return NULL;
}

int main(int argc, char* argv[]) {
    struct sockaddr_un client_addr;
    struct sockaddr_un server_addr;
    int client_socket = 0;
    int server_socket = 0;
    pthread_arg_t *pthread_arg;
    unsigned int client_len, server_len;
    int ret=0;
    
    unlink(STORK_SOCKET);
    std::cout << "Unlink Socket" << std::endl;
    server_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if(server_socket < 0) {
        std::cout << "Server socket failed: " << strerror(errno) << std::endl;
    }

    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, STORK_SOCKET, STORK_SOCKET_LEN);
    server_len = sizeof(server_addr);

    ret = bind(server_socket, (struct sockaddr *)&server_addr, server_len);
    if(ret == -1) {
         std::cout << "Server bind fail : " << strerror(errno) << std::endl;
         goto error;
    }
    std::cout << "Bind ok" << std::endl;

    ret = listen(server_socket,0);
    if(ret == -1) {
        std::cout << "Server listen fail : " << strerror(errno) << std::endl;
        goto error;
    }
    std::cout << "Listen ok" << std::endl;

    while(1) {
        pthread_arg = (pthread_arg_t *)malloc(sizeof *pthread_arg);
        if(!pthread_arg) {
            std::cout << "Thread create fail" << std::endl;
            continue;
        }
        std::cout << "Thread init ok" << std::endl;
        client_len = sizeof(client_addr);
        client_socket = accept(server_socket, (struct sockaddr *)&client_addr, &client_len);
        if(client_socket == -1) {
            if(errno == EBADF || errno == EINVAL) {
                std::cout << "Server fail" << std::endl;
                free(pthread_arg);
                break;
            }
            std::cout << "Accept fail" << std::endl;
            free(pthread_arg);
            continue;
        }

        pthread_arg->client_socket=client_socket;
        if (pthread_create(&main_thread, NULL, exec_stork, (void*)pthread_arg) !=0) {
            std::cout << "Can't create thread" << std::endl;
            free(pthread_arg);
            continue;
        }
        std::cout << "Thread create ok" << std::endl;
        std::cout << "Thread id : " << pthread_self() << std::endl;
        pthread_join(main_thread, NULL);
        std::cout << "Thread join ok" << std::endl;
    }

error:
    std::cout << "Critical error" << std::endl;
    if(server_socket >=0) {
        close(server_socket);
    }
}
