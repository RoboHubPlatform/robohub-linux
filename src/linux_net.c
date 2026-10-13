/**
 * @file linux_net.c
 * @brief The RoboHub network contract over POSIX sockets.
 *
 * A Linux host is already on a network, so there is nothing to join: the
 * interface is connected from the start and its address is the machine's, and
 * `wifi_connect` is a formality a deployment with no interface keeps working
 * without. The sockets are BSD sockets, non-blocking, with the contract's
 * timeout spelled as a `select`.
 *
 * The functions are strong, so they are reached instead of robohub-common's weak
 * defaults — the same mechanism any platform uses.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "robohub/net.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <stdint.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/select.h>
#include <unistd.h>

static RoboHubNetStatus linux_status = ROBOHUB_NET_CONNECTED;

RoboHubError robohub_net_wifi_connect(const RoboHubWiFiConfig* config)
{
    (void)config;
    linux_status = ROBOHUB_NET_CONNECTED;
    return ROBOHUB_OK;
}

RoboHubError robohub_net_wifi_disconnect(void)
{
    linux_status = ROBOHUB_NET_DISCONNECTED;
    return ROBOHUB_OK;
}

RoboHubNetStatus robohub_net_status(void)
{
    return linux_status;
}

RoboHubError robohub_net_ip(char* out_ip, rh_size max_len)
{
    if (out_ip == 0 || max_len < 10u) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    /* A host may have several; the loopback address is the one always there and
     * the one a local gateway is reached on. */
    strcpy(out_ip, "127.0.0.1");
    return ROBOHUB_OK;
}

RoboHubSocket robohub_socket_create(RoboHubSocketType type)
{
    int kind = (type == ROBOHUB_SOCKET_TCP) ? SOCK_STREAM : SOCK_DGRAM;
    int fd = socket(AF_INET, kind, 0);
    if (fd < 0) {
        return 0;
    }
    int flags = fcntl(fd, F_GETFL, 0);
    (void)fcntl(fd, F_SETFL, flags | O_NONBLOCK);
    return (RoboHubSocket)(intptr_t)fd;
}

RoboHubError robohub_socket_connect(RoboHubSocket socket, const char* host, rh_u16 port)
{
    if (socket == 0) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    int fd = (int)(intptr_t)socket;
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_port = htons(port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        return ROBOHUB_ERR_INVALID_ARG;
    }
    int result = connect(fd, (struct sockaddr*)&address, sizeof(address));
    if (result < 0 && errno != EINPROGRESS) {
        return ROBOHUB_ERR_IO;
    }
    return ROBOHUB_OK;
}

int robohub_socket_send(RoboHubSocket socket, const void* data, rh_size len)
{
    if (socket == 0) {
        return ROBOHUB_SOCKET_ERROR;
    }
    int fd = (int)(intptr_t)socket;
    ssize_t sent = send(fd, data, len, 0);
    return (sent < 0) ? ROBOHUB_SOCKET_ERROR : (int)sent;
}

int robohub_socket_recv(RoboHubSocket socket, void* data, rh_size len, rh_u32 timeout_ms)
{
    if (socket == 0) {
        return ROBOHUB_SOCKET_ERROR;
    }
    int fd = (int)(intptr_t)socket;
    fd_set readable;
    FD_ZERO(&readable);
    FD_SET(fd, &readable);
    struct timeval tv;
    tv.tv_sec = (long)(timeout_ms / 1000u);
    tv.tv_usec = (long)((timeout_ms % 1000u) * 1000u);
    int ready = select(fd + 1, &readable, 0, 0, &tv);
    if (ready == 0) {
        return ROBOHUB_SOCKET_TIMEOUT;
    }
    if (ready < 0) {
        return ROBOHUB_SOCKET_ERROR;
    }
    ssize_t got = recv(fd, data, len, 0);
    return (got < 0) ? ROBOHUB_SOCKET_ERROR : (int)got;
}

void robohub_socket_close(RoboHubSocket socket)
{
    if (socket != 0) {
        close((int)(intptr_t)socket);
    }
}
