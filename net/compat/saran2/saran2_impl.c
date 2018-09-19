/*
 * Copyright 2018 Nikolaj Due Oesterbye <nikolaj@due-oesterbye.dk>
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *     http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

#include <errno.h>

#include "saran2_interface.h"
//#include "../../src/net_impl.h"

VISIBILITY_SOURCE_BEGIN

static int connect_saran2(avs_net_abstract_socket_t *net_socket_,
                          const char* host,
                          const char *port);
static int send_saran2(avs_net_abstract_socket_t *net_socket,
                       const void* buffer,
                       size_t buffer_length);
static int send_to_saran2(avs_net_abstract_socket_t *socket,
                          const void *buffer,
                          size_t buffer_length,
                          const char *host,
                          const char *port);
static int receive_saran2(avs_net_abstract_socket_t *net_socket_,
                          size_t *out,
                          void *buffer,
                          size_t buffer_length);
static int bind_saran2(avs_net_abstract_socket_t *net_socket,
                       const char *localaddr,
                       const char *port);
static int close_saran2(avs_net_abstract_socket_t *net_socket_);
static int cleanup_saran2(avs_net_abstract_socket_t **net_socket);
static int system_socket_saran2(avs_net_abstract_socket_t *net_socket_,
                                const void **out);
static int remote_host_saran2(avs_net_abstract_socket_t *socket_,
                              char *out_buffer, size_t out_buffer_size);
static int remote_hostname_saran2(avs_net_abstract_socket_t *socket_,
                                  char *out_buffer, size_t out_buffer_size);
static int remote_port_saran2(avs_net_abstract_socket_t *socket_,
                              char *out_buffer, size_t out_buffer_size);
static int local_port_saran2(avs_net_abstract_socket_t *socket_,
                             char *out_buffer, size_t out_buffer_size);
static int get_opt_saran2(avs_net_abstract_socket_t *net_socket_,
                          avs_net_socket_opt_key_t option_key,
                          avs_net_socket_opt_value_t *out_option_value);
static int set_opt_saran2(avs_net_abstract_socket_t *net_socket_,
                          avs_net_socket_opt_key_t option_key,
                          avs_net_socket_opt_value_t option_value);
static int errno_saran2(avs_net_abstract_socket_t *net_socket);


static int unimplemented() {
    return -1;
}

static const avs_net_socket_v_table_t net_vtable = {
    connect_saran2,
    (avs_net_socket_decorate_t) unimplemented,
    send_saran2,
    send_to_saran2,
    receive_saran2,
    (avs_net_socket_receive_from_t) unimplemented, // TODO: this should be implemented
    bind_saran2,
    (avs_net_socket_accept_t) unimplemented,
    close_saran2,
    (avs_net_socket_shutdown_t) unimplemented,
    cleanup_saran2,
    system_socket_saran2,
    (avs_net_socket_get_interface_t) unimplemented,
    remote_host_saran2,
    remote_hostname_saran2,
    remote_port_saran2,
    (avs_net_socket_get_local_host_t) unimplemented, // TODO: this is just our local ip, right?
    local_port_saran2,
    get_opt_saran2,
    set_opt_saran2,
    errno_saran2
};

/*typedef struct {
    const avs_net_socket_v_table_t * const operations;
    int socket; // 0-7
    avs_net_socket_state_t state;
    char remote_hostname[NET_MAX_HOSTNAME_SIZE];
    char remote_host_ip[NET_MAX_HOST_IP_SIZE];
    char remote_port[NET_PORT_SIZE];
    char local_port[NET_PORT_SIZE];
    avs_net_socket_configuration_t configuration;
    avs_time_duration_t recv_timeout;
    volatile int error_code;
} avs_net_socket_t;*/



