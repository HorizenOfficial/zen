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

int Sock::Wait(int64_t timeout, Event requested) const
{
#ifdef USE_POLL
    pollfd fd;
    fd.fd = m_socket;
    fd.events = 0;
    if (requested & RECV) {
        fd.events |= POLLIN;
    }
    if (requested & SEND) {
        fd.events |= POLLOUT;
    }

    return poll(&fd, 1, count_milliseconds(timeout));
#else
    if (!IsSelectable()) {
        return -1;
    }

    fd_set fdset_recv;
    fd_set fdset_send;
    FD_ZERO(&fdset_recv);
    FD_ZERO(&fdset_send);

    if (requested & RECV) {
        FD_SET(m_socket, &fdset_recv);
    }

    if (requested & SEND) {
        FD_SET(m_socket, &fdset_send);
    }

    timeval timeout_struct = MillisToTimeval(timeout);

    return select(m_socket + 1, &fdset_recv, &fdset_send, nullptr, &timeout_struct);
#endif /* USE_POLL */
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
        LogPrintf("CLosing socket: %d. Error: %s\n", m_socket, NetworkErrorString(WSAGetLastError()));
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

