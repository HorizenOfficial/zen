#include "compat.h"
#include "tinyformat.h"
#include "sock.h"
#include "util.h"

#include <codecvt>
#include <cwchar>
#include <locale>
#include <string>

#ifdef USE_POLL
#include <poll.h>
#endif

#include <openssl/ssl.h>
#include <openssl/err.h>

Sock::Sock() : m_socket(INVALID_SOCKET), m_ssl(nullptr) {}

Sock::Sock(SOCKET s, SSL* ssl) : m_socket(s), m_ssl(ssl) {}

Sock::Sock(Sock&& other)
{
    m_socket = other.m_socket;
    m_ssl    = other.m_ssl;
    other.m_socket = INVALID_SOCKET;
    other.m_ssl    = nullptr;
}

Sock::~Sock() { Reset(); }

Sock& Sock::operator=(Sock&& other)
{
    Reset();
    m_socket = other.m_socket;
    m_ssl    = other.m_ssl;
    other.m_socket = INVALID_SOCKET;
    other.m_ssl    = nullptr;
    return *this;
}

SOCKET Sock::Get() const { return m_socket; }

SSL* Sock::GetSSL() const { return m_ssl; }
bool Sock::SetSSL(SSL* ssl) {
    if (m_ssl) {
        SSL_free(m_ssl);
    }

    m_ssl = ssl;
    if (!m_ssl) {
        return false;
    }
    return SSL_set_fd(m_ssl, m_socket);
}

/*
SOCKET Sock::Release()
{
    const SOCKET s = m_socket;
    m_socket = INVALID_SOCKET;
    return s;
}
*/

bool Sock::Reset() { return Close(); }

ssize_t Sock::Send(const void* data, size_t len, int flags) const
{
    if (m_ssl) {
        ERR_clear_error(); // clear the error queue
        return SSL_write(m_ssl, static_cast<const char*>(data), len);
    }
    return send(m_socket, static_cast<const char*>(data), len, flags);
}

ssize_t Sock::Recv(void* buf, size_t len, int flags) const
{
    if (m_ssl) {
        ERR_clear_error(); // clear the error queue
        return SSL_read(m_ssl, static_cast<char*>(buf), len);
    }
    return recv(m_socket, static_cast<char*>(buf), len, flags);
}


int Sock::WaitMany(int64_t timeout, std::unordered_map<SOCKET, Events>& events_per_sock)
{
#ifdef USE_POLL
    std::vector<pollfd> pfds;
    for (auto& [sock, events] : events_per_sock) {
        pfds.emplace_back();
        auto& pfd = pfds.back();
        pfd.fd = sock;
        if (events.requested & RECV) {
            pfd.events |= POLLIN;
        }
        if (events.requested & SEND) {
            pfd.events |= POLLOUT;
        }
    }

    int ret = poll(pfds.data(), pfds.size(), timeout);
    if (ret == SOCKET_ERROR) {
        return -1;
    }

    assert(pfds.size() == events_per_sock.size());
    size_t i{0};
    for (auto& [sock, events] : events_per_sock) {
        assert(sock == static_cast<SOCKET>(pfds[i].fd));
        events.occurred = 0;
        if (pfds[i].revents & POLLIN) {
            events.occurred |= RECV;
        }
        if (pfds[i].revents & POLLOUT) {
            events.occurred |= SEND;
        }
        if (pfds[i].revents & (POLLERR | POLLHUP)) {
            events.occurred |= ERR;
        }
        ++i;
    }
    return ret;
#else
    fd_set recv;
    fd_set send;
    fd_set err;
    FD_ZERO(&recv);
    FD_ZERO(&send);
    FD_ZERO(&err);
    SOCKET socket_max{0};

    for (const auto& [s, events] : events_per_sock) {
        if (s >= FD_SETSIZE) {
            return false;
        }
        if (events.requested & RECV) {
            FD_SET(s, &recv);
        }
        if (events.requested & SEND) {
            FD_SET(s, &send);
        }
        FD_SET(s, &err);
        socket_max = std::max(socket_max, s);
    }

    timeval tv = MillisToTimeval(timeout);

    int ret = select(socket_max + 1, &recv, &send, &err, &tv);
    if (ret == SOCKET_ERROR) {
        return -1;
    }

    for (auto& [s, events] : events_per_sock) {
        events.occurred = 0;
        if (FD_ISSET(s, &recv)) {
            events.occurred |= RECV;
        }
        if (FD_ISSET(s, &send)) {
            events.occurred |= SEND;
        }
        if (FD_ISSET(s, &err)) {
            events.occurred |= ERR;
        }
    }

    return ret;
#endif /* USE_POLL */
}


