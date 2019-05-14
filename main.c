// https://blog.csdn.net/ruizeng88/article/details/6682028
#include <fcntl.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/socket.h>
#include <errno.h>
#include <netinet/in.h>
#include <string.h>
#include <stdio.h>
#include <malloc.h>
#include <liburing.h>

#define BUF_LEN 1028
#define SERVER_PORT 8080

static struct io_uring ring;

//定义好的html页面，实际情况下web server基本是从本地文件系统读取html文件
static char http_error_hdr[] = "HTTP/1.1 404 Not Found\r\nContent-type: text/html\r\n\r\n";
static char http_html_hdr[] = "HTTP/1.1 200 OK\r\nContent-type: text/html\r\n\r\n";
static char http_index_html[] =
"<html><head><title>Congrats!</title></head>"
"<body><h1>Welcome to our HTTP server demo!</h1>"
"<p>This is a just small test page.</body></html>\n";

typedef struct {
    int type; // 0 read, 1 write
    int sockfd;
    char *buf;
} OpData;

//解析到HTTP请求的文件后，发送本地文件系统中的文件
//这里，我们处理对index文件的请求，发送我们预定好的html文件
//呵呵，一切从简！
int http_send_file(char *filename, int sockfd) {
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    if (!strcmp(filename, "/")) {
        struct iovec ioves[2] = {
            {
                .iov_base = http_html_hdr,
                .iov_len = strlen(http_html_hdr),
            },
            {
                .iov_base = http_index_html,
                .iov_len = strlen(http_index_html),
            },
        };
        //通过write函数发送http响应报文；报文包括HTTP响应头和响应内容--HTML文件
        io_uring_prep_writev(sqe, sockfd, ioves, 2, 0);
    } else {
        // 文件未找到情况下发送404error响应
        printf("%s:file not find!\n", filename);
        struct iovec iov = {
            .iov_base = http_error_hdr,
            .iov_len = strlen(http_error_hdr),
        };
        io_uring_prep_writev(sqe, sockfd, &iov, 1, 0);
    }
    OpData* opData = (OpData *)malloc(sizeof(*opData));
    opData->type = 1;
    opData->sockfd = sockfd;
    opData->buf = NULL;
    io_uring_sqe_set_data(sqe, opData);
    io_uring_submit(&ring);
    return 0;
}

void readReq(int sockfd) {
    struct iovec iov = {
        .iov_base = (char *)malloc(BUF_LEN * sizeof(char)),
        .iov_len = BUF_LEN,
    };
    struct io_uring_sqe* sqe = io_uring_get_sqe(&ring);
    OpData* opData = (OpData *)malloc(sizeof(*opData));
    opData->type = 0;
    opData->sockfd = sockfd;
    opData->buf = (char *)iov.iov_base;
    io_uring_prep_readv(sqe, sockfd, &iov, 1, 0);
    io_uring_sqe_set_data(sqe, opData); // 这个东西必须在prep语句后面
    io_uring_submit(&ring);
}

//HTTP请求解析
void serve(int sockfd, char* buf) {
    if (!strncmp(buf, "GET", 3)) {
        char *file = buf + 4;
        char *space = strchr(file, ' ');
        *space = '\0';
        printf("received request: %s\n", file);
        http_send_file(file, sockfd);
    } else {
        //其他HTTP请求处理，如POST，HEAD等 。这里我们只处理GET
        printf("unsupported request: %s\n", buf);
    }
}

int main() {
    if (io_uring_queue_init(32, &ring, 0) < 0) {
        perror("queue_init");
        return 1;
    }
    //建立TCP套接字
    int sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0){
        perror("socket creation failed!\n");
        return 1;
    }

    fcntl(sockfd, F_SETFL, fcntl(sockfd, F_GETFL, 0) | O_NONBLOCK);

    struct sockaddr_in addr;
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    //这里要注意，端口号一定要使用htons先转化为网络字节序，否则绑定的实际端口
    //可能和你需要的不同
    addr.sin_port = htons(SERVER_PORT);
    addr.sin_addr.s_addr = INADDR_ANY;
    if (bind(sockfd, (struct sockaddr *)&addr, sizeof(struct sockaddr_in))) {
        perror("socket binding failed!\n");
        return 1;
    }
    listen(sockfd, 128);
    for (;;) {
        //不间断接收HTTP请求并处理
        int newfd = accept(sockfd, NULL, NULL);
        if (newfd < 0) {
            // respond
            struct io_uring_cqe* cqe;
            if (io_uring_peek_cqe(&ring, &cqe) < 0) {
                perror("peek_cqe");
                return -1;
            }
            if (cqe) {
                OpData* opData = (OpData *)io_uring_cqe_get_data(cqe);
                if (cqe->res <= 0) {
                    perror("cqe");
                    io_uring_cqe_seen(&ring, cqe);
                    close(opData->sockfd);
                    continue;
                }
                switch (opData->type) {
                case 0:
                    printf("%d bytes read\n", cqe->res);
                    serve(opData->sockfd, opData->buf);
                    free(opData->buf);
                    break;
                case 1:
                    printf("%d bytes written\n", cqe->res);
                    close(opData->sockfd);
                    break;
                }
                io_uring_cqe_seen(&ring, cqe);
                free(opData);
            }
        } else {
            readReq(newfd);
        }
        __asm__ ( "pause" );
    }
}
