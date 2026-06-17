#include "dict_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <termios.h>
#include <unistd.h>

static int read_line_prompt(const char* prompt, char* buffer, size_t buffer_size);
static int read_password_prompt(const char* prompt, char* buffer, size_t buffer_size);
static int prompt_menu_choice(const char* prompt, int min_value, int max_value);
static const char* describe_status(const char* status);
static int send_request_and_wait(int sockfd, MSG* msg);
static int handle_register(int sockfd);
static int handle_login(int sockfd);
static void handle_query(int sockfd);
static void handle_history(int sockfd);

static int read_line_prompt(const char* prompt, char* buffer, size_t buffer_size)
{
    size_t length;

    if (prompt != NULL)
    {
        fputs(prompt, stdout);
        fflush(stdout);
    }

    if (fgets(buffer, (int)buffer_size, stdin) == NULL)
    {
        return 0;
    }

    length = strlen(buffer);
    if (length > 0 && buffer[length - 1] != '\n')
    {
        int ch;
        while ((ch = getchar()) != '\n' && ch != EOF)
        {
        }
    }

    trim_whitespace(buffer);
    return 1;
}

static int read_password_prompt(const char* prompt, char* buffer, size_t buffer_size)
{
    struct termios old_attr;
    struct termios new_attr;
    size_t index = 0;
    int ch;

    if (tcgetattr(STDIN_FILENO, &old_attr) != 0)
    {
        perror("tcgetattr");
        return 0;
    }

    new_attr = old_attr;
    new_attr.c_lflag &= ~(ECHO);

    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_attr) != 0)
    {
        perror("tcsetattr");
        return 0;
    }

    fputs(prompt, stdout);
    fflush(stdout);

    while ((ch = getchar()) != EOF)
    {
        if (ch == '\n' || ch == '\r')
        {
            break;
        }

        if (ch == 127 || ch == '\b')
        {
            if (index > 0)
            {
                index--;
                fputs("\b \b", stdout);
                fflush(stdout);
            }
            continue;
        }

        if (index + 1 < buffer_size)
        {
            buffer[index++] = (char)ch;
            fputc('*', stdout);
            fflush(stdout);
        }
    }

    buffer[index] = '\0';
    fputc('\n', stdout);

    if (tcsetattr(STDIN_FILENO, TCSANOW, &old_attr) != 0)
    {
        perror("tcsetattr");
        return 0;
    }

    return ch != EOF;
}

static int prompt_menu_choice(const char* prompt, int min_value, int max_value)
{
    char line[32];
    int choice = 0;

    for (;;)
    {
        if (!read_line_prompt(prompt, line, sizeof(line)))
        {
            return -1;
        }

        if (parse_menu_choice(line, min_value, max_value, &choice))
        {
            return choice;
        }

        printf("Please enter a number between %d and %d.\n", min_value, max_value);
    }
}

static const char* describe_status(const char* status)
{
    if (strcmp(status, MSG_STATUS_OK) == 0)
    {
        return "Success.";
    }
    if (strcmp(status, MSG_STATUS_ERR_EXISTS) == 0)
    {
        return "User already exists.";
    }
    if (strcmp(status, MSG_STATUS_ERR_CREDENTIALS) == 0)
    {
        return "Invalid username or password.";
    }
    if (strcmp(status, MSG_STATUS_ERR_AUTH) == 0)
    {
        return "Please log in before using this action.";
    }
    if (strcmp(status, MSG_STATUS_ERR_INPUT) == 0)
    {
        return "Input is not valid.";
    }
    if (strcmp(status, MSG_STATUS_ERR_INTERNAL) == 0)
    {
        return "The server reported an internal error.";
    }
    if (strcmp(status, MSG_STATUS_NOT_FOUND) == 0)
    {
        return "Word not found.";
    }
    if (strcmp(status, MSG_STATUS_NO_HISTORY) == 0)
    {
        return "No history is available yet.";
    }
    if (strcmp(status, MSG_STATUS_ERR_NETWORK) == 0)
    {
        return "Network communication failed.";
    }
    return status;
}

static int send_request_and_wait(int sockfd, MSG* msg)
{
    int recv_status;

    if (send_msg(sockfd, msg) != 0)
    {
        perror("send");
        return 0;
    }

    recv_status = recv_msg(sockfd, msg);
    if (recv_status <= 0)
    {
        fprintf(stderr, "Connection closed while waiting for a response.\n");
        return 0;
    }

    return 1;
}

static int handle_register(int sockfd)
{
    MSG msg;

    init_msg(&msg, MSG_TYPE_REGISTER);
    if (!read_line_prompt("Enter username (letters, numbers, underscore): ", msg.name, sizeof(msg.name)))
    {
        return 0;
    }
    if (!is_valid_username(msg.name))
    {
        puts("Username must be 1-15 characters using letters, numbers, or underscore.");
        return 0;
    }

    if (!read_password_prompt("Enter password: ", msg.data, sizeof(msg.data)))
    {
        return 0;
    }
    if (!is_valid_password(msg.data))
    {
        puts("Password must be non-empty and contain no control characters.");
        return 0;
    }

    if (!send_request_and_wait(sockfd, &msg))
    {
        return -1;
    }

    printf("Register: %s\n", describe_status(msg.data));
    return strcmp(msg.data, MSG_STATUS_OK) == 0 ? 1 : 0;
}