int Sock::Wait(int64_t timeout, Event requested) const
{
    std::unordered_map<SOCKET, Events> events_per_sock = { {m_socket, Events(requested)} };
    return WaitMany(timeout, events_per_sock);
}


#ifdef WIN32
std::string NetworkErrorString(int err)
{
    wchar_t buf[256];
    buf[0] = 0;
    if(FormatMessageW(FORMAT_MESSAGE_FROM_SYSTEM | FORMAT_MESSAGE_IGNORE_INSERTS | FORMAT_MESSAGE_MAX_WIDTH_MASK,
            nullptr, err, MAKELANGID(LANG_NEUTRAL, SUBLANG_DEFAULT),
            buf, ARRAYSIZE(buf), nullptr))
    {
        return strprintf("%s (%d)", std::wstring_convert<std::codecvt_utf8_utf16<wchar_t>,wchar_t>().to_bytes(buf), err);
    }
    else
    {
        return strprintf("Unknown error (%d)", err);
    }
}
#else
std::string NetworkErrorString(int err)
{
    char buf[256];
    buf[0] = 0;
    /* Too bad there are two incompatible implementations of the
     * thread-safe strerror. */
    const char *s;
#ifdef STRERROR_R_CHAR_P /* GNU variant can return a pointer outside the passed buffer */
    s = strerror_r(err, buf, sizeof(buf));
#else /* POSIX variant always returns message in buffer */
    s = buf;
    if (strerror_r(err, buf, sizeof(buf)))
        buf[0] = 0;
#endif
    return strprintf("%s (%d)", s, err);
}
#endif

bool Sock::Close()
{
    if (m_ssl) {
        SSL_free(m_ssl);
        m_ssl = nullptr;
    }

    if (m_socket == INVALID_SOCKET)
        return false;
#ifdef WIN32
    int ret = closesocket(m_socket);
#else
    int ret = close(m_socket);
#endif
    if (ret) {
        LogPrintf("Socket close failed: %d. Error: %s\n", m_socket, NetworkErrorString(WSAGetLastError()));
    }
    m_socket = INVALID_SOCKET;
    return ret != SOCKET_ERROR;
}

int Sock::GetSockOpt(int level, int opt_name, void* opt_val, socklen_t* opt_len) const
{
    return getsockopt(m_socket, level, opt_name, static_cast<char*>(opt_val), opt_len);
}

int Sock::SetSockOpt(int level, int opt_name, const void* opt_val, socklen_t opt_len) const
{
    return setsockopt(m_socket, level, opt_name, static_cast<const char*>(opt_val), opt_len);
}

bool Sock::SetNonBlocking() const
{
#ifdef WIN32
    u_long on{1};
    if (ioctlsocket(m_socket, FIONBIO, &on) == SOCKET_ERROR) {
        return false;
    }
#else
    const int flags{fcntl(m_socket, F_GETFL, 0)};
    if (flags == SOCKET_ERROR) {
        return false;
    }
    if (fcntl(m_socket, F_SETFL, flags | O_NONBLOCK) == SOCKET_ERROR) {
        return false;
    }
#endif
    return true;
}

bool Sock::IsSelectable() const
{
#if defined(USE_POLL) || defined(WIN32)
    return true;
#else
    return m_socket < FD_SETSIZE;
#endif
}

int Sock::Connect(const sockaddr* addr, socklen_t addr_len) const
{
    return connect(m_socket, addr, addr_len);
}

std::unique_ptr<Sock> Sock::Accept(sockaddr* addr, socklen_t* addr_len) const
{
#ifdef WIN32
    static constexpr auto ERR = INVALID_SOCKET;
#else
    static constexpr auto ERR = SOCKET_ERROR;
#endif

    std::unique_ptr<Sock> sock;

    const SOCKET socket = accept(m_socket, addr, addr_len);
    if (socket != ERR) {
        try {
            sock = std::make_unique<Sock>(socket);
        } catch (const std::exception&) {
#ifdef WIN32
            closesocket(socket);
#else
            close(socket);
#endif
        }
    }

    return sock;
}

int Sock::Bind(const sockaddr* addr, socklen_t addr_len) const
{
    return bind(m_socket, addr, addr_len);
}

int Sock::Listen(int backlog) const
{
    return listen(m_socket, backlog);
}

