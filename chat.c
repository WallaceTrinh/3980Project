/**
 * Wallace Trinh & Jack Luo
 * A01289206 & A00948446
 */

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
#define BUFFER_SIZE 1024     // Buffer size for message exchange
#define BASE_TEN 10          // Base for numerical conversion
#define MAX_CONNECTIONS 1    // Max number of connections for the server
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

/**
 * Main Driver of the program.
 * @param argc Number of command-line arguments.
 * @param argv Array of command-line arguments.
 * @return Exit status of the program.
 */
int main(int argc, char *argv[])
{
    struct sockaddr_storage addr;
    struct sockaddr_storage client_addr;
    socklen_t               addr_size = sizeof(client_addr);
    int                     sockfd    = 0;    // Socket file descriptor
    char                   *ip;
    char                   *port_str;
    in_port_t               port;
    pthread_t               send_thread;
    pthread_t               receive_thread;
    int                     mode = 0;    // 0 for server, 1 for client

    // Check for correct number of arguments
    if(argc != 4)
    {
        fprintf(stderr, "Usage: %s [-a (for server) | -c (for client)] <ip address> <port>\n", argv[0]);
        exit(EXIT_FAILURE);
    }

    // Determine the operating mode (server or client)
    if(strcmp(argv[1], "-a") == 0)
    {
        mode = 0;    // Server mode
    }
    else if(strcmp(argv[1], "-c") == 0)
    {
        mode = 1;    // Client mode
    }
    else
    {
        fprintf(stderr, "Invalid mode. Use -a for server or -c for client.\n");
        exit(EXIT_FAILURE);
    }

    // Assign IP address and port from arguments
    ip       = argv[2];
    port_str = argv[3];
    port     = parse_in_port_t(port_str);      // Convert port string to integer
    convert_address(ip, &addr);                // Convert IP address to network address structure
    sockfd = socket_create(addr.ss_family);    // Create a socket

    // Server mode operations
    if(mode == 0)
    {                                        // Server mode
        int client_fd;                       // Socket file descriptor for the client
        socket_bind(sockfd, &addr, port);    // Bind the socket to the server address and port
        listen(sockfd, MAX_CONNECTIONS);     // Listen for incoming connections
        printf("Server listening on %s:%d\n", ip, port);

        // Accept an incoming connection
        client_fd = accept(sockfd, (struct sockaddr *)&client_addr, &addr_size);

        // Check if 'accept' was successful before printing the connection message
        if(client_fd != -1)
        {
            printf("Client has connected to the chat.\n");
            socket_close(sockfd);    // Close the listening socket as it's no longer needed
            sockfd = client_fd;      // Use the client socket for communication
        }
        else
        {
            perror("accept");
            exit(EXIT_FAILURE);
        }
    }
    else
    {    // Client mode
        socket_connect(sockfd, &addr, port);
        printf("Client connected to %s:%d\n", ip, port);
    }

    // Set up signal handler for SIGINT
    setup_signal_handler();

    // Create threads for sending and receiving messages
    pthread_create(&send_thread, NULL, handle_send, &sockfd);
    pthread_create(&receive_thread, NULL, handle_receive, (void *)&sockfd);

    // Wait for threads to complete
    pthread_join(send_thread, NULL);
    pthread_join(receive_thread, NULL);

    // Check if exit flag is set to terminate the client process
    if(exit_flag)
    {
        // The exit flag is set, so exit the client process
        exit(EXIT_SUCCESS);
    }
}

/**
 * Parses a port number from a string.
 * @param str String containing the port number.
 * @pre str should contain a valid port number.
 * @post Returns the port number in network byte order.
 * @return The port number as in_port_t.
 */
static in_port_t parse_in_port_t(const char *str)
{
    char         *endptr;
    unsigned long parsed_value;
    errno        = 0;                                  // Reset errno before conversion
    parsed_value = strtoul(str, &endptr, BASE_TEN);    // Convert string to unsigned long

    // Check for conversion errors
    if(errno != 0 || *endptr != '\0' || parsed_value > UINT16_MAX)
    {
        fprintf(stderr, "Invalid port number.\n");
        exit(EXIT_FAILURE);
    }
    return (in_port_t)parsed_value;    // Return the port number
}

/**
 * Converts an IP address string to a network address structure.
 * @pre address should contain a valid IPv4 or IPv6 address.
 * @post addr is filled with the network address.
 * @param address String containing the IP address.
 * @param addr Pointer to sockaddr_storage to store the converted address.
 */
static void convert_address(const char *address, struct sockaddr_storage *addr)
{
    // Zero out the address structure
    memset(addr, 0, sizeof(*addr));

    // Try converting to IPv4 address
    if(inet_pton(AF_INET, address, &(((struct sockaddr_in *)addr)->sin_addr)) == 1)
    {
        addr->ss_family = AF_INET;    // Set family to IPv4
    }
    // Try converting to IPv6 address
    else if(inet_pton(AF_INET6, address, &(((struct sockaddr_in6 *)addr)->sin6_addr)) == 1)
    {
        addr->ss_family = AF_INET6;    // Set family to IPv6
    }
    else    // If neither, it's an invalid address
    {
        fprintf(stderr, "%s is not a valid IPv4 or IPv6 address\n", address);
        exit(EXIT_FAILURE);
    }
}

/**
 * Creates a new socket..
 * @param domain The protocol family of the socket (e.g., AF_INET).
 * @pre domain should be a valid protocol family.
 * @post Returns a file descriptor for the new socket.
 * @return Socket file descriptor.
 */
