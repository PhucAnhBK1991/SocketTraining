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
#include <linux/fs.h>
#include <linux/types.h>
#include <sys/ioctl.h>
#include <pthread.h>
#include <errno.h>
#include <stdlib.h>
#include <fcntl.h>
#include <time.h>
#include <unistd.h>

#define STORK_SOCKET "/home/phucanh/Training code/Socket training/socket/socket"
#define STORK_SOCKET_LEN 58
#define RETRY_COUNT_MAX 20

struct cmd_req {
    int data;
};

struct cmd_res {
    int data;
};

static int connect_socket(int client_socket, struct sockaddr *server_addr) {
    int ret = -1;
    int flags;
    int retry_count = 0;

    if((flags = fcntl(client_socket, F_GETFL, NULL)) < 0) {
        std::cout << "F_GETFL error" << std::endl;
        return -1;
    }

    // Set non-blocking mode socket
    if(fcntl(client_socket, F_SETFL, flags | O_NONBLOCK) < 0) {
        std::cout << "F_SETFL error" << std::endl;
        return -1;
    }
    std::cout << "Set blocking mode ok" << std::endl;
    do {
        if((ret = connect(client_socket, server_addr, sizeof(struct sockaddr_un)))== 0) break;
        std::cout << "Fail to connect to server, retry : " << retry_count << std::endl;

        if ((errno == EWOULDBLOCK || errno == EAGAIN)) {
            usleep(100*1000);
            retry_count++;
        } else {
            return -1;
        }
    } while (retry_count <= RETRY_COUNT_MAX);

    // Change to blocking mode
    fcntl(client_socket, F_SETFL, flags);

    if(retry_count == 20)
        return -1;

    return ret;
}

static int init_client(void) {
    int ret;
    std::cout << "Client init" << std::endl;
    struct sockaddr_un server_addr;
    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sun_family = AF_UNIX;
    strncpy(server_addr.sun_path, STORK_SOCKET, STORK_SOCKET_LEN);

    int client_socket = socket(AF_UNIX, SOCK_STREAM, 0);
    if(client_socket == -1) {
        std::cout << "Fail create socket : %s" << strerror(errno) << std::endl;
    }

    ret = connect_socket(client_socket, (struct sockaddr *)&server_addr);
    if(ret < 0) {
        std::cout << "Error connect" << std::endl;
        if(0 != close(client_socket)) {
            std::cout << "Error in close socket" << std::endl;
        }
        return -1;
    }
    std::cout << "Client connect ok" << std::endl;
    return client_socket;
}

static int send_request(int client_socket, struct cmd_req* req_buf) {
    if(send(client_socket, req_buf, sizeof(struct cmd_req), MSG_EOR | MSG_NOSIGNAL) < 0) {
        std::cout << "Error write function " << strerror(errno) << std::endl;
        return -1;
    }
    std::cout << "Client send ok" << std::endl;
    return 0;
}

static int receive_reply(int client_socket, struct cmd_res *res_buf) {
    int epollfdr;
    struct epoll_event ev;
    struct epoll_event events[1];

    int r = -1;
    int ret = 0;

    epollfdr = epoll_create(1);
    if(epollfdr < 0) {
        std::cout << "Epoll create failed" << std::endl;
        ret = -1;
        goto receive_exit;
    }

    ev.events = EPOLLIN;
    ev.data.fd = client_socket;

    if(epoll_ctl(epollfdr, EPOLL_CTL_ADD, client_socket, &ev) < 0) {
        std::cout << "Client error epoll add" << std::endl;
        ret = -1;
        goto receive_exit;
    }
    std::cout << "Epoll add ok" << std::endl;
    while(1) {
        r=epoll_wait(epollfdr, events, 1, 5000);
        std::cout << "r : " << r << std::endl;
        if(r==-1 && errno !=EINTR) {
            std::cout << "Epoll wait failed" << std::endl;
            ret = -1;
            goto receive_exit;
        } else if(r == -1 && errno == EINTR) {
            std::cout << "Epoll wait catches the interrupted" << std::endl;
            continue;
        } else if(r) {
            std::cout << "Read client start" << std::endl;
            if(read(client_socket, res_buf, sizeof(struct cmd_res)) < 0) {
                std::cout << "Read function error" << std::endl;
                goto receive_exit;
            }
            //std::cout << "Wait func res_buf.data : " << res_buf->data << std::endl;
            ret = 0;
            goto receive_exit;
        } else {
            std::cout << "Epoll wait time out" << std::endl;
            ret = -1;
            goto receive_exit;
        }
    }
receive_exit:
    if(epoll_ctl(epollfdr, EPOLL_CTL_DEL, client_socket, &ev) < 0) {
        std::cout << "Client error epoll delete" << std::endl;
        ret = -1;
    }
    close(epollfdr);
    return ret;
}

int exec_command(struct cmd_req *req_buf, struct cmd_res *res_buf) {
    int ret = 0;
    int client_socket;
    std::cout << "Client execute command" << std::endl;
    /*prepare socket*/
    client_socket = init_client();
    if((ret = send_request(client_socket, req_buf)) < 0) {
        std::cout << "Fail to send client socket" << std::endl;
        goto exec_exit;
    }
    std::cout << "send_request ok" << std::endl;
    if((ret = receive_reply(client_socket,res_buf)) < 0) {
        std::cout << "Error reveive reply" << std::endl;
        goto exec_exit;
    }
    std::cout << "receive_reply ok" << std::endl;

exec_exit:
    if(client_socket >=0) {
        std::cout << "Closing client socket" << std::endl;
        if(0!= close(client_socket)) {
            std::cout << "Error when closing client socket" << std::endl;
            ret = -1;
        }
    }
    std::cout << "Exec_command ret : " << ret << std::endl;
    return ret;
}
int main() {
    struct cmd_req req_buf;
    struct cmd_res res_buf;
    int ret;
    memset(&req_buf, 0, sizeof(req_buf));
    memset(&res_buf, 0, sizeof(res_buf));
    req_buf.data = 69;
    ret = exec_command(&req_buf, &res_buf);
    if(ret < 0) {
        std::cout << "Client fail to execute command" << std::endl;
    }
    std::cout << "Result hear" << std::endl;
    std::cout << "Result took from server : " << res_buf.data << std::endl;

    return 0;
}
