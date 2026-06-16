# Linux Network Dictionary / Linux 网络词典

一个运行在 Linux 上、使用 C 语言编写的客户端/服务端词典项目。  
A Linux-only client/server dictionary application written in C.

这个项目实现了一个基于 TCP 的小型网络词典系统，包含终端客户端、多进程服务端、基于 SQLite 的用户存储，以及轻量级定长消息协议。它适合作为一个紧凑的网络编程与系统编程练习，涵盖 socket、进程管理、文件加载、身份认证和查询历史持久化等内容。  
This project implements a small TCP dictionary system with a terminal client, a multi-process server, SQLite-backed user storage, and a lightweight fixed-size message protocol. It is a compact networking and systems-programming exercise that combines sockets, process management, file loading, authentication, and persistent query history.

仓库地址 / Repository: <https://github.com/xiao-wu1004/Linux-network-dictionary>

## 功能特性 | Features

- 基于 Linux 的 TCP 客户端/服务端词典查询 / TCP client/server dictionary lookup on Linux
- 交互式终端客户端，支持注册、登录、单词查询和历史查询 / Interactive terminal client with register, login, word query, and history query flows
- 基于 `fork` 的并发服务端，可同时处理多个客户端 / Fork-based concurrent server for handling multiple clients
- 使用定长 `MSG` 协议，并完整处理短读短写 / Fixed-width `MSG` protocol with complete short-read and short-write handling
- 使用 SQLite 存储用户和查询历史 / SQLite-based user and history storage
- 使用 OpenSSL 实现带盐 `SHA-256` 密码存储 / Salted SHA-256 password storage using OpenSSL
- 使用内存哈希表索引词典，提升查询效率 / In-memory hash-table dictionary index for faster queries
- 由服务端维护认证会话，防止客户端伪造用户名 / Server-side authenticated session state to prevent forged client usernames
- 提供覆盖主流程和数据库行为的 smoke test / Smoke tests covering the main protocol flow and database behavior

## 技术栈 | Tech Stack

- 语言 / Language: C11
- 网络 / Networking: POSIX sockets
- 持久化 / Persistence: SQLite3
- 密码哈希 / Password hashing: OpenSSL `SHA-256`
- 构建 / Build: `make`
- 测试辅助 / Test helper: Python 3 + Bash

## 项目结构 | Project Layout

```text
.
|-- dict_client.c
|-- dict_common.c
|-- dict_common.h
|-- dict_server.c
|-- dict.txt
|-- Makefile
|-- README.md
`-- tests
    |-- smoke_client.py
    `-- smoke_test.sh
```

## 工作原理 | How It Works

服务端启动时会一次性加载 `dict.txt`，并构建内存哈希表用于单词查询。客户端通过 TCP 连接服务端，并使用固定大小的 `MSG` 结构进行通信。用户登录成功后，服务端会把认证身份保存在连接对应的会话中；后续查询历史时，服务端使用的是会话里的真实身份，而不是客户端消息里可伪造的用户名字段。  
The server loads `dict.txt` once at startup and builds an in-memory hash table for word lookups. Clients connect over TCP and communicate with the server using a fixed-size `MSG` structure. After a user logs in successfully, the server stores the authenticated identity in the connection session and uses that identity for query history, regardless of the username embedded in later client messages.

查询成功的单词会被写入 SQLite，每个用户最多保留最近 10 条查询记录。  
Successful word queries are stored in SQLite and the history for each user is capped at the latest 10 records.

## 环境要求 | Requirements

- Linux
- `gcc`
- `make`
- `libsqlite3-dev`
- `libssl-dev`
- `python3`
- `bash`

Ubuntu 或 Debian 安装示例 / Example installation on Ubuntu or Debian:

```bash
sudo apt-get update
sudo apt-get install build-essential libsqlite3-dev libssl-dev python3 bash
```

## 编译 | Build

编译服务端和客户端 / Build both server and client:

```bash
make
```

可用目标 / Available targets:

- `make all`
- `make server`
- `make client`
- `make clean`
- `make run-server`
- `make run-client`

## 快速开始 | Quick Start

1. 编译项目 / Build the project:

```bash
make
```

2. 启动服务端 / Start the server:

```bash
./dict_server 127.0.0.1 8888
```

3. 打开另一个终端并启动客户端 / Open another terminal and start the client:

```bash
./dict_client 127.0.0.1 8888
```

4. 在客户端菜单中执行以下操作 / Use the client menu to:

- 注册账号 / register an account
- 登录 / log in
- 查询单词 / search for words
- 查看历史记录 / check query history

## 客户端流程 | Client Flow

登录前 / Before login:

- `1` 注册 / Register
- `2` 登录 / Login
- `3` 退出 / Exit

登录后 / After login:

- `1` 查询单词 / Query word
- `2` 查询历史 / Query history
- `3` 退出 / Exit

查询单词时输入 `#` 可退出查询循环。  
When querying words, enter `#` to leave the query loop.

## 协议说明 | Protocol

客户端和服务端使用固定宽度的消息结构通信。  
The client and server exchange a fixed-width message structure.

```c
typedef struct {
    int type;
    char name[16];
    char data[256];
} MSG;
```

消息类型 / Message types:

- `1`: 注册 / register
- `2`: 登录 / login
- `3`: 查询单词 / query word
- `4`: 查询历史 / query history

常见状态字段 / Common status payloads:

