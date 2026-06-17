#include "dict_common.h"

#include <arpa/inet.h>
#include <errno.h>
#include <netinet/in.h>
#include <openssl/evp.h>
#include <openssl/rand.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

#define DATABASE_FILE "my.db"
#define DICTIONARY_FILE "dict.txt"
#define DB_SCHEMA_VERSION 2
#define DICT_BUCKET_COUNT 4096U
#define MAX_DICT_LINE 1024
#define MAX_HISTORY_RECORDS 10
#define SALT_SIZE 16
#define SALT_HEX_SIZE ((SALT_SIZE * 2) + 1)
#define HASH_SIZE 32
#define HASH_HEX_SIZE ((HASH_SIZE * 2) + 1)

typedef struct DictEntry {
    char *word;
    char *meaning;
    struct DictEntry *next;
} DictEntry;

typedef struct {
    DictEntry **buckets;
    size_t bucket_count;
    size_t entry_count;
} Dictionary;

typedef struct {
    int authenticated;
    char current_user[DICT_USERNAME_SIZE];
} ClientSession;

static Dictionary g_dictionary;

static void reap_children(int signal_number);
static int install_signal_handlers(void);
static char *duplicate_string(const char *value);
static unsigned long hash_word(const char *word);
static void dictionary_init(Dictionary *dictionary);
static void dictionary_free(Dictionary *dictionary);
static int dictionary_insert(Dictionary *dictionary, const char *word, const char *meaning);
static const char *dictionary_lookup(const Dictionary *dictionary, const char *word);
static int load_dictionary(Dictionary *dictionary, const char *path);
static char *find_definition_separator(char *line);
static void format_timestamp(char *buffer, size_t buffer_size);
static void hex_encode(const unsigned char *input, size_t input_size, char *output, size_t output_size);
static int hex_decode(const char *input, unsigned char *output, size_t output_size);
static int hash_password_hex(const unsigned char *salt, size_t salt_size, const char *password,
                             char *hash_hex, size_t hash_hex_size);
static int configure_database(sqlite3 *db);
static int rebuild_schema(sqlite3 *db);
static int ensure_database_schema(sqlite3 *db);
static int open_database(sqlite3 **db_out);
static int sqlite_step_done(sqlite3 *db, sqlite3_stmt *stmt, const char *context);
static int user_exists(sqlite3 *db, const char *username);
static int create_user(sqlite3 *db, const char *username, const char *password, char *message, size_t message_size);
static int authenticate_user(sqlite3 *db, const char *username, const char *password,
                             ClientSession *session, char *message, size_t message_size);
static int prune_history(sqlite3 *db, const char *username);
static int record_history(sqlite3 *db, const char *username, const char *word);
static int send_history(sqlite3 *db, int fd, const char *username);
static void send_status(int fd, int type, const char *status);
static int require_authenticated(int fd, int type, const ClientSession *session);
static void handle_register_request(int fd, MSG *msg, sqlite3 *db);
static void handle_login_request(int fd, MSG *msg, sqlite3 *db, ClientSession *session);
static void handle_query_request(int fd, MSG *msg, sqlite3 *db, const ClientSession *session);
static void handle_history_request(int fd, MSG *msg, sqlite3 *db, const ClientSession *session);
static void handle_client(int fd, const struct sockaddr_in *client_addr);

static void reap_children(int signal_number)
{
    int saved_errno = errno;
    (void)signal_number;

    while (waitpid(-1, NULL, WNOHANG) > 0) {
    }

    errno = saved_errno;
}

static int install_signal_handlers(void)
{
    struct sigaction action;

    memset(&action, 0, sizeof(action));
    action.sa_handler = reap_children;
    action.sa_flags = SA_RESTART;
    sigemptyset(&action.sa_mask);

    return sigaction(SIGCHLD, &action, NULL);
}

static char *duplicate_string(const char *value)
{
    size_t length;
    char *copy;

    if (value == NULL) {
        return NULL;
    }

    length = strlen(value) + 1;
    copy = (char *)malloc(length);
    if (copy == NULL) {
        return NULL;
    }

    memcpy(copy, value, length);
    return copy;
}

static unsigned long hash_word(const char *word)
{
    unsigned long hash = 5381UL;
    int ch;

    while ((ch = (unsigned char)*word++) != 0) {
        hash = ((hash << 5) + hash) + (unsigned long)ch;
    }

    return hash;
}

