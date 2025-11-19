#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sqlite3.h>
#include <signal.h>
#include <time.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/wait.h>

#define N 16 // 用户名和字段名最大长度
#define R 1  // 用户注册操作码
#define L 2  // 用户登录操作码
#define Q 3  // 查询单词操作码
#define H 4  // 查询历史记录操作码

#define DATABASE "my.db" // 数据库文件名
#define MAX_RECORDS 10   // 单用户最大历史记录数

typedef struct
{
    int type;       // 操作类型 R/L/Q/H
    char name[N];   // 用户名
    char data[256]; // 密码/单词/查询结果
} MSG;

void handler(int sig);                                                     // 僵尸进程处理函数
void handle_register(int connectfd, MSG *msg, sqlite3 *db);                // 处理注册请求
void handle_login(int connectfd, MSG *msg, sqlite3 *db);                   // 处理登录请求
void handle_client(int connectfd, sqlite3 *db);                            // 处理客户端请求
int handle_searchword(int connectfd, MSG *msg);                            // 处理单词搜索请求
void getdate(char *data);                                                  // 获取当前时间
void handle_query(int connectfd, MSG *msg, sqlite3 *db);                   // 处理查询请求
int history_callback(void *arg, int f_num, char **f_value, char **f_name); // 历史记录回调函数
void handle_history(int connectfd, MSG *msg, sqlite3 *db);                 // 处理历史记录请求
void clean_old_records(const char *name, sqlite3 *db);                     // 清理旧的历史记录

// 僵尸进程处理函数
void handler(int sig)
{
    wait(NULL); // 回收子进程，避免僵尸进程
}

// 注册处理函数
void handle_register(int connectfd, MSG *msg, sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO usr (name, pass) VALUES (?, ?)"; // 参数化SQL

    // 准备SQL语句
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        sprintf(msg->data, "注册失败：%s", sqlite3_errmsg(db));
        send(connectfd, msg, sizeof(MSG), 0);
        sqlite3_finalize(stmt);
        return;
    }

    // 绑定参数（避免SQL注入）
    sqlite3_bind_text(stmt, 1, msg->name, -1, SQLITE_STATIC); // 用户名
    sqlite3_bind_text(stmt, 2, msg->data, -1, SQLITE_STATIC); // 密码

    // 执行插入
    int ret = sqlite3_step(stmt);
    if (ret == SQLITE_DONE)
    {
        strcpy(msg->data, "OK"); // 注册成功
    }
    else if (ret == SQLITE_CONSTRAINT)
    {
        sprintf(msg->data, "用户%s已存在", msg->name); // 精确捕获主键冲突
    }
    else
    {
        sprintf(msg->data, "注册失败：%s", sqlite3_errmsg(db));
    }

    // 清理资源
    sqlite3_finalize(stmt);
    send(connectfd, msg, sizeof(MSG), 0);
}

// 登录处理函数
void handle_login(int connectfd, MSG *msg, sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT 1 FROM usr WHERE name = ? AND pass = ?"; // 只查存在性，更高效

    // 准备SQL语句
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        sprintf(msg->data, "登录失败：%s", sqlite3_errmsg(db));
        send(connectfd, msg, sizeof(MSG), 0);
        sqlite3_finalize(stmt);
        return;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, msg->name, -1, SQLITE_STATIC);
    sqlite3_bind_text(stmt, 2, msg->data, -1, SQLITE_STATIC);

    // 执行查询（只需判断是否有结果）
    int ret = sqlite3_step(stmt);
    if (ret == SQLITE_ROW)
    {
        strcpy(msg->data, "OK"); // 登录成功
    }
    else
    {
        strcpy(msg->data, "用户名或密码错误");
    }

    // 清理资源（避免原代码中sqlite3_get_table的内存泄漏）
    sqlite3_finalize(stmt);
    send(connectfd, msg, sizeof(MSG), 0);
}

// 处理客户端子进程函数
void handle_client(int connectfd, sqlite3 *db)
{
    MSG msg;
    while (recv(connectfd, &msg, sizeof(MSG), 0) > 0)
    {
        printf("操作类型：%d\n", msg.type);
        printf("数据data：%s\n", msg.data);

        switch (msg.type)
        {
        case R: // 注册
            // 调用注册处理函数
            handle_register(connectfd, &msg, db);
            break;
        case L: // 登录
            // 调用登录处理函数
            handle_login(connectfd, &msg, db);
            break;
        case Q: // 查询单词
            // 调用查询单词处理函数
            handle_query(connectfd, &msg, db);
            break;
        case H: // 查询历史记录
            // 调用查询历史记录处理函数
            handle_history(connectfd, &msg, db);
            break;
        default:
            break;
        }
    }
    printf("客户端退出！\n");
    close(connectfd);
    return;
}