static int connect_saran2(avs_net_abstract_socket_t *net_socket_,
                          const char* host,
                          const char *port) {
    avs_net_socket_t *net_socket = (avs_net_socket_t *) net_socket_;
    avs_net_addrinfo_t *info = NULL;
    int result = 0;
    const int resolve_flags = 0;
    const avs_net_af_t family = AVS_NET_AF_INET4;

    // It is fine to connect to a bound socket, since connecting doesn't modify
    // UDP sockets. We don't want to allow changing the connection parameters
    // to an existing connection socket though.
    if (net_socket->socket != INVALID_SOCKET
            && net_socket->state != AVS_NET_SOCKET_STATE_BOUND) {
        LOG(ERROR, "socket is already connected");
        net_socket->error_code = EISCONN;
        return -1;
    }

    if (_saran2_ready()) {
        LOG(ERROR, "SARA-N2 not ready");
        net_socket->error_code = ENETUNREACH;
        return -1;
    }

    LOG(TRACE, "connecting to [%s]:%s", host, port);

    net_socket->error_code = EADDRNOTAVAIL;
    if ((info = avs_net_addrinfo_resolve_ex(
                net_socket->type, family, host, port, resolve_flags, NULL))) {
        avs_net_resolved_endpoint_t address;
        while (!(result = avs_net_addrinfo_next(info, &address))) {
            if (!avs_simple_snprintf(net_socket->remote_host_ip,
                                    sizeof(net_socket->remote_host_ip),
                                    "%s", address->data.buf) < 0) {
                if (!_saran2_create_socket(net_socket)) {
                    if (avs_simple_snprintf(net_socket->remote_hostname,
                                            sizeof(net_socket->remote_hostname),
                                            "%s", host) < 0) {
                        LOG(WARNING, "Hostname %s is too long, not storing",
                            host);
                        net_socket->remote_hostname[0] = '\0';
                    }
                    if (avs_simple_snprintf(net_socket->remote_port,
                                            sizeof(net_socket->remote_port),
                                            "%s", port) < 0) {
                        LOG(WARNING, "Port %s is too long, not storing", port);
                        net_socket->remote_port[0] = '\0';
                    }
                    avs_net_addrinfo_delete(&info);
                    net_socket->state = AVS_NET_SOCKET_STATE_CONNECTED;
                    return 0;
                } else {
                    LOG(ERROR, "Failed to create SARA-N2 socket");
                }
            } else {
                LOG(WARNING, "Host IP %s is too long, not storing",
                    address->data.buf);
                net_socket->remote_host_ip[0] = '\0';
            }
        }
    }

    avs_net_addrinfo_delete(&info);
    net_socket->remote_host_ip[0] = '\0';
    LOG(ERROR, "cannot establish connection to [%s]:%s: %s",
        host, port, strerror(net_socket->error_code));
    return result < 0 ? result : -1;
}

static int send_saran2(avs_net_abstract_socket_t *net_socket,
                       const void* buffer,
                       size_t buffer_length) {
    // TODO
    return 0;
}

static int send_to_saran2(avs_net_abstract_socket_t *socket,
                          const void *buffer,
                          size_t buffer_length,
                          const char *host,
                          const char *port) {
    // TODO
    return 0;
}

static int receive_saran2(avs_net_abstract_socket_t *net_socket_,
                          size_t *out,
                          void *buffer,
                          size_t buffer_length) {
    // TODO
    return 0;
}

static int bind_saran2(avs_net_abstract_socket_t *net_socket,
                       const char *localaddr,
                       const char *port) {
    // TODO: return -1 if already connected or bound
    return 0;
}

static int close_saran2(avs_net_abstract_socket_t *net_socket_) {
    avs_net_socket_t *net_socket = (avs_net_socket_t *) net_socket_;
    // TODO: +NSOCL
    net_socket->error_code = 0;
    return 0;
}

static int cleanup_saran2(avs_net_abstract_socket_t **net_socket) {
    close_saran2(*net_socket);
    avs_free(*net_socket);
    *net_socket = NULL;
    return 0;
}

static int system_socket_saran2(avs_net_abstract_socket_t *net_socket_,
                                const void **out) {
    avs_net_socket_t *net_socket = (avs_net_socket_t *) net_socket_;
    if (net_socket->socket != INVALID_SOCKET) {
        *out = &net_socket->socket;
        net_socket->error_code = 0;
        return 0;
    } else {
        net_socket->error_code = EBADF;
        return -1;
    }
}

static int remote_host_saran2(avs_net_abstract_socket_t *socket_,
                              char *out_buffer, size_t out_buffer_size) {
    avs_net_socket_t *socket = (avs_net_socket_t *) socket_;
    if (!socket->remote_host_ip[0]) {
        socket->error_code =
                (socket->socket == INVALID_SOCKET ? EBADF : ENOBUFS);
        return -1;
    }
    if (avs_simple_snprintf(out_buffer, out_buffer_size, "%s",
                            socket->remote_host_ip) < 0) {
        socket->error_code = ERANGE;
        return -1;
    } else {
        socket->error_code = 0;
        return 0;
    }
}

static int remote_hostname_saran2(avs_net_abstract_socket_t *socket_,
                                  char *out_buffer, size_t out_buffer_size) {
    avs_net_socket_t *socket = (avs_net_socket_t *) socket_;
    if (!socket->remote_hostname[0]) {
        socket->error_code =
                (socket->socket == INVALID_SOCKET ? EBADF : ENOBUFS);
        return -1;
    }
    if (avs_simple_snprintf(out_buffer, out_buffer_size, "%s",
                            socket->remote_hostname) < 0) {
        socket->error_code = ERANGE;
        return -1;
    } else {
        socket->error_code = 0;
        return 0;
    }
}

