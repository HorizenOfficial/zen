#pragma once

#include <memory>
#include <unordered_map>

#include "compat.h"

typedef struct ssl_st SSL;

class Sock
{
public:
    /**
     * Default constructor, creates an empty object that does nothing when destroyed.
     */
    Sock();

    /**
     * Take ownership of an existent socket.
     */
    explicit Sock(SOCKET s, SSL* ssl = nullptr);

    /**
     * Copy constructor, disabled because closing the same socket twice is undesirable.
     */
    Sock(const Sock&) = delete;

    /**
     * Move constructor, grab the socket from another object and close ours (if set).
     */
    Sock(Sock&& other);

    /**
     * Destructor, close the socket or do nothing if empty.
     */
    virtual ~Sock();

    /**
     * Copy assignment operator, disabled because closing the same socket twice is undesirable.
     */
    Sock& operator=(const Sock&) = delete;

    /**
     * Move assignment operator, grab the socket from another object and close ours (if set).
     */
    virtual Sock& operator=(Sock&& other);

    /**
     * Get the value of the contained socket.
     * @return socket or INVALID_SOCKET if empty
     */
    virtual SOCKET Get() const;

    virtual SSL* GetSSL() const;
    virtual bool SetSSL(SSL* ssl);

    /**
     * Get the value of the contained socket and drop ownership. It will not be closed by the
     * destructor after this call.
     * @return socket or INVALID_SOCKET if empty
     */
    //virtual SOCKET Release();

    /**
     * Close if non-empty.
     */
    virtual bool Reset();

    /**
     * send(2) wrapper. Equivalent to `send(this->Get(), data, len, flags);`. Code that uses this
     * wrapper can be unit-tested if this method is overridden by a mock Sock implementation.
     */
    virtual ssize_t Send(const void* data, size_t len, int flags) const;

    /**
     * recv(2) wrapper. Equivalent to `recv(this->Get(), buf, len, flags);`. Code that uses this
     * wrapper can be unit-tested if this method is overridden by a mock Sock implementation.
     */
    virtual ssize_t Recv(void* buf, size_t len, int flags) const;

    using Event = uint8_t;

    /**
     * If passed to `Wait()`, then it will wait for readiness to read from the socket.
     */
    static constexpr Event RECV = 0b001;

    /**
     * If passed to `Wait()`, then it will wait for readiness to send to the socket.
     */
    static constexpr Event SEND = 0b010;

    /**
     * Ignored if passed to `Wait()`, but could be set in the occurred events if an
     * exceptional condition has occurred on the socket or if it has been disconnected.
     */
    static constexpr Event ERR = 0b100;

    struct Events {
        explicit Events() : requested{0} {}
        explicit Events(Event req) : requested{req} {}
        Event requested;
        Event occurred{0};
    };

    /**
     * Wait for readiness for input (recv) or output (send).
     * @param[in] timeout Wait this much for at least one of the requested events to occur.
     * @param[in] requested Wait for those events, bitwise-or of `RECV` and `SEND`.
     * @return true on success and false otherwise
     */
    virtual int Wait(int64_t timeout, Event requested) const;
    static int WaitMany(int64_t timeout, std::unordered_map<SOCKET, Events>& events_per_sock);
    int GetSockOpt(int level, int opt_name, void* opt_val, socklen_t* opt_len) const;
    int SetSockOpt(int level, int opt_name, const void* opt_val, socklen_t opt_len) const;
    bool SetNonBlocking() const;
    bool IsSelectable() const;
    int Connect(const sockaddr* addr, socklen_t addr_len) const;
    std::unique_ptr<Sock> Accept(sockaddr* addr, socklen_t* addr_len) const;
    int Bind(const sockaddr* addr, socklen_t addr_len) const;
    int Listen(int backlog) const;

private:
    /**
     * Contained socket. `INVALID_SOCKET` designates the object is empty.
     */
    SOCKET m_socket;
    SSL*   m_ssl;

    /** Close socket and set hSocket to INVALID_SOCKET */
    bool Close();
};

/** Return readable error string for a network error code */
std::string NetworkErrorString(int err);

struct timeval MillisToTimeval(int64_t nTimeout);
