namespace zen {
typedef enum
{
    SSL_ACCEPT,
    SSL_CONNECT,
    SSL_SHUTDOWN
} SSLConnectionRoutine;
typedef enum
{
    CLIENT_CONTEXT,
    SERVER_CONTEXT
} TLSContextType;
}  // namespace zen
