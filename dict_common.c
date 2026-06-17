#include "dict_common.h"

#include <ctype.h>
#include <errno.h>
#include <stdarg.h>
#include <stdio.h>
#include <string.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>

static void log_with_level(const char *level, const char *fmt, va_list args)
{
    char timestamp[32];
    time_t now = time(NULL);
    struct tm local_now;

    localtime_r(&now, &local_now);
    strftime(timestamp, sizeof(timestamp), "%Y-%m-%d %H:%M:%S", &local_now);

    fprintf(stderr, "[%s] [%s] ", timestamp, level);
    vfprintf(stderr, fmt, args);
    fputc('\n', stderr);
}

int send_all(int fd, const void *buffer, size_t length)
{
    const char *cursor = (const char *)buffer;
    size_t total_sent = 0;

    while (total_sent < length) {
        ssize_t sent = send(fd, cursor + total_sent, length - total_sent, 0);
        if (sent < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (sent == 0) {
            return -1;
        }
        total_sent += (size_t)sent;
    }

    return 0;
}

int recv_all(int fd, void *buffer, size_t length)
{
    char *cursor = (char *)buffer;
    size_t total_read = 0;

    while (total_read < length) {
        ssize_t received = recv(fd, cursor + total_read, length - total_read, 0);
        if (received < 0) {
            if (errno == EINTR) {
                continue;
            }
            return -1;
        }
        if (received == 0) {
            return total_read == 0 ? 0 : -1;
        }
        total_read += (size_t)received;
    }

    return 1;
}

int send_msg(int fd, const MSG *msg)
{
    return send_all(fd, msg, sizeof(*msg));
}

int recv_msg(int fd, MSG *msg)
{
    return recv_all(fd, msg, sizeof(*msg));
}

void init_msg(MSG *msg, int type)
{
    memset(msg, 0, sizeof(*msg));
    msg->type = type;
}

void copy_bounded(char *dest, size_t dest_size, const char *src)
{
    if (dest == NULL || dest_size == 0) {
        return;
    }

    if (src == NULL) {
        dest[0] = '\0';
        return;
    }

    snprintf(dest, dest_size, "%s", src);
}

void trim_whitespace(char *value)
{
    char *start;
    char *end;

    if (value == NULL || value[0] == '\0') {
        return;
    }

    start = value;
    while (*start != '\0' && isspace((unsigned char)*start)) {
        start++;
    }

    if (start != value) {
        memmove(value, start, strlen(start) + 1);
    }

    end = value + strlen(value);
    while (end > value && isspace((unsigned char)*(end - 1))) {
        end--;
    }
    *end = '\0';
}

void normalize_msg_fields(MSG *msg)
{
    if (msg == NULL) {
        return;
    }

    msg->name[DICT_USERNAME_SIZE - 1] = '\0';
    msg->data[DICT_DATA_SIZE - 1] = '\0';
    trim_whitespace(msg->name);
    trim_whitespace(msg->data);
}

int parse_menu_choice(const char *input, int min_value, int max_value, int *choice)
{
    char *end = NULL;
    long parsed;

    if (input == NULL || choice == NULL) {
        return 0;
    }

    errno = 0;
    parsed = strtol(input, &end, 10);
    if (errno != 0 || end == input) {
        return 0;
    }

    while (*end != '\0') {
        if (!isspace((unsigned char)*end)) {
            return 0;
        }
        end++;
    }

    if (parsed < min_value || parsed > max_value) {
        return 0;
    }

    *choice = (int)parsed;
    return 1;
}

int is_valid_username(const char *username)
{
    size_t index;
    size_t length;

    if (username == NULL) {
        return 0;
    }

    length = strlen(username);
    if (length == 0 || length >= DICT_USERNAME_SIZE) {
        return 0;
    }

    for (index = 0; index < length; ++index) {
        unsigned char ch = (unsigned char)username[index];
        if (!(isalnum(ch) || ch == '_')) {
            return 0;
        }
    }

    return 1;
}

int is_valid_password(const char *password)
{
    size_t index;
    size_t length;

    if (password == NULL) {
        return 0;
    }

    length = strlen(password);
    if (length == 0 || length >= DICT_DATA_SIZE) {
        return 0;
    }

    for (index = 0; index < length; ++index) {
        if (iscntrl((unsigned char)password[index])) {
            return 0;
        }
    }

    return 1;
}

int is_valid_word(const char *word)
{
    size_t index;
    size_t length;
    int seen_non_space = 0;

    if (word == NULL) {
        return 0;
    }

    length = strlen(word);
    if (length == 0 || length >= DICT_DATA_SIZE) {
        return 0;
    }

    for (index = 0; index < length; ++index) {
        unsigned char ch = (unsigned char)word[index];
        if (iscntrl(ch)) {
            return 0;
        }
        if (isspace(ch)) {
            continue;
        }
        seen_non_space = 1;
    }

    return seen_non_space;
}

const char *msg_type_name(int type)
{
    switch (type) {
    case MSG_TYPE_REGISTER:
        return "REGISTER";
    case MSG_TYPE_LOGIN:
        return "LOGIN";
    case MSG_TYPE_QUERY:
        return "QUERY";
    case MSG_TYPE_HISTORY:
        return "HISTORY";
    default:
        return "UNKNOWN";
    }
}

void log_info(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_with_level("INFO", fmt, args);
    va_end(args);
}

void log_error(const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    log_with_level("ERROR", fmt, args);
    va_end(args);
}