static void dictionary_init(Dictionary *dictionary)
{
    memset(dictionary, 0, sizeof(*dictionary));
    dictionary->bucket_count = DICT_BUCKET_COUNT;
    dictionary->buckets = (DictEntry **)calloc(dictionary->bucket_count, sizeof(DictEntry *));
}

static void dictionary_free(Dictionary *dictionary)
{
    size_t index;

    if (dictionary == NULL || dictionary->buckets == NULL) {
        return;
    }

    for (index = 0; index < dictionary->bucket_count; ++index) {
        DictEntry *entry = dictionary->buckets[index];
        while (entry != NULL) {
            DictEntry *next = entry->next;
            free(entry->word);
            free(entry->meaning);
            free(entry);
            entry = next;
        }
    }

    free(dictionary->buckets);
    dictionary->buckets = NULL;
    dictionary->bucket_count = 0;
    dictionary->entry_count = 0;
}

static int dictionary_insert(Dictionary *dictionary, const char *word, const char *meaning)
{
    size_t bucket_index;
    DictEntry *entry;
    char *word_copy;
    char *meaning_copy;

    if (dictionary == NULL || dictionary->buckets == NULL || word == NULL || meaning == NULL) {
        return -1;
    }

    bucket_index = hash_word(word) % dictionary->bucket_count;
    for (entry = dictionary->buckets[bucket_index]; entry != NULL; entry = entry->next) {
        if (strcmp(entry->word, word) == 0) {
            meaning_copy = duplicate_string(meaning);
            if (meaning_copy == NULL) {
                return -1;
            }
            free(entry->meaning);
            entry->meaning = meaning_copy;
            return 0;
        }
    }

    entry = (DictEntry *)calloc(1, sizeof(*entry));
    if (entry == NULL) {
        return -1;
    }

    word_copy = duplicate_string(word);
    meaning_copy = duplicate_string(meaning);
    if (word_copy == NULL || meaning_copy == NULL) {
        free(word_copy);
        free(meaning_copy);
        free(entry);
        return -1;
    }

    entry->word = word_copy;
    entry->meaning = meaning_copy;
    entry->next = dictionary->buckets[bucket_index];
    dictionary->buckets[bucket_index] = entry;
    dictionary->entry_count++;
    return 0;
}

static const char *dictionary_lookup(const Dictionary *dictionary, const char *word)
{
    size_t bucket_index;
    DictEntry *entry;

    if (dictionary == NULL || dictionary->buckets == NULL || word == NULL) {
        return NULL;
    }

    bucket_index = hash_word(word) % dictionary->bucket_count;
    for (entry = dictionary->buckets[bucket_index]; entry != NULL; entry = entry->next) {
        if (strcmp(entry->word, word) == 0) {
            return entry->meaning;
        }
    }

    return NULL;
}

static char *find_definition_separator(char *line)
{
    char *cursor = line;

    while (*cursor != '\0') {
        if ((*cursor == ' ' || *cursor == '\t') &&
            (cursor[1] == ' ' || cursor[1] == '\t')) {
            return cursor;
        }
        cursor++;
    }

    return NULL;
}

static int load_dictionary(Dictionary *dictionary, const char *path)
{
    FILE *fp;
    char line[MAX_DICT_LINE];
    size_t loaded = 0;
    size_t skipped = 0;

    fp = fopen(path, "r");
    if (fp == NULL) {
        log_error("Failed to open dictionary file: %s", path);
        return -1;
    }

    while (fgets(line, sizeof(line), fp) != NULL) {
        char *separator;
        char *word;
        char *meaning;

        trim_whitespace(line);
        if (line[0] == '\0') {
            skipped++;
            continue;
        }

        word = line;
        separator = find_definition_separator(word);
        if (separator == NULL) {
            skipped++;
            continue;
        }

        *separator++ = '\0';
        while (*separator == ' ' || *separator == '\t') {
            separator++;
        }

        meaning = separator;
        trim_whitespace(word);
        trim_whitespace(meaning);
        if (word[0] == '\0' || meaning[0] == '\0') {
            skipped++;
            continue;
        }

        if (dictionary_insert(dictionary, word, meaning) != 0) {
            fclose(fp);
            log_error("Failed to store dictionary entry for word: %s", word);
            return -1;
        }
        loaded++;
    }

    fclose(fp);
    log_info("Dictionary loaded: %zu entries, %zu skipped", loaded, skipped);
    return loaded > 0 ? 0 : -1;
}

