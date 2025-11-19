#include <stdio.h>
#include <stdlib.h>
#include <unistd.h>
#include <string.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <termios.h> // 终端属性控制
#include <unistd.h>  // 提供STDIN_FILENO等定义

#define N 16 // 用户名最大长度
#define R 1  // 注册
#define L 2  // 登录
#define Q 3  // 查询单词
#define H 4  // 查询历史记录

typedef struct
{
    int type;
    char name[N];
    char data[256];
} MSG;

void handle_register(int sockfd, MSG *msg); // 处理注册函数
int handle_login(int sockfd, MSG *msg);     // 处理登录函数
void query_menu(int sockfd, MSG *msg);      // 查询菜单函数
void handle_query(int sockfd, MSG *msg);    // 处理查询函数
void handle_history(int sockfd, MSG *msg);  // 处理历史记录函数

void read_password(const char *prompt, char *password, int max_len); // 读取密码（无明文显示，支持退格）

// 处理注册函数
void handle_register(int sockfd, MSG *msg)
{
    msg->type = R;

    printf("请输入您的用户名：");
    scanf("%s", msg->name);

    // printf("请输入您的密码：");
    // scanf("%s", msg->data);

    // 清空输入缓冲区（避免残留的回车影响后续输入）
    while (getchar() != '\n');
    read_password("请输入您的密码：", msg->data, sizeof(msg->data));

    // 发送注册请求
    send(sockfd, msg, sizeof(MSG), 0);

    // 接收响应
    recv(sockfd, msg, sizeof(MSG), 0);
    printf("注册：%s\n", msg->data);
}

// 处理登录函数
int handle_login(int sockfd, MSG *msg)
{
    // 设置操作类型为L
    msg->type = L;

    printf("请输入您的用户名：");
    scanf("%s", msg->name);

    // printf("请输入您的密码：");
    // scanf("%s", msg->data);

    // 清空输入缓冲区
    while (getchar() != '\n');

    // 读取密码（替换原来的scanf）
    read_password("请输入您的密码：", msg->data, sizeof(msg->data));

    // 发送登录请求
    send(sockfd, msg, sizeof(MSG), 0);

    // 接收响应
    recv(sockfd, msg, sizeof(MSG), 0);

    if (strncmp(msg->data, "OK", 3) == 0)
    {
        printf("登录成功！\n");
        return 1;
    }

    printf("登录失败：%s\n", msg->data);
    return 0;
}

void query_menu(int sockfd, MSG *msg)
{
    int choose = 0;
    while (1)
    {

        // 打印功能菜单
        printf("*****************************************\n");
        printf("* 1.查询单词    2.查询历史记录    3.退出*\n");
        printf("*****************************************\n");
        // printf("请输入功能菜单功能码：（1|2|3）\n");

        // if (scanf("%d", &choose) <= 0)
        // {
        //     perror("接收输入失败");
        //     return;
        // }
        // 严格检查输入：必须是1、2、3
        do
        {
            printf("请输入功能菜单功能码：（1|2|3）\n");
            int ret = scanf("%d", &choose); // 读取输入
            if (ret != 1)
            { // 输入非整数
                printf("输入错误！请输入数字1(查询单词)、2(查询历史记录)或3(退出)\n");
                // 清空输入缓冲区（避免残留字符影响下次输入）
                while (getchar() != '\n');
            }
            else if (choose < 1 || choose > 3)
            { // 整数但不在范围内
                printf("输入错误！请输入数字1(查询单词)、2(查询历史记录)或3(退出)\n");
            }
            else
            {
                break; // 输入有效，退出检查循环
            }
        } while (1);

        switch (choose)
        {
        case 1: // 查询单词
            // 调用查询单词函数
            handle_query(sockfd, msg);
            break;
        case 2: // 查询历史记录
            // 调用查询历史记录函数
            handle_history(sockfd, msg);
            break;
        case 3:
            close(sockfd);
            exit(0);
        default:
            break;
        }
    }
}

// 单词查询函数
void handle_query(int sockfd, MSG *msg)
{
    msg->type = Q;
    puts("------------------------------");

    while (1)
    {
        printf("请输入要查询的单词（输入#退出）\n");
        scanf("%s", msg->data);

        // 退出条件
        if (strcmp(msg->data, "#") == 0)
        {
            break;
        }
        printf("查询中...\n");
        send(sockfd, msg, sizeof(MSG), 0);

        recv(sockfd, msg, sizeof(MSG), 0);

        printf(">>> %s\n", msg->data);
    }
}