static int socket_create(int domain)
{
    // Create a socket with given domain
    int sockfd = socket(domain, SOCK_STREAM | SOCK_CLOEXEC, 0);
    // Check if socket creation failed
    if(sockfd == -1)
    {
        perror("Socket creation failed");
        exit(EXIT_FAILURE);
    }
    return sockfd;    // Return the socket file descriptor
}

/**
 * Binds a socket to a specific address and port.
 * @param sockfd Socket file descriptor.
 * @param addr Pointer to the address to bind the socket.
 * @param port Port number to bind the socket.
 */
static void socket_bind(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    // Set port for IPv4 addresses
    if(addr->ss_family == AF_INET)
    {
        ((struct sockaddr_in *)addr)->sin_port = htons(port);    // Convert port to network byte order
    }
    // Set port for IPv6 addresses
    else if(addr->ss_family == AF_INET6)
    {
        ((struct sockaddr_in6 *)addr)->sin6_port = htons(port);    // Convert port to network byte order
    }
    // If address family is neither IPv4 nor IPv6
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

/**
 * Connects the client socket to the server's IP address and port.
 * @param sockfd Socket file descriptor.
 * @param addr Pointer to the address to connect the socket.
 * @param port Port number to connect the socket.
 */
static void socket_connect(int sockfd, struct sockaddr_storage *addr, in_port_t port)
{
    // Set port for IPv4 addresses
    if(addr->ss_family == AF_INET)
    {
        ((struct sockaddr_in *)addr)->sin_port = htons(port);    // Convert port to network byte order
    }
    // Set port for IPv6 addresses
    else if(addr->ss_family == AF_INET6)
    {
        ((struct sockaddr_in6 *)addr)->sin6_port = htons(port);    // Convert port to network byte order for IPv6
    }
    else
    {
        fprintf(stderr, "Invalid address family: %d\n", addr->ss_family);
        exit(EXIT_FAILURE);
    }
    // Connect the socket to the server's address and port
    if(connect(sockfd, (struct sockaddr *)addr, addr->ss_family == AF_INET ? sizeof(struct sockaddr_in) : sizeof(struct sockaddr_in6)) == -1)
    {
        perror("Connect failed");
        exit(EXIT_FAILURE);
    }
}

/**
 * Closes a given socket.
 * @param sockfd Socket file descriptor to close.
 */
static void socket_close(int sockfd)
{
    if(close(sockfd) == -1)    // Attempt to close the socket
    {
        perror("Error closing socket");
        exit(EXIT_FAILURE);
    }
}

/**
 * Thread function to handle sending messages.
 * @param arg Pointer to the socket file descriptor.
 * @return NULL
 */
static void *handle_send(void *arg)
{
    int const  *sock_fd = (int *)arg;                  // Cast argument to socket file descriptor
    char        message[BUFFER_SIZE];                  // Buffer to store message
    const char *shutdown_msg = "SERVER_SHUTDOWN\n";    // Message to indicate server shutdown

    while(!exit_flag)    // Continue until exit flag is set
    {
        if(fgets(message, BUFFER_SIZE, stdin) == NULL)
        {
            printf("\nEOF received, shutting down server...\n");
            exit_flag = 1;    // Set exit flag
            send(*sock_fd, shutdown_msg, strlen(shutdown_msg), 0);
            break;    // Break from loop
        }

        if(strlen(message) > 0)
        {
            if(send(*sock_fd, message, strlen(message), 0) == -1)
            {
                perror("send failed");
                break;    // Break from loop on failure
            }
        }
    }
    return NULL;    // Return NULL from the thread function
}

/**
 * Thread function to handle receiving messages.
 * @param arg Pointer to the socket file descriptor.
 * @return NULL
 */
static void *handle_receive(void *arg)
{
    int *sock_fd = (int *)arg;    // Cast argument to socket file descriptor
    char buffer[BUFFER_SIZE];     // Buffer to store received message

    while(!exit_flag)    // Continue until exit flag is set
    {
        ssize_t num_bytes = recv(*sock_fd, buffer, BUFFER_SIZE - 1, 0);    // Receive message
        if(num_bytes <= 0)                                                 // Check for error or server shutdown
        {
            // Either an error occurred, or the server closed the connection
            exit_flag = 1;    // Set exit flag
            break;            // Break from loop
        }

        buffer[num_bytes] = '\0';          // Null-terminate the received message
        printf("Received: %s", buffer);    // Print the received message

        // Check for the server shutdown message
        if(strcmp(buffer, "SERVER_SHUTDOWN\n") == 0)
        {
            printf("Server has shutdown. Exiting client.\n");
            exit_flag = 1;    // Set exit flag
            break;            // Break from loop
        }
    }

    // Close the socket and exit the client
    if(*sock_fd != -1)
    {
        socket_close(*sock_fd);    // Close the socket
        *sock_fd = -1;             // Reset socket file descriptor
    }

    // Ensure the client exits completely
    exit(EXIT_SUCCESS);    // Exit successfully
    return NULL;           // Return NULL from thread function
}

/**
 * Sets up the handler for SIGINT signal for shutdown.
 * @param signum signals will be handled by sigint_handler.
 */
static void setup_signal_handler(void)
{
    struct sigaction sa;           // Structure to specify signal handling
    memset(&sa, 0, sizeof(sa));    // Zero out the structure
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

/**
 * Handler for SIGINT signal.
 * @param signum Signal number (expected to be SIGINT).
 */
static void sigint_handler(int signum)
{
    (void)signum;     // Unused parameter
    exit_flag = 1;    // Set exit flag to handle SIGINT
}
