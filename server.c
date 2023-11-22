// Name: Wallace Trinh
// Student #:A01289206
// Date: Oct 2023

#include <arpa/inet.h>
#include <errno.h>
#include <inttypes.h>
#include <netdb.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

#define BUFFER_SIZE 1024
#define MAX_ARGS_LENGTH 100
#define BACKLOG 20
#define BASE_TEN 10
#define MAX_PATH_LENGTH 256

// Conditional SOCK_CLOEXEC for compatibility
#ifndef SOCK_CLOEXEC
    #pragma GCC diagnostic push
    #pragma GCC diagnostic ignored "-Wunused-macros"
    #define SOCK_CLOEXEC 0
    #pragma GCC diagnostic pop
#endif

// Function Declarations
static void setup_signal_handler(void);

static void sigint_handler(int signum);

static void handle_connection(int client_sockfd, struct sockaddr_storage *client_addr);

static in_port_t parse_in_port_t(const char *port_str);

static void convert_address(const char *address, struct sockaddr_storage *addr);

static int socket_create(int domain);

static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port);

static void start_listening(int server_fd);

static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len);

static void socket_close(int sockfd);

pid_t create_child_process(void);

char *trimmer(const char *str);

void parse_command_args(char *arg, char *args[]);

void attemptCommand(char *path, char *args[]);

void await_child_process(pid_t childPid);

// Global vairable to handle ext upon receiving SIGINT
// NOLINTNEXTLINE(cppcoreguidelines-avoid-non-const-global-variables)
static volatile sig_atomic_t exit_flag = 0;

