/*
 * test_linux_platform.c — the Linux platform brings up what a host has, and says
 * so about what it does not.
 *
 * The platform is its own test host: it registers a clock and a socket and
 * nothing else, and the checks are the same ones a board's bring-up would make —
 * the clock moves forward, a socket reaches a loopback peer, and a bus a host
 * does not have is absent rather than a failure.
 *
 * @project RoboHub Platform
 * @license MIT
 */

#include "platform.h"
#include "registry.h"
#include "robohub/hal/console.h"
#include "robohub/hal/timer.h"
#include "robohub/net.h"

#include <assert.h>
#include <stdint.h>
#include <string.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <unistd.h>

int main(void)
{
    /* The platform's own console: a program built with this platform reaches it,
     * which is what puts the platform object in the link. A static archive is
     * pulled by what refers to it, so a platform the program never touches is a
     * platform whose board hook the linker may drop. */
    assert(robohub_console_init() == ROBOHUB_CONSOLE_OK);
    assert(robohub_platform_init() == ROBOHUB_OK);

    /* A host registers a clock, and no bus it has no answer for. */
    assert(robohub_count_timer() == 1);
    assert(robohub_count_onewire() == 0);
    assert(robohub_count_i2c() == 0);
    assert(robohub_count_spi() == 0);
    assert(robohub_count_gpio() == 0);

    RoboHubTimer* timer = robohub_get_timer("system_timer");
    assert(timer != 0);
    rh_millis before = timer->get_millis(timer);
    timer->delay_ms(timer, 5u);
    rh_millis after = timer->get_millis(timer);
    assert(after >= before);

    /* The network contract: a host is connected, and a socket reaches a peer. */
    assert(robohub_net_status() == ROBOHUB_NET_CONNECTED);
    char ip[16] = { 0 };
    assert(robohub_net_ip(ip, sizeof(ip)) == ROBOHUB_OK);

    int server = socket(AF_INET, SOCK_STREAM, 0);
    assert(server >= 0);
    struct sockaddr_in address;
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    address.sin_port = 0;
    assert(bind(server, (struct sockaddr*)&address, sizeof(address)) == 0);
    assert(listen(server, 1) == 0);
    socklen_t length = sizeof(address);
    assert(getsockname(server, (struct sockaddr*)&address, &length) == 0);
    rh_u16 port = ntohs(address.sin_port);

    RoboHubSocket client = robohub_socket_create(ROBOHUB_SOCKET_TCP);
    assert(client != 0);
    assert(robohub_socket_connect(client, "127.0.0.1", port) == ROBOHUB_OK);

    int peer = -1;
    for (int i = 0; i < 200 && peer < 0; i++) {
        fd_set readable;
        FD_ZERO(&readable);
        FD_SET(server, &readable);
        struct timeval tv = { 0, 10000 };
        if (select(server + 1, &readable, 0, 0, &tv) > 0) {
            peer = accept(server, 0, 0);
        }
    }
    assert(peer >= 0);

    assert(robohub_socket_send(client, "ping", 4u) == 4);
    char buffer[8] = { 0 };
    assert(recv(peer, buffer, sizeof(buffer), 0) == 4);
    assert(memcmp(buffer, "ping", 4) == 0);

    assert(robohub_socket_recv(client, buffer, sizeof(buffer), 50u) == ROBOHUB_SOCKET_TIMEOUT);

    robohub_socket_close(client);
    close(peer);
    close(server);
    return 0;
}
