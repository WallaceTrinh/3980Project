#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <pthread.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define BASE_TEN 10
#define MAX_CONNECTIONS 1

// Conditional SOCK_CLOEXEC for compatibility
#ifndef SOCK_CLOEXEC
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-macros"
    #define SOCK_CLOEXEC 0
    #pragma GCC diagnostic pop
#endif

// Function Declarations
static void      setup_signal_handler(void);
static void      sigint_handler(int signum);
static in_port_t parse_in_port_t(const char *port_str);
static void      convert_address(const char *address, struct sockaddr_storage *addr);
static int       socket_create(int domain);
static void      socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void      socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port);
static void      socket_close(int sockfd);
static void     *handle_send(void *arg);
static void     *handle_receive(void *arg);

// Global variable to handle ext upon receiving SIGINT
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t exit_flag = 0;

int main(int argc, char *argv[])
{
    struct sockaddr_storage addr;
    struct sockaddr_storage client_addr;
    socklen_t               addr_size = sizeof(client_addr);
    int                     sockfd    = 0;    // Socket file descriptor
                                              //    int                     new_fd    = -1;    // Declare new_fd at the top, initialized to -1
    char     *ip;
    char     *port_str;
    in_port_t port;
    pthread_t send_thread;
    pthread_t receive_thread;
    int       mode = 0;    // 0 for server, 1 for client

    if(argc != 4)
    {
        fprintf(stderr, "Usage: %s [-a (for server) | -c (for client)] <ip address> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Determine mode
    if(strcmp(argv[1], "-a") == 0)
    {
        mode = 0;
    }
    else if(strcmp(argv[1], "-c") == 0)
    {
        mode = 1;
    }
    else
    {
        fprintf(stderr, "Invalid mode. Use -a for server or -c for client.\n");
        exit(EXIT_FAILURE);
    }

    ip       = argv[2];
    port_str = argv[3];
    port     = parse_in_port_t(port_str);
    convert_address(ip, &addr);
    sockfd = socket_create(addr.ss_family);

    if(mode == 0)
    {    // Server mode
        int client_fd;
        socket_bind(sockfd, &addr, port);
        listen(sockfd, MAX_CONNECTIONS);
        printf("Server listening on %s:%d\n", ip, port);

        client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);
        if(client_fd == -1)
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }

        socket_close(sockfd);
        sockfd = client_fd;
    }
    else
    {    // Client mode
        socket_connect(sockfd, &addr, port);
        printf("Client connected to %s:%d\n", ip, port);
    }

    setup_signal_handler();
    pthread_create(&send_thread, NULL, handle_send, &sockfd);
    pthread_create(&receive_thread, NULL, handle_receive, &sockfd);
    pthread_join(send_thread, NULL);
    pthread_join(receive_thread, NULL);
    socket_close(sockfd);
    return EXIT_SUCCESS;
}

// Parses the port number from string and checks for errors
static in_port_t parse_in_port_t(const char *str)
{
    char         *endptr;
    unsigned long parsed_value;

    errno        = 0;
    parsed_value = strtoul(str, &endptr, BASE_TEN);

    if(errno != 0 || *endptr != '\0' || parsed_value > UINT16_MAX)
    {
        fprintf(stderr, "Invalid port number.\n");
        exit(EXIT_FAILURE);
    }

    return (in_port_t)parsed_value;
}

// Converts the IP address string to network address
static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    memset(addr, 0, sizeof(*addr));

    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;
    }
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;
    }
    else
    {
        fprintf(stderr, "%s is not a valid IPv4 or IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

// Creates a socket with given domain
static int socket_create(int domain)
{
    // may need to rename sockfd to something else, but may not too
    int sockfd = socket(domain, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if(sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    return sockfd;
}

// Binds the socket to a specified address and port
static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    if(addr->ss_family == AF_INET)
    {
        ((struct sockaddr_in *)addr)->sin_port = htons(port);
    }
    else if(addr->ss_family == AF_INET6)
    {
        ((struct sockaddr_in6 *)addr)->sin6_port = htons(port);
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(bind(sockfd, (struct sockaddr *)addr, addr->ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)) == -1)
    {
        perror("Binding failed");
        exit(EXIT_FAILURE);
    }
}

// Connects the client socket to the server's IP address and port
static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    if(addr->ss_family == AF_INET)
    {
        ((struct sockaddr_in *)addr)->sin_port = htons(port);
    }
    else if(addr->ss_family == AF_INET6)
    {
        ((struct sockaddr_in6 *)addr)->sin6_port = htons(port);
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(connect(sockfd, (struct sockaddr *)addr, addr->ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)) == -1)
    {
        perror("Connect failed");
        exit(EXIT_FAILURE);
    }
}

// Closes the given socket
static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

// Handles sending messages to the other client
static void *handle_send(void *arg)
{
    int const *sock_fd = (int *)arg;
    char       message[BUFFER_SIZE];

    while(!exit_flag)
    {
        if(fgets(message, BUFFER_SIZE, stdin) != NULL)
        {
            if(strlen(message) > 0)
            {
                if(send(*sock_fd, message, strlen(message), 0) == -1)
                {
                    perror("send failed");
                    break;
                }
            }
        }
        else if(feof(stdin))
        {
            // feof(stdin) returns true on end-of-file (Ctrl+D)
            printf("Exiting...\n");
            exit_flag = 1;
            exit(EXIT_SUCCESS);
            //            break;
        }
        else
        {
            perror("fgets failed");
            break;
        }
    }
    return NULL;
}

// static void *handle_send(void *arg)
//{
//     int const *sock_fd = (int *)arg;
//     char       message[BUFFER_SIZE];
//
//     while(!exit_flag)
//     {
//         if(fgets(message, BUFFER_SIZE, stdin) != NULL)
//         {
//             if(strlen(message) > 0)
//             {
//                 if(send(*sock_fd, message, strlen(message), 0) == -1)
//                 {
//                     perror("send failed");
//                     break;
//                 }
//             }
//         }
//     }
//     return NULL;
// }

static void *handle_receive(void *arg)
{
    int const *sock_fd = (int *)arg;    // Cast the argument back to the appropriate type
    char       buffer[BUFFER_SIZE];

    while(!exit_flag)
    {
        ssize_t num_bytes = recv(*sock_fd, buffer, BUFFER_SIZE - 1, 0);    // Scope of num_bytes is reduced to inside the while loop
        if(num_bytes > 0)
        {
            buffer[num_bytes] = '\0';
            printf("Received: %s\n", buffer);    // testing with \n
        }
        else if(num_bytes == 0)
        {
            printf("Connection closed by peer.\n");
            break;
        }
        else
        {
            perror("recv failed");
            break;
        }
    }
    return NULL;
}

// Sets up the handler for SIGINT signal for shutdown
static void setup_signal_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

#if defined(__clang__)
    #pragma clang diagnostic push
    #pragma clang diagnostic ignored "-Wdisabled-macro-expansion"
#endif
    sa.sa_handler = sigint_handler;    // Assign signal handler function
#if defined(__clang__)
    #pragma clang diagnostic pop
#endif

    sa.sa_flags = 0;             // Use default signal handling flags
    sigemptyset(&sa.sa_mask);    // Initialize signal mask

    if(sigaction(SIGINT, &sa, NULL) == -1)
    {
        perror("sigaction");
        exit(EXIT_FAILURE);
    }
}

// Handler for SIGINT signal
static void sigint_handler(int signum)
{
    (void)signum;
    exit_flag = 1;
}