static int handle_login(int sockfd)
{
    MSG msg;

    init_msg(&msg, MSG_TYPE_LOGIN);
    if (!read_line_prompt("Enter username: ", msg.name, sizeof(msg.name)))
    {
        return 0;
    }
    if (!is_valid_username(msg.name))
    {
        puts("Username must be 1-15 characters using letters, numbers, or underscore.");
        return 0;
    }

    if (!read_password_prompt("Enter password: ", msg.data, sizeof(msg.data)))
    {
        return 0;
    }
    if (!is_valid_password(msg.data))
    {
        puts("Password must be non-empty and contain no control characters.");
        return 0;
    }

    if (!send_request_and_wait(sockfd, &msg))
    {
        return -1;
    }

    printf("Login: %s\n", describe_status(msg.data));
    return strcmp(msg.data, MSG_STATUS_OK) == 0 ? 1 : 0;
}

static void handle_query(int sockfd)
{
    MSG msg;

    init_msg(&msg, MSG_TYPE_QUERY);
    puts("------------------------------");

    for (;;)
    {
        if (!read_line_prompt("Enter a word to search (# to exit): ", msg.data, sizeof(msg.data)))
        {
            return;
        }

        if (strcmp(msg.data, "#") == 0)
        {
            return;
        }

        if (!is_valid_word(msg.data))
        {
            puts("Word input is not valid.");
            continue;
        }

        printf("Searching...\n");
        if (!send_request_and_wait(sockfd, &msg))
        {
            exit(EXIT_FAILURE);
        }

        if (strcmp(msg.data, MSG_STATUS_NOT_FOUND) == 0 ||
            strcmp(msg.data, MSG_STATUS_ERR_AUTH) == 0 ||
            strcmp(msg.data, MSG_STATUS_ERR_INPUT) == 0 ||
            strcmp(msg.data, MSG_STATUS_ERR_INTERNAL) == 0)
        {
            printf(">>> %s\n", describe_status(msg.data));
        }
        else
        {
            printf(">>> %s\n", msg.data);
        }
    }
}

static void handle_history(int sockfd)
{
    MSG msg;
    int recv_status;

    init_msg(&msg, MSG_TYPE_HISTORY);
    if (send_msg(sockfd, &msg) != 0)
    {
        perror("send");
        exit(EXIT_FAILURE);
    }

    for (;;)
    {
        recv_status = recv_msg(sockfd, &msg);
        if (recv_status <= 0)
        {
            fprintf(stderr, "Connection closed while reading history.\n");
            exit(EXIT_FAILURE);
        }

        if (strcmp(msg.data, MSG_STATUS_HISTORY_OVER) == 0)
        {
            break;
        }

        if (strcmp(msg.data, MSG_STATUS_NO_HISTORY) == 0 ||
            strcmp(msg.data, MSG_STATUS_ERR_AUTH) == 0 ||
            strcmp(msg.data, MSG_STATUS_ERR_INTERNAL) == 0)
        {
            printf("%s\n", describe_status(msg.data));
            continue;
        }

        printf("%s\n", msg.data);
    }
}

int main(int argc, char* argv[])
{
    int sockfd;
    long port_number;
    char* end = NULL;
    struct sockaddr_in server_addr;

    if (argc != 3)
    {
        fprintf(stderr, "Usage: %s <server-ip> <server-port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    errno = 0;
    port_number = strtol(argv[2], &end, 10);
    if (errno != 0 || end == argv[2] || *end != '\0' || port_number < 1 || port_number > 65535)
    {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    sockfd = socket(AF_INET, SOCK_STREAM, 0);
    if (sockfd < 0)
    {
        perror("socket");
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((unsigned short)port_number);
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1)
    {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        close(sockfd);
        return EXIT_FAILURE;
    }

    if (connect(sockfd, (struct sockaddr*)&server_addr, sizeof(server_addr)) != 0)
    {
        perror("connect");
        close(sockfd);
        return EXIT_FAILURE;
    }

    for (;;)
    {
        int choice;

        printf("*********************************\n");
        printf("* 1. Register  2. Login  3. Exit *\n");
        printf("*********************************\n");

        choice = prompt_menu_choice("Select an action (1-3): ", 1, 3);
        if (choice < 0)
        {
            break;
        }

        switch (choice)
        {
        case 1:
            if (handle_register(sockfd) < 0)
            {
                close(sockfd);
                return EXIT_FAILURE;
            }
            break;
        case 2:
        {
            int login_status = handle_login(sockfd);
            if (login_status < 0)
            {
                close(sockfd);
                return EXIT_FAILURE;
            }
            if (login_status == 1)
            {
                for (;;)
                {
                    int query_choice;

                    printf("******************************************\n");
                    printf("* 1. Query word  2. Query history  3. Exit *\n");
                    printf("******************************************\n");

                    query_choice = prompt_menu_choice("Select an action (1-3): ", 1, 3);
                    if (query_choice < 0)
                    {
                        close(sockfd);
                        return EXIT_SUCCESS;
                    }

                    if (query_choice == 1)
                    {
                        handle_query(sockfd);
                    }
                    else if (query_choice == 2)
                    {
                        handle_history(sockfd);
                    }
                    else
                    {
                        close(sockfd);
                        return EXIT_SUCCESS;
                    }
                }
            }
        }
        break;
        case 3:
            close(sockfd);
            return EXIT_SUCCESS;
        default:
            break;
        }
    }

    close(sockfd);
    return EXIT_SUCCESS;
}