static int remote_port_saran2(avs_net_abstract_socket_t *socket_,
                              char *out_buffer, size_t out_buffer_size) {
    avs_net_socket_t *socket = (avs_net_socket_t *) socket_;
    if (!socket->remote_port[0]) {
        socket->error_code =
                (socket->socket == INVALID_SOCKET ? EBADF : ENOBUFS);
        return -1;
    }
    if (avs_simple_snprintf(out_buffer, out_buffer_size, "%s",
                            socket->remote_port) < 0) {
        socket->error_code = ERANGE;
        return -1;
    } else {
        socket->error_code = 0;
        return 0;
    }
}

static int local_port_saran2(avs_net_abstract_socket_t *socket_,
                             char *out_buffer, size_t out_buffer_size) {
    avs_net_socket_t *socket = (avs_net_socket_t *) socket_;
    if (!socket->local_port[0]) {
        socket->error_code =
                (socket->socket == INVALID_SOCKET ? EBADF : ENOBUFS);
        return -1;
    }
    if (avs_simple_snprintf(out_buffer, out_buffer_size, "%s",
                            socket->local_port) < 0) {
        socket->error_code = ERANGE;
        return -1;
    } else {
        socket->error_code = 0;
        return 0;
    }
}

static int get_opt_saran2(avs_net_abstract_socket_t *net_socket_,
                          avs_net_socket_opt_key_t option_key,
                          avs_net_socket_opt_value_t *out_option_value) {
    avs_net_socket_t *net_socket = (avs_net_socket_t *) net_socket_;
    net_socket->error_code = 0;
    switch (option_key) {
    case AVS_NET_SOCKET_OPT_RECV_TIMEOUT:
        out_option_value->recv_timeout = net_socket->recv_timeout;
        return 0;
    case AVS_NET_SOCKET_OPT_STATE:
        out_option_value->state = net_socket->state;
        return 0;
    case AVS_NET_SOCKET_OPT_ADDR_FAMILY:
        // SARA N2 only supports IPv4
        out_option_value->addr_family = AF_INET;
        return 0;
    case AVS_NET_SOCKET_OPT_MTU:
    case AVS_NET_SOCKET_OPT_INNER_MTU:
        // SARA N2 has a max payload size of 512 bytes. Since we don't need any
        // headers from software this is applicable for both MTUs.
        return 512;
    default:
        LOG(DEBUG,
            "get_opt_net: unknown or unsupported option key: "
            "(avs_net_socket_opt_key_t) %d",
            (int) option_key);
        net_socket->error_code = EINVAL;
        return -1;
    }
}

static int set_opt_saran2(avs_net_abstract_socket_t *net_socket_,
                          avs_net_socket_opt_key_t option_key,
                          avs_net_socket_opt_value_t option_value) {
    avs_net_socket_t *net_socket = (avs_net_socket_t *) net_socket_;
    switch (option_key) {
    case AVS_NET_SOCKET_OPT_RECV_TIMEOUT:
        net_socket->recv_timeout = option_value.recv_timeout;
        net_socket->error_code = 0;
        return 0;
    default:
        LOG(DEBUG,
            "set_opt_net: unknown or unsupported option key: "
            "(avs_net_socket_opt_key_t) %d",
            (int) option_key);
        net_socket->error_code = EINVAL;
        return -1;
    }
}

static int errno_saran2(avs_net_abstract_socket_t *net_socket) {
    return ((avs_net_socket_t *) net_socket)->error_code;
}

int _avs_net_create_udp_socket(avs_net_abstract_socket_t **socket,
                               const void *socket_configuration) {
    const avs_net_socket_v_table_t *const VTABLE_PTR = &net_vtable;
    const avs_net_socket_configuration_t *configuration =
            (const avs_net_socket_configuration_t *) socket_configuration;

    if (sara_fd < 0) {
        LOG(ERROR, "SARA-N2 UART not open");
        _saran2_setup_uart();
    }

    avs_net_socket_t *net_socket =
            (avs_net_socket_t *) avs_calloc(1, sizeof (avs_net_socket_t));
    if (!net_socket) {
        return -1;
    }

    memcpy((void *) (intptr_t) &net_socket->operations,
           &VTABLE_PTR, sizeof(VTABLE_PTR));
    net_socket->socket = INVALID_SOCKET;
    net_socket->type = AVS_NET_UDP_SOCKET;
    net_socket->recv_timeout = AVS_NET_SOCKET_DEFAULT_RECV_TIMEOUT;

    /*VALGRIND_HG_DISABLE_CHECKING(&net_socket->socket,
                                 sizeof(net_socket->socket));
    VALGRIND_HG_DISABLE_CHECKING(&net_socket->error_code,
                                 sizeof(net_socket->error_code));*/

    *socket = (avs_net_abstract_socket_t *) net_socket;

    if (configuration) {
        LOG(TRACE, "Additional socket configuration ignored");
    } else {
        LOG(TRACE, "no additional socket configuration");
    }
    return 0;
}

int _avs_net_create_tcp_socket(avs_net_abstract_socket_t **socket,
                               const void *socket_configuration) {
    return -1;
}
