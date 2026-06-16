#!/usr/bin/env python3
import random
import socket
import sqlite3
import string
import struct
import sys
import time

MSG_STRUCT = struct.Struct("=i16s256s")

MSG_TYPE_REGISTER = 1
MSG_TYPE_LOGIN = 2
MSG_TYPE_QUERY = 3
MSG_TYPE_HISTORY = 4

OK = "OK"
NOT_FOUND = "NOT_FOUND"
NO_HISTORY = "NO_HISTORY"
OVER = "**OVER**"
ERR_AUTH = "ERR_NOT_AUTHENTICATED"


def _encode_field(value: str, size: int) -> bytes:
    raw = value.encode("utf-8")
    if len(raw) >= size:
        raise ValueError(f"value too large for field of size {size}: {value!r}")
    return raw.ljust(size, b"\0")


def _decode_field(value: bytes) -> str:
    return value.split(b"\0", 1)[0].decode("utf-8")


def send_msg(sock: socket.socket, msg_type: int, name: str = "", data: str = "") -> None:
    sock.sendall(MSG_STRUCT.pack(msg_type, _encode_field(name, 16), _encode_field(data, 256)))


def recv_msg(sock: socket.socket) -> tuple[int, str, str]:
    payload = b""
    while len(payload) < MSG_STRUCT.size:
        chunk = sock.recv(MSG_STRUCT.size - len(payload))
        if not chunk:
            raise RuntimeError("socket closed while receiving MSG payload")
        payload += chunk
    msg_type, name, data = MSG_STRUCT.unpack(payload)
    return msg_type, _decode_field(name), _decode_field(data)


def assert_equal(actual, expected, message):
    if actual != expected:
        raise AssertionError(f"{message}: expected {expected!r}, got {actual!r}")


def assert_true(condition, message):
    if not condition:
        raise AssertionError(message)


def random_user(prefix: str) -> str:
    suffix = "".join(random.choice(string.ascii_lowercase + string.digits) for _ in range(6))
    return f"{prefix}{suffix}"[:15]


def run(host: str, port: int) -> None:
    password = "TestPass123"
    user_one = random_user("smokea_")
    user_two = random_user("smokeb_")
    known_words = [
        "abandon",
        "abandonment",
        "abbreviation",
        "abeyance",
        "abide",
        "ability",
        "able",
        "abnormal",
        "abolish",
        "about",
        "above",
    ]

    with socket.create_connection((host, port), timeout=5) as first_sock:
        send_msg(first_sock, MSG_TYPE_QUERY, "forged_user", "abandon")
        _, _, data = recv_msg(first_sock)
        assert_equal(data, ERR_AUTH, "unauthenticated query must be rejected")

        send_msg(first_sock, MSG_TYPE_REGISTER, user_one, password)
        _, _, data = recv_msg(first_sock)
        assert_equal(data, OK, "first user registration failed")

        send_msg(first_sock, MSG_TYPE_REGISTER, user_two, password)
        _, _, data = recv_msg(first_sock)
        assert_equal(data, OK, "second user registration failed")

        send_msg(first_sock, MSG_TYPE_LOGIN, user_one, password)
        _, _, data = recv_msg(first_sock)
        assert_equal(data, OK, "login failed")

        send_msg(first_sock, MSG_TYPE_QUERY, "forged_user", "abandon")
        _, _, data = recv_msg(first_sock)
        assert_true(data not in (ERR_AUTH, NOT_FOUND), "authenticated query should return a definition")

        for word in known_words:
            send_msg(first_sock, MSG_TYPE_QUERY, "still_forged", word)
            _, _, data = recv_msg(first_sock)
            assert_true(data not in (ERR_AUTH, NOT_FOUND), f"expected dictionary hit for {word}")
            time.sleep(0.02)

        send_msg(first_sock, MSG_TYPE_QUERY, "still_forged", "definitely_missing_word")
        _, _, data = recv_msg(first_sock)
        assert_equal(data, NOT_FOUND, "missing word should return NOT_FOUND")

        send_msg(first_sock, MSG_TYPE_HISTORY, "forged_user", "")
        history_rows = []
        while True:
            _, _, data = recv_msg(first_sock)
            if data == OVER:
                break
            if data == NO_HISTORY:
                break
            history_rows.append(data)

        assert_true(history_rows, "expected non-empty history")
        assert_equal(len(history_rows), 10, "history response should retain only 10 rows")
        assert_true(any("above" in row for row in history_rows), "latest queried word should appear in history")

    with sqlite3.connect("my.db") as db:
        user_rows = db.execute(
            "SELECT name, salt_hex, pass_hash_hex FROM usr WHERE name IN (?, ?) ORDER BY name ASC",
            (user_one, user_two),
        ).fetchall()
        assert_equal(len(user_rows), 2, "expected both users to exist in database")
        assert_true(all(row[1] and row[2] for row in user_rows), "salt/hash columns must be populated")
        assert_true(all(row[2] != password for row in user_rows), "password hash must not equal plaintext")
        assert_true(user_rows[0][1] != user_rows[1][1], "salts should differ for equal passwords")
        assert_true(user_rows[0][2] != user_rows[1][2], "hashes should differ for equal passwords")

        history_count = db.execute(
            "SELECT COUNT(*) FROM record WHERE name = ?",
            (user_one,),
        ).fetchone()[0]
        assert_equal(history_count, 10, "database should retain only 10 history rows")


if __name__ == "__main__":
    if len(sys.argv) != 3:
        print(f"Usage: {sys.argv[0]} <host> <port>", file=sys.stderr)
        sys.exit(1)

    run(sys.argv[1], int(sys.argv[2]))