static void format_timestamp(char *buffer, size_t buffer_size)
{
    time_t now = time(NULL);
    struct tm local_now;

    localtime_r(&now, &local_now);
    strftime(buffer, buffer_size, "%Y-%m-%d %H:%M:%S", &local_now);
}

static void hex_encode(const unsigned char *input, size_t input_size, char *output, size_t output_size)
{
    static const char digits[] = "0123456789abcdef";
    size_t index;

    if (output_size < (input_size * 2U + 1U)) {
        if (output_size > 0U) {
            output[0] = '\0';
        }
        return;
    }

    for (index = 0; index < input_size; ++index) {
        output[index * 2U] = digits[(input[index] >> 4) & 0x0F];
        output[index * 2U + 1U] = digits[input[index] & 0x0F];
    }
    output[input_size * 2U] = '\0';
}

static int hex_decode(const char *input, unsigned char *output, size_t output_size)
{
    size_t input_length;
    size_t index;

    if (input == NULL || output == NULL) {
        return -1;
    }

    input_length = strlen(input);
    if (input_length != output_size * 2U) {
        return -1;
    }

    for (index = 0; index < output_size; ++index) {
        unsigned int value;
        if (sscanf(input + (index * 2U), "%2x", &value) != 1) {
            return -1;
        }
        output[index] = (unsigned char)value;
    }

    return 0;
}

static int hash_password_hex(const unsigned char *salt, size_t salt_size, const char *password,
                             char *hash_hex, size_t hash_hex_size)
{
    EVP_MD_CTX *context = NULL;
    unsigned char digest[HASH_SIZE];
    unsigned int digest_length = 0;
    int result = -1;

    context = EVP_MD_CTX_new();
    if (context == NULL) {
        return -1;
    }

    if (EVP_DigestInit_ex(context, EVP_sha256(), NULL) != 1) {
        goto cleanup;
    }
    if (EVP_DigestUpdate(context, salt, salt_size) != 1) {
        goto cleanup;
    }
    if (EVP_DigestUpdate(context, password, strlen(password)) != 1) {
        goto cleanup;
    }
    if (EVP_DigestFinal_ex(context, digest, &digest_length) != 1 || digest_length != HASH_SIZE) {
        goto cleanup;
    }

    hex_encode(digest, digest_length, hash_hex, hash_hex_size);
    result = 0;

cleanup:
    EVP_MD_CTX_free(context);
    return result;
}

