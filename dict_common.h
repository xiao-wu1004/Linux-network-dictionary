#ifndef DICT_COMMON_H
#define DICT_COMMON_H

#include <stddef.h>

#define DICT_USERNAME_SIZE 16
#define DICT_DATA_SIZE 256

#define MSG_TYPE_REGISTER 1
#define MSG_TYPE_LOGIN 2
#define MSG_TYPE_QUERY 3
#define MSG_TYPE_HISTORY 4

#define MSG_STATUS_OK "OK"
#define MSG_STATUS_NOT_FOUND "NOT_FOUND"
#define MSG_STATUS_NO_HISTORY "NO_HISTORY"
#define MSG_STATUS_HISTORY_OVER "**OVER**"
#define MSG_STATUS_ERR_AUTH "ERR_NOT_AUTHENTICATED"
#define MSG_STATUS_ERR_INPUT "ERR_INVALID_INPUT"
#define MSG_STATUS_ERR_INTERNAL "ERR_INTERNAL"
#define MSG_STATUS_ERR_EXISTS "ERR_USER_EXISTS"
#define MSG_STATUS_ERR_CREDENTIALS "ERR_INVALID_CREDENTIALS"
#define MSG_STATUS_ERR_NETWORK "ERR_NETWORK"

typedef struct {
    int type;
    char name[DICT_USERNAME_SIZE];
    char data[DICT_DATA_SIZE];
} MSG;

int send_all(int fd, const void *buffer, size_t length);
int recv_all(int fd, void *buffer, size_t length);
int send_msg(int fd, const MSG *msg);
int recv_msg(int fd, MSG *msg);

void init_msg(MSG *msg, int type);
void copy_bounded(char *dest, size_t dest_size, const char *src);
void trim_whitespace(char *value);
void normalize_msg_fields(MSG *msg);
int parse_menu_choice(const char *input, int min_value, int max_value, int *choice);

int is_valid_username(const char *username);
int is_valid_password(const char *password);
int is_valid_word(const char *word);
const char *msg_type_name(int type);

void log_info(const char *fmt, ...);
void log_error(const char *fmt, ...);

#endif