- `OK`: 成功 / success
- `NOT_FOUND`: 未找到单词 / word not found
- `NO_HISTORY`: 没有历史记录 / no history
- `ERR_NOT_AUTHENTICATED`: 未认证 / not authenticated
- `ERR_INVALID_INPUT`: 输入无效 / invalid input
- `ERR_INTERNAL`: 服务端内部错误 / internal server error
- `ERR_USER_EXISTS`: 用户已存在 / user already exists
- `ERR_INVALID_CREDENTIALS`: 用户名或密码错误 / invalid username or password
- `ERR_NETWORK`: 网络通信错误 / network communication failed

历史记录响应以如下标记结束 / History responses end with:

```text
**OVER**
```

实现中通过 `send_all()` 和 `recv_all()` 保证每个逻辑 `MSG` 都被完整发送和接收，即使底层 socket 出现短读或短写。  
The implementation uses `send_all()` and `recv_all()` helpers so each logical `MSG` is transferred completely even when the underlying socket performs short reads or short writes.

## 认证与存储 | Authentication and Storage

### 用户名与密码规则 | Username and Password Rules

- 用户名长度为 `1-15` 个字符 / Username length: `1-15` characters
- 用户名仅允许字母、数字和下划线 / Allowed username characters: letters, numbers, underscore
- 密码不能为空 / Password must be non-empty
- 密码不能包含控制字符 / Password cannot contain control characters

### 密码存储 | Password Storage

密码不会以明文形式存储。服务端会生成随机盐，并保存以下字段：  
Passwords are not stored in plaintext. The server generates a random salt and stores:

- `salt_hex`
- `pass_hash_hex`

哈希格式 / Hash format:

```text
SHA-256(salt + password)
```

### SQLite 表结构 | SQLite Tables

```sql
CREATE TABLE usr (
  name TEXT PRIMARY KEY NOT NULL,
  salt_hex TEXT NOT NULL,
  pass_hash_hex TEXT NOT NULL
);

CREATE TABLE record (
  id INTEGER PRIMARY KEY AUTOINCREMENT,
  name TEXT NOT NULL,
  created_at TEXT NOT NULL,
  word TEXT NOT NULL
);
```

说明 / Notes:

- 数据库文件为 `my.db` / The database file is `my.db`
- 服务端启用了 SQLite WAL 模式 / The server enables SQLite WAL mode
- 使用 `PRAGMA user_version` 管理 schema 版本 / The schema is versioned with `PRAGMA user_version`
- 如果发现旧版本 schema，服务端会重建表结构 / If an old schema version is detected, the server rebuilds the schema

## 词典数据 | Dictionary Data

- 词典文件 / Source file: `dict.txt`
- 当前仓库词典规模约为 `7973` 行 / Current repository dictionary size: about `7973` lines
- 预期格式为每行一条记录 / Expected format: one entry per line

示例 / Example:

```text
word meaning
```

服务端启动时会执行以下操作 / At startup the server:

- 一次性加载词典文件 / loads the dictionary file once
- 跳过空行和格式错误的行 / skips malformed or empty lines
- 将条目存入内存哈希表 / stores entries in an in-memory hash table

这样可以避免每次查询都重新扫描文本文件。  
This avoids rescanning the text file for every query request.

## 测试 | Testing

运行 smoke test / Run the smoke test:

```bash
bash tests/smoke_test.sh
```

该测试会执行以下步骤 / The smoke test will:

- 删除已有测试数据库 / remove the existing test database
- 重新编译项目 / rebuild the project
- 在本地启动服务端 / start the server locally
- 使用 Python 客户端进行协议检查 / run protocol checks with a Python client
- 校验数据库内容 / verify database contents

覆盖场景 / Covered scenarios:

- 未登录查询被拒绝 / unauthenticated query rejection
- 用户注册 / user registration
- 登录 / login
- 命中词典 / dictionary hit
- 未命中词典 / dictionary miss
- 查询历史记录 / history retrieval
- 每个用户历史记录最多保留 10 条 / per-user history retention capped at 10 rows
- 密码已加盐并哈希存储 / salted and hashed password storage

## 日志 | Logging

服务端会输出带时间戳的轻量日志，包括：  
The server prints lightweight logs with timestamps, including:

- 启动信息 / startup
- 词典加载结果 / dictionary load result
- 客户端连接与断开 / client connect and disconnect
- 认证成功与失败 / authentication success and failure
- 查询命中与未命中 / query hit and miss
- 数据库和协议错误 / database and protocol errors

## 限制说明 | Limitations

- 客户端密码输入依赖 `termios`，因此客户端行为是 Linux 专用的 / Linux-only client behavior because password input depends on `termios`
- 当前仅支持 IPv4 / IPv4 only
- 不包含 TLS 或传输层加密 / No TLS or transport encryption
- 历史记录保存的是查询过的单词，不是完整释义 / Query history stores searched words, not full definitions
- 当前更适合本地环境或受信任网络使用 / The project currently focuses on local or trusted-network usage

## 学习方向 | Learning Goals

如果你想学习以下内容，这个项目可以作为一个不错的参考：  
This project is a good reference if you want to study:

- C 语言 socket 网络编程 / socket programming in C
- 客户端/服务端协议设计 / client/server protocol design
- 基于 `fork` 的并发处理 / fork-based concurrency
- C 语言中集成 SQLite / SQLite integration in C
- 基础凭证哈希与带盐密码存储 / basic credential hashing and salted password storage
- 输入校验与更稳妥的服务端会话控制 / input validation and defensive server-side session handling

## 许可证 | License

当前仓库还没有包含许可证文件。如果你计划公开发布或允许他人复用，建议补充一个如 MIT 的许可证。  
No license file is currently included in this repository. If you plan to publish or reuse it openly, adding a license such as MIT would be a good next step.