int handle_searchword(int connectfd, MSG *msg)
{
    FILE *fp;

    // 存储词典文件每行内容
    char temp[300];
    char *p;

    // 查询单词长度
    int len = strlen(msg->data);

    if ((fp = fopen("dict.txt", "r")) == NULL)
    {
        strcpy(msg->data, "词典文件无法打开");
        send(connectfd, msg, sizeof(MSG), 0);
        return 0;
    }

    while (fgets(temp, 300, fp) != NULL)
    {
        int result = strncmp(msg->data, temp, len);

        // 精确匹配：匹配单词且后面是空格
        if (result == 0 && temp[len] == ' ')
        {
            p = temp + len; // 移动到单词的末尾
            while (*p == ' ')
                p++; // 跳过空格

            // 复制解释
            strcpy(msg->data, p);
            fclose(fp);
            return 1; // 找到单词
        }
    }

    strcpy(msg->data, "未找到单词");
    fclose(fp);
    return 0;
}

void getdate(char *data)
{
    time_t t;
    struct tm *tp;
    time(&t);           // 获取当前时间
    tp = localtime(&t); // 将当前时间转化为本地时间

    // 格式化为YYYY-MM-DD HH:MM:SS
    sprintf(data, "%d-%02d-%02d %02d:%02d:%02d", 1900 + tp->tm_year, 1 + tp->tm_mon, tp->tm_mday, tp->tm_hour, tp->tm_min, tp->tm_sec);
}

// 执行查询函数
void handle_query(int connectfd, MSG *msg, sqlite3 *db)
{
    char date[128], word[128];
    sqlite3_stmt *stmt = NULL;
    const char *sql = "INSERT INTO record (name, date, word) VALUES (?, ?, ?)";

    strcpy(word, msg->data); // 保存原始查询词
    int found = handle_searchword(connectfd, msg);// 执行搜索单词函数

    if (found == 1)
    {
        getdate(date);
        // 准备SQL
        if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
        {
            printf("历史记录插入失败：%s\n", sqlite3_errmsg(db));
            sqlite3_finalize(stmt);
            send(connectfd, msg, sizeof(MSG), 0);
            return;
        }

        // 绑定参数
        sqlite3_bind_text(stmt, 1, msg->name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, date, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 3, word, -1, SQLITE_STATIC);

        // 执行插入
        if (sqlite3_step(stmt) != SQLITE_DONE)
        {
            printf("历史记录插入失败：%s\n", sqlite3_errmsg(db));
        }
        sqlite3_finalize(stmt);

        // 插入后清理旧记录（保证不超过最大限制10条）
        clean_old_records(msg->name, db);
    }

    send(connectfd, msg, sizeof(MSG), 0);
}

// 历史记录回调函数
int history_callback(void *arg, int f_num, char **f_value, char **f_name)
{
    int connectfd = *(int *)arg; // 获取连接套接字

    MSG msg;

    // 时间 ： 单词
    sprintf(msg.data, "%s:%s", f_value[1], f_value[2]);
    send(connectfd, &msg, sizeof(msg), 0); // 发送历史记录
    return 0;                              // 继续处理下一条语句
}

