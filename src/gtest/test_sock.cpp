#include <gtest/gtest.h>
#include <thread>

#include "compat.h"
#include "util/sock.h"

static bool SocketIsClosed(const SOCKET& s)
{
    // Notice that if another thread is running and creates its own socket after `s` has been
    // closed, it may be assigned the same file descriptor number. In this case, our test will
    // wrongly pretend that the socket is not closed.
    int type;
    socklen_t len = sizeof(type);
    return getsockopt(s, SOL_SOCKET, SO_TYPE, (sockopt_arg_type)&type, &len) == SOCKET_ERROR;
}

static SOCKET CreateSocket()
{
    const SOCKET s = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    assert(s != static_cast<SOCKET>(SOCKET_ERROR));
    return s;
}


TEST(Sock, ConstructorDestructor) {
    SOCKET s = CreateSocket();
    {
        Sock sock(s);
        ASSERT_EQ(sock.Get(), s);
        ASSERT_FALSE(SocketIsClosed(s));
    }
    ASSERT_TRUE(SocketIsClosed(s));
}

TEST(Sock, MoveConstructor) {
    SOCKET s = CreateSocket();
    Sock sock(s);
    ASSERT_EQ(sock.Get(), s);
    ASSERT_FALSE(SocketIsClosed(s));

    Sock sock2(std::move(sock));
    ASSERT_EQ(sock.Get(), INVALID_SOCKET);
    ASSERT_EQ(sock2.Get(), s);
    ASSERT_FALSE(SocketIsClosed(s));
}

TEST(Sock, MoveAssignment) {
    SOCKET s = CreateSocket();
    Sock sock(s);
    ASSERT_EQ(sock.Get(), s);
    ASSERT_FALSE(SocketIsClosed(s));

    Sock sock2 = std::move(sock);
    ASSERT_EQ(sock.Get(), INVALID_SOCKET);
    ASSERT_EQ(sock2.Get(), s);
    ASSERT_FALSE(SocketIsClosed(s));
}

TEST(Sock, Reset) {
    const SOCKET s = CreateSocket();
    Sock sock(s);
    ASSERT_FALSE(SocketIsClosed(s));
    sock.Reset();
    ASSERT_TRUE(SocketIsClosed(s));
}

#ifndef WIN32 // Windows does not have socketpair(2).

static void CreateSocketPair(int s[2]) {
    assert(socketpair(AF_UNIX, SOCK_STREAM, 0, s) == 0);
}

static void SendAndRecvMessage(const Sock& sender, const Sock& receiver) {
    const char* msg = "abcd";
    constexpr size_t msg_len = 4;
    char recv_buf[10];

    ASSERT_EQ(sender.Send(msg, msg_len, 0), msg_len);
    ASSERT_EQ(receiver.Recv(recv_buf, sizeof(recv_buf), 0), msg_len);
    ASSERT_EQ(strncmp(msg, recv_buf, msg_len), 0);
}

TEST(Sock, SendAndReceive) {
    int s[2];
    CreateSocketPair(s);

    {
        Sock sock0(s[0]);
        Sock sock1(s[1]);

        SendAndRecvMessage(sock0, sock1);

        Sock sock0moved = std::move(sock0);
        Sock sock1moved = std::move(sock1);

        SendAndRecvMessage(sock1moved, sock0moved);
    }

    ASSERT_TRUE(SocketIsClosed(s[0]));
    ASSERT_TRUE(SocketIsClosed(s[1]));
}

TEST(Sock, Wait)
{
    int s[2];
    CreateSocketPair(s);

    Sock sock0(s[0]);
    Sock sock1(s[1]);

    constexpr int64_t millis_in_day = 24 * 60 * 60 * 1000;
    std::thread waiter([&sock0]() { ASSERT_EQ(sock0.Wait(millis_in_day, Sock::RECV), 1); });

    ASSERT_EQ(sock1.Send("a", 1, 0), 1);

    waiter.join();
}

#endif /* WIN32 */

