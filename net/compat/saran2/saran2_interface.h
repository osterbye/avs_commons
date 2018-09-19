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

#ifndef SARAN2_INTERFACE_H
#define SARAN2_INTERFACE_H

#include <inttypes.h>

#include "../../src/net_impl.h"


VISIBILITY_PRIVATE_HEADER_BEGIN

#define INVALID_SOCKET          -1
#define NET_MAX_HOST_IP_SIZE    16
#define SARA_BUFFER_SIZE        1064
#define SARA_N2_DEVICE_NAME     "/dev/ttyO5"
#define READ_TIMEOUT_MS_FETCH   250
#define READ_TIMEOUT_MS_NORMAL  100
#define IP_TIMEOUT              30000
int dummy; // TODO: remove.

static enum RET_VALUE {
    RET_INCOMPLETE = -1,
    RET_OK = 0,
    RET_ERROR = 1,
    RET_NSONMI = 2,
    RET_REBOOT = 3
};

typedef struct {
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
} avs_net_socket_t;


static int sara_fd = -1;
static char m_buffer[SARA_BUFFER_SIZE];
static unsigned int m_bufferIndex;
static char saran2_ip[NET_MAX_HOST_IP_SIZE];

static int _saran2_ready(void);
static int _saran2_create_socket(avs_net_socket_t *net_socket,
                                 int32_t port = -1);
static int _saran2_setup_uart(void);
static int _saran2_get_local_ip(void);
static int _saran2_fetch(void);
static uint8_t _saran2_get_index(uint8_t field, uint8_t skip,
                                 const char delimiter);
static int _saran2_message_complete(void);
static uint32_t _saran2_time_ms(void);


VISIBILITY_PRIVATE_HEADER_END

#endif /* SARAN2_INTERFACE_H */