static int configure_database(sqlite3 *db)
{
    char *errmsg = NULL;
    const char *sql =
        "PRAGMA journal_mode=WAL;"
        "PRAGMA synchronous=NORMAL;"
        "PRAGMA foreign_keys=ON;";

    sqlite3_busy_timeout(db, 5000);
    if (sqlite3_exec(db, sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        log_error("Failed to configure database: %s", errmsg == NULL ? "unknown error" : errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    return 0;
}

static int rebuild_schema(sqlite3 *db)
{
    char *errmsg = NULL;
    const char *schema_sql =
        "DROP TABLE IF EXISTS record;"
        "DROP TABLE IF EXISTS usr;"
        "CREATE TABLE usr ("
        "  name TEXT PRIMARY KEY NOT NULL,"
        "  salt_hex TEXT NOT NULL,"
        "  pass_hash_hex TEXT NOT NULL"
        ");"
        "CREATE TABLE record ("
        "  id INTEGER PRIMARY KEY AUTOINCREMENT,"
        "  name TEXT NOT NULL,"
        "  created_at TEXT NOT NULL,"
        "  word TEXT NOT NULL"
        ");"
        "CREATE INDEX idx_record_name_created_id "
        "  ON record(name, created_at DESC, id DESC);"
        "PRAGMA user_version=2;";

    if (sqlite3_exec(db, schema_sql, NULL, NULL, &errmsg) != SQLITE_OK) {
        log_error("Failed to rebuild database schema: %s", errmsg == NULL ? "unknown error" : errmsg);
        sqlite3_free(errmsg);
        return -1;
    }

    log_info("Database schema initialized to version %d", DB_SCHEMA_VERSION);
    return 0;
}

static int ensure_database_schema(sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    int version = 0;

    if (sqlite3_prepare_v2(db, "PRAGMA user_version;", -1, &stmt, NULL) != SQLITE_OK) {
        log_error("Failed to read database schema version: %s", sqlite3_errmsg(db));
        return -1;
    }

    if (sqlite3_step(stmt) == SQLITE_ROW) {
        version = sqlite3_column_int(stmt, 0);
    }
    sqlite3_finalize(stmt);

    if (version != DB_SCHEMA_VERSION) {
        log_info("Rebuilding database schema (found version %d, expected %d)", version, DB_SCHEMA_VERSION);
        return rebuild_schema(db);
    }

    return 0;
}

static int open_database(sqlite3 **db_out)
{
    sqlite3 *db = NULL;

    if (sqlite3_open(DATABASE_FILE, &db) != SQLITE_OK) {
        log_error("Failed to open database: %s", sqlite3_errmsg(db));
        if (db != NULL) {
            sqlite3_close(db);
        }
        return -1;
    }

    if (configure_database(db) != 0 || ensure_database_schema(db) != 0) {
        sqlite3_close(db);
        return -1;
    }

    *db_out = db;
    return 0;
}

static int sqlite_step_done(sqlite3 *db, sqlite3_stmt *stmt, const char *context)
{
    if (sqlite3_step(stmt) != SQLITE_DONE) {
        log_error("%s failed: %s", context, sqlite3_errmsg(db));
        return -1;
    }
    return 0;
}

static int user_exists(sqlite3 *db, const char *username)
{
    sqlite3_stmt *stmt = NULL;
    int exists = 0;

    if (sqlite3_prepare_v2(db, "SELECT 1 FROM usr WHERE name = ?;", -1, &stmt, NULL) != SQLITE_OK) {
        log_error("Failed to prepare user lookup: %s", sqlite3_errmsg(db));
        return 0;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    exists = (sqlite3_step(stmt) == SQLITE_ROW);
    sqlite3_finalize(stmt);
    return exists;
}

static int create_user(sqlite3 *db, const char *username, const char *password, char *message, size_t message_size)
{
    sqlite3_stmt *stmt = NULL;
    unsigned char salt[SALT_SIZE];
    char salt_hex[SALT_HEX_SIZE];
    char hash_hex[HASH_HEX_SIZE];

    if (!is_valid_username(username) || !is_valid_password(password)) {
        copy_bounded(message, message_size, MSG_STATUS_ERR_INPUT);
        return -1;
    }

    if (user_exists(db, username)) {
        copy_bounded(message, message_size, MSG_STATUS_ERR_EXISTS);
        return -1;
    }

    if (RAND_bytes(salt, sizeof(salt)) != 1) {
        log_error("Failed to generate password salt");
        copy_bounded(message, message_size, MSG_STATUS_ERR_INTERNAL);
        return -1;
    }

    hex_encode(salt, sizeof(salt), salt_hex, sizeof(salt_hex));
    if (hash_password_hex(salt, sizeof(salt), password, hash_hex, sizeof(hash_hex)) != 0) {
        log_error("Failed to hash password");
        copy_bounded(message, message_size, MSG_STATUS_ERR_INTERNAL);
        return -1;
    }

    if (sqlite3_prepare_v2(db,
                           "INSERT INTO usr (name, salt_hex, pass_hash_hex) VALUES (?, ?, ?);",
                           -1, &stmt, NULL) != SQLITE_OK) {
        log_error("Failed to prepare user insert: %s", sqlite3_errmsg(db));
        copy_bounded(message, message_size, MSG_STATUS_ERR_INTERNAL);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, salt_hex, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, hash_hex, -1, SQLITE_STATIC);

    if (sqlite_step_done(db, stmt, "User insert") != 0) {
        copy_bounded(message, message_size, MSG_STATUS_ERR_INTERNAL);
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    copy_bounded(message, message_size, MSG_STATUS_OK);
    return 0;
}

static int authenticate_user(sqlite3 *db, const char *username, const char *password,
                             ClientSession *session, char *message, size_t message_size)
{
    sqlite3_stmt *stmt = NULL;
    const unsigned char *salt_hex_text;
    const unsigned char *stored_hash_text;
    unsigned char salt[SALT_SIZE];
    char computed_hash[HASH_HEX_SIZE];

    if (!is_valid_username(username) || !is_valid_password(password)) {
        copy_bounded(message, message_size, MSG_STATUS_ERR_INPUT);
        return -1;
    }

    if (sqlite3_prepare_v2(db,
                           "SELECT salt_hex, pass_hash_hex FROM usr WHERE name = ?;",
                           -1, &stmt, NULL) != SQLITE_OK) {
        log_error("Failed to prepare login query: %s", sqlite3_errmsg(db));
        copy_bounded(message, message_size, MSG_STATUS_ERR_INTERNAL);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    if (sqlite3_step(stmt) != SQLITE_ROW) {
        sqlite3_finalize(stmt);
        copy_bounded(message, message_size, MSG_STATUS_ERR_CREDENTIALS);
        return -1;
    }

    salt_hex_text = sqlite3_column_text(stmt, 0);
    stored_hash_text = sqlite3_column_text(stmt, 1);
    if (salt_hex_text == NULL || stored_hash_text == NULL ||
        hex_decode((const char *)salt_hex_text, salt, sizeof(salt)) != 0 ||
        hash_password_hex(salt, sizeof(salt), password, computed_hash, sizeof(computed_hash)) != 0 ||
        strcmp(computed_hash, (const char *)stored_hash_text) != 0) {
        sqlite3_finalize(stmt);
        copy_bounded(message, message_size, MSG_STATUS_ERR_CREDENTIALS);
        return -1;
    }

    sqlite3_finalize(stmt);
    session->authenticated = 1;
    copy_bounded(session->current_user, sizeof(session->current_user), username);
    copy_bounded(message, message_size, MSG_STATUS_OK);
    return 0;
}

static int prune_history(sqlite3 *db, const char *username)
{
    sqlite3_stmt *stmt = NULL;

    if (sqlite3_prepare_v2(
            db,
            "DELETE FROM record WHERE id IN ("
            "  SELECT id FROM record WHERE name = ? ORDER BY id DESC LIMIT -1 OFFSET ?"
            ");",
            -1, &stmt, NULL) != SQLITE_OK) {
        log_error("Failed to prepare history prune statement: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_int(stmt, 2, MAX_HISTORY_RECORDS);

    if (sqlite_step_done(db, stmt, "History prune") != 0) {
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return 0;
}

static int record_history(sqlite3 *db, const char *username, const char *word)
{
    sqlite3_stmt *stmt = NULL;
    char created_at[32];

    format_timestamp(created_at, sizeof(created_at));

    if (sqlite3_prepare_v2(db,
                           "INSERT INTO record (name, created_at, word) VALUES (?, ?, ?);",
                           -1, &stmt, NULL) != SQLITE_OK) {
        log_error("Failed to prepare history insert: %s", sqlite3_errmsg(db));
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, created_at, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 3, word, -1, SQLITE_STATIC);

    if (sqlite_step_done(db, stmt, "History insert") != 0) {
        sqlite3_finalize(stmt);
        return -1;
    }

    sqlite3_finalize(stmt);
    return prune_history(db, username);
}

static int send_history(sqlite3 *db, int fd, const char *username)
{
    sqlite3_stmt *stmt = NULL;
    MSG response;
    int row_count = 0;

    if (sqlite3_prepare_v2(db,
                           "SELECT created_at, word FROM record "
                           "WHERE name = ? ORDER BY id DESC;",
                           -1, &stmt, NULL) != SQLITE_OK) {
        log_error("Failed to prepare history query: %s", sqlite3_errmsg(db));
        send_status(fd, MSG_TYPE_HISTORY, MSG_STATUS_ERR_INTERNAL);
        send_status(fd, MSG_TYPE_HISTORY, MSG_STATUS_HISTORY_OVER);
        return -1;
    }

    sqlite3_bind_text(stmt, 1, username, -1, SQLITE_STATIC);

    while (sqlite3_step(stmt) == SQLITE_ROW) {
        const unsigned char *created_at = sqlite3_column_text(stmt, 0);
        const unsigned char *word = sqlite3_column_text(stmt, 1);

        init_msg(&response, MSG_TYPE_HISTORY);
        snprintf(response.data, sizeof(response.data), "%s:%s",
                 created_at == NULL ? "" : (const char *)created_at,
                 word == NULL ? "" : (const char *)word);

        if (send_msg(fd, &response) != 0) {
            sqlite3_finalize(stmt);
            return -1;
        }
        row_count++;
    }

    sqlite3_finalize(stmt);

    if (row_count == 0) {
        send_status(fd, MSG_TYPE_HISTORY, MSG_STATUS_NO_HISTORY);
    }
    send_status(fd, MSG_TYPE_HISTORY, MSG_STATUS_HISTORY_OVER);
    return 0;
}

static void send_status(int fd, int type, const char *status)
{
    MSG response;
    init_msg(&response, type);
    copy_bounded(response.data, sizeof(response.data), status);
    (void)send_msg(fd, &response);
}

static int require_authenticated(int fd, int type, const ClientSession *session)
{
    if (session->authenticated) {
        return 1;
    }

    send_status(fd, type, MSG_STATUS_ERR_AUTH);
    if (type == MSG_TYPE_HISTORY) {
        send_status(fd, type, MSG_STATUS_HISTORY_OVER);
    }
    return 0;
}

static void handle_register_request(int fd, MSG *msg, sqlite3 *db)
{
    create_user(db, msg->name, msg->data, msg->data, sizeof(msg->data));
    if (send_msg(fd, msg) != 0) {
        log_error("Failed to send register response");
    }
}

static void handle_login_request(int fd, MSG *msg, sqlite3 *db, ClientSession *session)
{
    authenticate_user(db, msg->name, msg->data, session, msg->data, sizeof(msg->data));
    if (send_msg(fd, msg) != 0) {
        log_error("Failed to send login response");
        return;
    }

    if (strcmp(msg->data, MSG_STATUS_OK) == 0) {
        log_info("User authenticated: %s", session->current_user);
    } else {
        log_info("Authentication failed for user: %s", msg->name);
    }
}

static void handle_query_request(int fd, MSG *msg, sqlite3 *db, const ClientSession *session)
{
    const char *meaning;

    if (!require_authenticated(fd, MSG_TYPE_QUERY, session)) {
        return;
    }

    if (!is_valid_word(msg->data)) {
        copy_bounded(msg->data, sizeof(msg->data), MSG_STATUS_ERR_INPUT);
        (void)send_msg(fd, msg);
        return;
    }

    {
        char searched_word[DICT_DATA_SIZE];
        copy_bounded(searched_word, sizeof(searched_word), msg->data);
        meaning = dictionary_lookup(&g_dictionary, searched_word);
        if (meaning == NULL) {
            copy_bounded(msg->data, sizeof(msg->data), MSG_STATUS_NOT_FOUND);
            log_info("Query miss for user %s: %s", session->current_user, searched_word);
        } else {
            copy_bounded(msg->data, sizeof(msg->data), meaning);
            if (record_history(db, session->current_user, searched_word) != 0) {
                log_error("Failed to record query history for user %s", session->current_user);
            }
            log_info("Query hit for user %s: %s", session->current_user, searched_word);
        }
    }

    if (send_msg(fd, msg) != 0) {
        log_error("Failed to send query response");
    }
}

static void handle_history_request(int fd, MSG *msg, sqlite3 *db, const ClientSession *session)
{
    (void)msg;
    if (!require_authenticated(fd, MSG_TYPE_HISTORY, session)) {
        return;
    }

    if (send_history(db, fd, session->current_user) != 0) {
        log_error("Failed to send history for user %s", session->current_user);
    } else {
        log_info("History returned for user %s", session->current_user);
    }
}

static void handle_client(int fd, const struct sockaddr_in *client_addr)
{
    sqlite3 *db = NULL;
    ClientSession session;
    MSG msg;
    int recv_status;
    char address_buffer[INET_ADDRSTRLEN];
    unsigned short port;

    memset(&session, 0, sizeof(session));

    if (inet_ntop(AF_INET, &client_addr->sin_addr, address_buffer, sizeof(address_buffer)) == NULL) {
        copy_bounded(address_buffer, sizeof(address_buffer), "unknown");
    }
    port = ntohs(client_addr->sin_port);

    if (open_database(&db) != 0) {
        send_status(fd, MSG_TYPE_LOGIN, MSG_STATUS_ERR_INTERNAL);
        close(fd);
        _exit(EXIT_FAILURE);
    }

    log_info("Client connected: %s:%u", address_buffer, port);

    for (;;) {
        recv_status = recv_msg(fd, &msg);
        if (recv_status == 0) {
            log_info("Client disconnected: %s:%u", address_buffer, port);
            break;
        }
        if (recv_status < 0) {
            log_error("Protocol receive failure from %s:%u", address_buffer, port);
            break;
        }

        normalize_msg_fields(&msg);
        log_info("Received %s request from %s:%u", msg_type_name(msg.type), address_buffer, port);
        switch (msg.type) {
        case MSG_TYPE_REGISTER:
            handle_register_request(fd, &msg, db);
            break;
        case MSG_TYPE_LOGIN:
            handle_login_request(fd, &msg, db, &session);
            break;
        case MSG_TYPE_QUERY:
            handle_query_request(fd, &msg, db, &session);
            break;
        case MSG_TYPE_HISTORY:
            handle_history_request(fd, &msg, db, &session);
            break;
        default:
            log_error("Unsupported message type: %d", msg.type);
            send_status(fd, MSG_TYPE_QUERY, MSG_STATUS_ERR_INPUT);
            break;
        }
    }

    sqlite3_close(db);
    close(fd);
}

int main(int argc, char *argv[])
{
    int listenfd;
    int enable_reuse = 1;
    long port_number;
    char *end = NULL;
    struct sockaddr_in server_addr;

    if (argc != 3) {
        fprintf(stderr, "Usage: %s <listen-ip> <listen-port>\n", argv[0]);
        return EXIT_FAILURE;
    }

    errno = 0;
    port_number = strtol(argv[2], &end, 10);
    if (errno != 0 || end == argv[2] || *end != '\0' || port_number < 1 || port_number > 65535) {
        fprintf(stderr, "Invalid port: %s\n", argv[2]);
        return EXIT_FAILURE;
    }

    dictionary_init(&g_dictionary);
    if (g_dictionary.buckets == NULL || load_dictionary(&g_dictionary, DICTIONARY_FILE) != 0) {
        dictionary_free(&g_dictionary);
        return EXIT_FAILURE;
    }

    {
        sqlite3 *bootstrap_db = NULL;
        if (open_database(&bootstrap_db) != 0) {
            dictionary_free(&g_dictionary);
            return EXIT_FAILURE;
        }
        sqlite3_close(bootstrap_db);
    }

    if (install_signal_handlers() != 0) {
        perror("sigaction");
        dictionary_free(&g_dictionary);
        return EXIT_FAILURE;
    }

    listenfd = socket(AF_INET, SOCK_STREAM, 0);
    if (listenfd < 0) {
        perror("socket");
        dictionary_free(&g_dictionary);
        return EXIT_FAILURE;
    }

    if (setsockopt(listenfd, SOL_SOCKET, SO_REUSEADDR, &enable_reuse, sizeof(enable_reuse)) != 0) {
        perror("setsockopt");
        close(listenfd);
        dictionary_free(&g_dictionary);
        return EXIT_FAILURE;
    }

    memset(&server_addr, 0, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons((uint16_t)port_number);
    if (inet_pton(AF_INET, argv[1], &server_addr.sin_addr) != 1) {
        fprintf(stderr, "Invalid IPv4 address: %s\n", argv[1]);
        close(listenfd);
        dictionary_free(&g_dictionary);
        return EXIT_FAILURE;
    }

    if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) != 0) {
        perror("bind");
        close(listenfd);
        dictionary_free(&g_dictionary);
        return EXIT_FAILURE;
    }

    if (listen(listenfd, 16) != 0) {
        perror("listen");
        close(listenfd);
        dictionary_free(&g_dictionary);
        return EXIT_FAILURE;
    }

    log_info("Server started on %s:%ld", argv[1], port_number);

    for (;;) {
        int client_fd;
        struct sockaddr_in client_addr;
        socklen_t client_size = sizeof(client_addr);
        pid_t pid;

        client_fd = accept(listenfd, (struct sockaddr *)&client_addr, &client_size);
        if (client_fd < 0) {
            if (errno == EINTR) {
                continue;
            }
            perror("accept");
            break;
        }

        pid = fork();
        if (pid < 0) {
            perror("fork");
            close(client_fd);
            continue;
        }

        if (pid == 0) {
            close(listenfd);
            handle_client(client_fd, &client_addr);
            dictionary_free(&g_dictionary);
            _exit(EXIT_SUCCESS);
        }

        close(client_fd);
    }

    close(listenfd);
    dictionary_free(&g_dictionary);
    return EXIT_SUCCESS;
}