// 历史查询函数
void handle_history(int connectfd, MSG *msg, sqlite3 *db)
{
    sqlite3_stmt *stmt = NULL;
    const char *sql = "SELECT * FROM record WHERE name = ? ORDER BY date DESC";//按查询时间降序排列

    // 准备SQL
    if (sqlite3_prepare_v2(db, sql, -1, &stmt, NULL) != SQLITE_OK)
    {
        printf("历史查询失败：%s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt);
        return;
    }

    // 绑定参数
    sqlite3_bind_text(stmt, 1, msg->name, -1, SQLITE_STATIC);

    // 遍历结果并调用回调
    int ret;
    int has_record = 0; // 标记是否有历史记录

    while ((ret = sqlite3_step(stmt)) == SQLITE_ROW)
    {
        has_record = 1;
        char *f_value[3] = {
            (char *)sqlite3_column_text(stmt, 0),
            (char *)sqlite3_column_text(stmt, 1),
            (char *)sqlite3_column_text(stmt, 2)};
        char *f_name[3] = {"name", "date", "word"};
        history_callback(&connectfd, 3, f_value, f_name);
    }

    if (ret != SQLITE_DONE)
    {
        printf("历史查询失败：%s\n", sqlite3_errmsg(db));
        sqlite3_finalize(stmt); // 释放结果集
        return;
    }

    // 若没有历史记录，则发送提示信息
    if (has_record == 0)
    {
        MSG empty_msg;
        strcpy(empty_msg.data, "暂无查询历史");
        // 检查发送结果
        if (send(connectfd, &empty_msg, sizeof(empty_msg), 0) < 0)
        {
            printf("发送空历史提示失败");
            return ;
        }
    }

    // 清理资源
    sqlite3_finalize(stmt);
    // 发送结束标记
    strcpy(msg->data, "**OVER**");
    send(connectfd, msg, sizeof(MSG), 0);
}

// 清理用户超过最大限制的旧记录（保留最新的10条）
void clean_old_records(const char *name, sqlite3 *db)
{
    sqlite3_stmt *stmt;
    // 1. 查询用户当前记录总数
    const char *count_sql = "SELECT COUNT(*) FROM record WHERE name = ?";
    sqlite3_prepare_v2(db, count_sql, -1, &stmt, NULL);
    sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
    sqlite3_step(stmt);
    int total = sqlite3_column_int(stmt, 0);
    sqlite3_finalize(stmt);

    // 2. 若超过最大限制，删除最早的记录
    if (total > MAX_RECORDS)
    {
        int delete_count = total - MAX_RECORDS; // 需要删除的数量
        const char *del_sql = "DELETE FROM record WHERE name = ? AND date IN ("
                              "SELECT date FROM record WHERE name = ? ORDER BY date ASC LIMIT ?)";
        sqlite3_prepare_v2(db, del_sql, -1, &stmt, NULL);
        sqlite3_bind_text(stmt, 1, name, -1, SQLITE_STATIC);
        sqlite3_bind_text(stmt, 2, name, -1, SQLITE_STATIC);
        sqlite3_bind_int(stmt, 3, delete_count);
        sqlite3_step(stmt);
        sqlite3_finalize(stmt);
    }
}

int main(int argc, char *argv[])
{

    // 声明
    int listenfd, connectfd;        // 监听套接字、连接套接字
    struct sockaddr_in server_addr; // 服务器地址结构
    pid_t pid;                      // 进程ID用于多进程
    sqlite3 *db;                    // sqlite3数据库指针

    if (argc < 3)
    {
        printf("格式：%s <ip> <端口>\n", argv[0]);
        return 1;
    }

    if (sqlite3_open(DATABASE, &db) != SQLITE_OK)
    {
        printf("error:%s\n", sqlite3_errmsg(db));
        return -1;
    }

    const char *create_usr_table = "CREATE TABLE IF NOT EXISTS usr("
                                   "name TEXT PRIMARY KEY NOT NULL,"
                                   "pass TEXT NOT NULL);";

    const char *create_record_table = "CREATE TABLE IF NOT EXISTS record("
                                      "name TEXT NOT NULL,"
                                      "date TEXT NOT NULL,"
                                      "word TEXT NOT NULL);";
    // 为record表的name+date字段创建复合索引（加速历史查询）
    const char *create_record_index = "CREATE INDEX IF NOT EXISTS idx_record_name_date "
                                      "ON record(name, date);"; // 复合索引优化

    char *errmsg = NULL;
    if (sqlite3_exec(db, create_usr_table, NULL, NULL, &errmsg) != SQLITE_OK)
    {
        printf("创建用户表失败：%s\n", errmsg);
        sqlite3_free(errmsg); // 释放错误信息内存
        sqlite3_close(db);
        return -1;
    }

    if (sqlite3_exec(db, create_record_table, NULL, NULL, &errmsg) != SQLITE_OK)
    {
        printf("创建历史记录表失败：%s\n", errmsg);
        sqlite3_free(errmsg); // 释放错误信息内存
        sqlite3_close(db);
        return -1;
    }

    if (sqlite3_exec(db, create_record_index, NULL, NULL, &errmsg) != SQLITE_OK)
    {
        printf("创建历史记录索引失败：%s\n", errmsg);
        sqlite3_free(errmsg);
        sqlite3_close(db);
        return -1;
    }

    // 创建套接字
    if ((listenfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("创建监听套接字失败：");
        return -1;
    }

    bzero(&server_addr, sizeof(server_addr)); // 清空地址结构
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);

    // 绑定
    if (bind(listenfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("绑定失败：");
        close(listenfd);
        return -1;
    }

    // 监听
    if (listen(listenfd, 5) < 0)
    {
        perror("监听失败：");
        close(listenfd);
        return -1;
    }
    printf("服务器启动成功!!!\n");
    signal(SIGCHLD, handler); // 注册SIGCHLD信号处理函数，避免僵尸进程占用资源

    while (1)
    {
        if ((connectfd = accept(listenfd, NULL, NULL)) < 0)
        {
            perror("accept 失败：");
            break;
        }

        if ((pid = fork()) < 0)
        {
            perror("fork失败：");
            break;
        }
        else if (pid == 0) //子进程
        {
            close(listenfd);
            // 调用处理客户端请求函数
            handle_client(connectfd, db);
            sqlite3_close(db); // 子进程关闭数据库
            exit(0);
        }
        else //父进程
        {
            // 父进程负责连接
            close(connectfd);
        }
    }
    sqlite3_close(db); // 主进程关闭数据库
    close(listenfd);
    return 0;
}