// Main function
int main(int argc, char *argv[])
{
    char                   *ip;
    char                   *port_str;
    in_port_t               port;
    struct sockaddr_storage addr;
    int                     sockfd;
    ip       = argv[1];
    port_str = argv[2];
    port     = parse_in_port_t(port_str);
    convert_address(ip, &addr);
    sockfd = socket_create(addr.ss_family);
    socket_bind(sockfd, &addr, port);
    start_listening(sockfd);
    setup_signal_handler();

    if(argc != 3)
    {
        fprintf(stderr, "Usage: %s <ip address> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    while(!exit_flag)
    {
        struct sockaddr_storage client_addr;
        socklen_t               client_addr_len = sizeof(client_addr);
        int                     client_sockfd   = socket_accept_connection(sockfd, &client_addr, &client_addr_len);

        if(client_sockfd == -1)
        {
            if(exit_flag)
            {
                break;
            }
            continue;
        }

        handle_connection(client_sockfd, &client_addr);
        socket_close(client_sockfd);
    }

    socket_close(sockfd);
    return EXIT_SUCCESS;
}

// Parses the port number from string and checks for errors
static in_port_t parse_in_port_t(const char *str)
{
    char     *endptr;
    uintmax_t parsed_value;

    errno        = 0;
    parsed_value = strtoumax(str, &endptr, BASE_TEN);

    if(errno != 0)
    {
        perror("Error parsing in_port_t");
        exit(EXIT_FAILURE);
    }

    // Check if there are any non-numeric characters in the input string
    if(*endptr != '\0')
    {
        printf("NON NUMERIC CHARS");
        exit(EXIT_FAILURE);
    }

    // Check if the parsed value is within the valid range for in_port_t
    if(parsed_value > UINT16_MAX)
    {
        printf("INPORTT OUT OF RANGE");
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
        fprintf(stderr, "%s is not an IPv4 or an IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

// Creates a socket with a specified domain
static int socket_create(int domain)
{
    //  int sockfd;
    int opt = 1;

    int sockfd = socket(domain, SOCK_STREAM | SOCK_CLOEXEC, 0);

    if(sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }

    if(setsockopt(sockfd, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt)) < 0)
    {
        perror("setsockopt");
        close(sockfd);
        exit(EXIT_FAILURE);
    }

    return sockfd;
}

// Binds the socket to a specified address and port
static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    char      addr_str[INET6_ADDRSTRLEN];
    socklen_t addr_len;
    void     *vaddr;
    in_port_t net_port;

    net_port = htons(port);

    if(addr->ss_family == AF_INET)
    {
        struct sockaddr_in *ipv4_addr;

        ipv4_addr           = (struct sockaddr_in *)addr;
        addr_len            = sizeof(*ipv4_addr);
        ipv4_addr->sin_port = net_port;
        vaddr               = (void *)&(((struct sockaddr_in *)addr)->sin_addr);
    }
    else if(addr->ss_family == AF_INET6)
    {
        struct sockaddr_in6 *ipv6_addr;

        ipv6_addr            = (struct sockaddr_in6 *)addr;
        addr_len             = sizeof(*ipv6_addr);
        ipv6_addr->sin6_port = net_port;
        vaddr                = (void *)&(((struct sockaddr_in6 *)addr)->sin6_addr);
    }
    else
    {
        fprintf(stderr,
                "Internal error: addr->ss_family must be AF_INET or AF_INET6, was: "
                "%d\n",
                addr->ss_family);
        exit(EXIT_FAILURE);
    }

    if(inet_ntop(addr->ss_family, vaddr, addr_str, sizeof(addr_str)) == NULL)
    {
        perror("inet_ntop");
        exit(EXIT_FAILURE);
    }

    printf("Binding to: %s:%u\n", addr_str, port);

    if(bind(sockfd, (struct sockaddr *)addr, addr_len) == -1)
    {
        perror("Binding failed");
        fprintf(stderr, "Error code: %d\n", errno);
        exit(EXIT_FAILURE);
    }

    printf("Bound to socket: %s:%u\n", addr_str, port);
}

// To make the server start listening for any incoming connections from clients
static void start_listening(int server_fd)
{
    if(listen(server_fd, BACKLOG) == -1)
    {
        perror("listen failed");
        close(server_fd);
        exit(EXIT_FAILURE);
    }

    printf("Listening for any incoming connections...\n");
}

// Executes the command received from the client
static void execute_command(char *arg)
{
    pid_t childPid;
    char *path = getenv("PATH");
    char *args[MAX_ARGS_LENGTH];

    for(int i = 0; i < MAX_ARGS_LENGTH; i++)
    {
        args[i] = NULL;    // Initialize the array with NULLs
    }

    parse_command_args(arg, args);

    // gets unique process id from fork()
    childPid = create_child_process();

    // code ran on child process
    if(childPid == 0)
    {
        attemptCommand(path, args);
        for(int i = 0; i < MAX_ARGS_LENGTH; i++)
        {
            free(args[i]);    // Free the memory allocated in parse_command_args
        }
        exit(0);
    }
    else
    {
        // this is a parent process
        // waiting for the child process to complete
        await_child_process(childPid);
    }
}

// Handles the incoming client connections and processes the client messages
static void handle_connection(int client_sockfd, struct sockaddr_storage *client_addr __attribute__((unused)))
{
    ssize_t bytesRead;
    char    message[BUFFER_SIZE];
    int     client_stdout;
    int     original_stdout;

    bytesRead = read(client_sockfd, message, sizeof(message));
    if(bytesRead > 0)
    {
        message[bytesRead] = '\0';
        printf("Command from client: %s\n", message);
    }

    // NOLINTNEXTLINE(android-cloexec-dup)
    original_stdout = dup(STDOUT_FILENO);
    if(original_stdout == -1)
    {
        perror("dup");
        exit(EXIT_FAILURE);
    }

    client_stdout = dup2(client_sockfd, STDOUT_FILENO);
    if(client_stdout == -1)
    {
        perror("dup2");
        exit(EXIT_FAILURE);
    }

    execute_command(message);
    fflush(stdout);    // Flush the output buffer

    write(client_sockfd, "", strlen(""));

    if(dup2(original_stdout, STDOUT_FILENO) == -1)
    {
        perror("dup2 revert");
        exit(EXIT_FAILURE);
    }

    close(original_stdout);    // Close the duplicated file descriptor

    fflush(stdout);                                // Flush the output buffer again
    printf("Successful Output from Client.\n");    // Prints the message on the server side after client's request
    fflush(stdout);                                // Ensure the message is printed immediately
}

// Forks a child process for command execution
pid_t create_child_process(void)
{
    pid_t pid = fork();
    if(pid == -1)
    {
        perror("fork failed");
        exit(EXIT_FAILURE);
    }
    return pid;
}

// Parses and trims the command string
void parse_command_args(char *arg, char *args[])
{
    char *token;
    char *command;
    char *savePtr;
    int   counter = 0;

    token = strtok_r(arg, " ", &savePtr);

    command = trimmer(token);

    args[counter] = command;

    while(token != NULL && counter < MAX_ARGS_LENGTH - 1)
    {
        char *option;
        counter++;
        token         = strtok_r(NULL, " ", &savePtr);
        option        = trimmer(token);
        args[counter] = option;
    }
}

// Helps trim the leading and trailing whitespaces from a string
char *trimmer(const char *str)
{
    size_t start;
    size_t end;
    size_t len;
    char  *trimmed;

    if(str == NULL)
    {
        return NULL;
    }

    start = 0;
    end   = strlen(str) - 1;

    // Find the start position for trimming
    while(str[start] == ' ')
    {
        start++;
    }

    // Find the end position for trimming
    while(end > start && str[end] == ' ')
    {
        end--;
    }

    // Calculate the length of the trimmed string
    len = end - start + 1;

    // Allocate memory for the trimmed string
    trimmed = (char *)malloc(len + 1);

    if(trimmed == NULL)
    {
        return NULL;
    }

    // Copy the trimmed portion of the original string
    strncpy(trimmed, str + start, len);
    trimmed[len] = '\0';

    return trimmed;
}

// Attempts to execute the provided command in child process
void attemptCommand(char *path, char *args[])
{
    char *saveptr;
    char *dir = strtok_r(path, ":", &saveptr);

    while(dir != NULL)
    {
        char testPath[MAX_PATH_LENGTH] = "";

        if(args == NULL)
        {
            perror("args are null");
            exit(EXIT_FAILURE);
        }

        snprintf(testPath, sizeof(testPath), "%s/%s", dir, args[0]);

        if(access(testPath, X_OK) == 0)
        {
            execv(testPath, args);
            perror("execv");    //   Executed only if execv fails
        }

        dir = strtok_r(NULL, ":", &saveptr);
    }

    printf("Invalid command.");
}

// Waits for the cihld process to complete execution
void await_child_process(pid_t childPid)
{
    int status;
    waitpid(childPid, &status, 0);
}

// Accepts a new connection from a client
static int socket_accept_connection(int server_fd, struct sockaddr_storage *client_addr, socklen_t *client_addr_len)
{
    int  client_fd;
    char client_host[NI_MAXHOST];
    char client_service[NI_MAXSERV];

    errno     = 0;
    client_fd = accept(server_fd, (struct sockaddr *)client_addr, client_addr_len);

    if(client_fd == -1)
    {
        if(errno != EINTR)
        {
            perror("accept failed");
        }

        return -1;
    }

    if(getnameinfo((struct sockaddr *)client_addr, *client_addr_len, client_host, NI_MAXHOST, client_service, NI_MAXSERV, 0) == 0)
    {
        printf("Accepted the connection from %s:%s\n", client_host, client_service);
    }
    else
    {
        printf("Unable to get client info\n");
    }

    return client_fd;
}

// Closing the given socket
static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)
    {
        perror("Error closing the socket");
        exit(EXIT_FAILURE);
    }
}

// Sets up the handler for SIGINT signal for shutdown
static void setup_signal_handler(void)
{
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));

    // Helps deal with the macro-expansion Clang error
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
    // Handler for SIGINT signal
    (void)signum;
    exit_flag = 1;
}