// 查询历史记录函数
void handle_history(int sockfd, MSG *msg)
{
    msg->type = H;

    // 发送查询历史记录请求
    send(sockfd, msg, sizeof(MSG), 0);

    // 循环接收历史记录
    while (1)
    {
        recv(sockfd, msg, sizeof(MSG), 0);

        // 结束标记检查 **OVER**
        if (strcmp(msg->data, "**OVER**") == 0)
        {
            break;
        }

        printf("%s\n", msg->data);
    }
}

// 读取密码（无明文显示，支持退格）
void read_password(const char *prompt, char *password, int max_len)
{
    struct termios old_attr, new_attr;
    int i = 0;
    char c;

    // 保存终端原始属性
    if (tcgetattr(STDIN_FILENO, &old_attr) != 0)
    {
        perror("获取终端属性失败");
        exit(EXIT_FAILURE);
    }

    // 复制原始属性并修改（禁用回显）
    new_attr = old_attr;
    new_attr.c_lflag &= ~(ECHO); // 关闭回显（输入不显示）
    // new_attr.c_lflag &= ~(ECHO | ECHONL);  // 若希望回车也不显示，用这行

    // 应用新属性
    if (tcsetattr(STDIN_FILENO, TCSANOW, &new_attr) != 0)
    {
        perror("设置终端属性失败");
        exit(EXIT_FAILURE);
    }

    // 打印提示信息
    printf("%s", prompt);
    fflush(stdout); // 确保提示信息立即显示

    // 逐个读取字符（支持退格）
    while (i < max_len - 1)
    { // 留1个位置给字符串结束符'\0'
        c = getchar();

        if (c == '\n' || c == '\r')
        { // 回车结束输入
            break;
        }
        else if (c == 127 || c == '\b')
        { // 退格键（ASCII 127或'\b'）
            if (i > 0)
            {
                i--;
                // 退格后清除屏幕上的字符（光标左移1位，打印空格覆盖，再左移1位）
                printf("\b \b");
                fflush(stdout);
            }
        }
        else
        { // 普通字符
            password[i++] = c;
            // 可选：显示星号代替明文（若需要完全隐藏则删除此行）
            printf("*");
            fflush(stdout);
        }
    }
    password[i] = '\0'; // 字符串结束符
    printf("\n");       // 输入结束后换行

    // 恢复终端原始属性
    tcsetattr(STDIN_FILENO, TCSANOW, &old_attr);
}

int main(int argc, char *argv[])
{
    // 参数检查
    if (argc < 3)
    {
        printf("格式：%s <服务ip> <服务端口号>\n", argv[0]);
        return -1;
    }

    // 创建TCP套接字
    int sockfd;
    if ((sockfd = socket(AF_INET, SOCK_STREAM, 0)) < 0)
    {
        perror("创建套接字失败\n");
        return -1;
    }

    struct sockaddr_in server_addr;

    // 清空结构体
    bzero(&server_addr, sizeof(server_addr));
    server_addr.sin_family = AF_INET;
    server_addr.sin_port = htons(atoi(argv[2]));
    server_addr.sin_addr.s_addr = inet_addr(argv[1]);

    if (connect(sockfd, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0)
    {
        perror("连接失败");
        close(sockfd);
        return -1;
    }

    int choose;
    MSG msg;

    while (1)
    {
        // 打印主菜单
        printf("*****************************\n");
        printf("* 1.注册    2.登录    3.退出*\n");
        printf("*****************************\n");
        // printf("请输入主菜单功能码：（1|2|3）\n");

        // // 获取用户的选择
        // if (scanf("%d", &choose) <= 0)
        // {
        //     perror("获取输入失败");
        //     return -1;
        // }
        // 严格检查输入：必须是1、2、3
        do
        {
            printf("请输入主菜单功能码：（1|2|3）\n");
            int ret = scanf("%d", &choose); // 读取输入
            if (ret != 1)
            { // 输入非整数（如字母、符号等）
                printf("输入错误！请输入数字1(注册)、2(登录)或3(退出)\n");
                // 清空输入缓冲区，避免错误输入残留
                while (getchar() != '\n');
            }
            else if (choose < 1 || choose > 3)
            { // 整数但不在1-3范围内
                printf("输入错误！请输入数字1(注册)、2(登录)或3(退出)\n");
            }
            else
            {
                break; // 输入有效，退出检查循环
            }
        } while (1);

        switch (choose)
        {
        case 1: // 注册
            // 调用处理注册函数
            handle_register(sockfd, &msg);
            break;
        case 2: // 登录
            // 调用处理登录函数
            if (handle_login(sockfd, &msg) == 1)
            {
                // 调用跳转到功能菜单页面
                query_menu(sockfd, &msg);
            }
            break;
        case 3: // 退出
            close(sockfd);
            return 0;
        }
    }

    return 0;
}