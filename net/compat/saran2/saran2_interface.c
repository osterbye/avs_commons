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

#include <avsystem/commons/saran2.h>

#include <fcntl.h>
#include <linux/input.h>
#include <stdio.h>
#include <string.h>
#include <termios.h>
#include <unistd.h>

#include "saran2_interface.h"


VISIBILITY_SOURCE_BEGIN

static int _saran2_ready(void) {
    if (sara_fd < 0)
        return -1;

    if (saran2_ip[0] == '\0') {
        uint32_t startTime = _saran2_time_ms();
        while ((_saran2_time_ms()-startTime) < IP_TIMEOUT) {
            _saran2_fetch(); // Clear buffer
            write(sara_fd, "AT+CGPADDR=0\r\n", 14);
            if (RET_OK == _saran2_fetch()) {
                uint8_t index = _saran2_get_index(1, 10, '\"');
                if (index > 0) {
                    ++index;
                    uint8_t index2 = _saran2_get_index(1, index, '\"');
                    if (index2 > 0) {
                        uint8_t len = index2 - index;
                        if (len >= NET_MAX_HOST_IP_SIZE)
                            len = NET_MAX_HOST_IP_SIZE - 1;

                        strncpy(saran2_ip, &m_buffer[index], len);
                        saran2_ip[len] = '\0';
                        LOG(TRACE, "Our IP adress is %s", saran2_ip);
                        return 0;
                    }
                }
            }
            sleep(2);
        }
        return -1;
    }
    return 0;
}

static int _saran2_create_socket(avs_net_socket_t *net_socket, int32_t port) {
    if (port == 5683 || port > 65535) {
        LOG(ERROR, "Invalid port number: %u", port);
        return -1;
    }

    char cmd[30];
    if (port >= 0) {
        snprintf(cmd, 30, "AT+NSOCR=\"DGRAM\",17,%u,0\r\n", port);
        snprintf(net_socket->local_port, NET_PORT_SIZE, "%u", port);
    } else {
        snprintf(cmd, 30, "AT+NSOCR=\"DGRAM\",17,,0\r\n");
        net_socket->local_port[0] = '\0';
    }
    _saran2_fetch(); // Clear buffer
    write(sara_fd, cmd, strlen(cmd));
    if (RET_OK == _saran2_fetch()) {
        uint8_t index = _saran2_get_index(2, 0, '\r');
        if (index >= 1) {
            net_socket->socket = atoi(&m_buffer[index-1]);
            LOG(TRACE, "Socket %u created", net_socket->socket);
            return 0;
        }
    }
    net_socket->socket = INVALID_SOCKET;
    net_socket->local_port[0] = '\0';
    LOG(ERROR, "Failed to create socket\n");
    return -1;
}

static int _saran2_setup_uart(void) {
    saran2_ip[0] = '\0';
    int retries = 6;
    while (--retries > 0) {
        sara_fd = open(SARA_N2_DEVICE_NAME, O_RDWR | O_NDELAY);
        if (sara_fd >= 0) break;
    }
    if (sara_fd < 0) {
        LOG(ERROR, "Could not open %s", SARA_N2_DEVICE_NAME);
        return -1;
    } else {
        struct termios tio;
        /* Get the attributes of the UART */
        if (tcgetattr(sara_fd, &tio) < 0) {
            LOG(ERROR, "Could not get attributes for UART connected to SARA-N2");
            return -1;
        } else {
            cfmakeraw(&tio);
            cfsetispeed(&tio, B9600);
            cfsetospeed(&tio, B9600);
            if (tcsetattr(sara_fd, TCSANOW, &tio) < 0) {
                LOG(ERROR, "Could not set attributes for UART connected to SARA-N2");
                return -1;
            }
        }
    }
    sleep(1);
    write(sara_fd, "\r\nAT\r\n", 6);
    if (fetch()) {
        LOG(ERROR, "SARA-N2 device not ready");
        return -1;
    }
    m_bufferIndex = 0;
    return 0;
}

static int _saran2_fetch(void) {
    int ret = RET_INCOMPLETE;
    m_bufferIndex = 0;
    uint32_t startTime = _saran2_time_ms();
    while (ret < RET_OK && (_saran2_time_ms()-startTime) < READ_TIMEOUT_MS_FETCH) {
        m_bufferIndex += read(sara_fd, &m_buffer[m_bufferIndex], SARA_BUFFER_SIZE-m_bufferIndex);
        ret = _saran2_message_complete();
    }
    return ret;
}

static uint8_t _saran2_get_index(uint8_t field, uint8_t skip, const char delimiter) {
    uint8_t fieldCounter = 0;
    for (uint8_t i = skip; i < m_bufferIndex; ++i) {
        if (delimiter == m_buffer[i]) {
            if (++fieldCounter == field) {
                return i;
            }
        }
    }
    return 0;
}

static uint32_t _saran2_time_ms(void) {
    uint32_t result;
    struct timeval system_value;
    gettimeofday(&system_value, NULL);
    result = static_cast<uint32_t>((system_value.tv_sec*1000) + (system_value.tv_usec/1000));
    return result;
}

static int _saran2_message_complete(void) {
    char rebootMsg[] = "REBOOT_CAUSE";
    size_t rebLen = strlen(rebootMsg);
    size_t j = 0;
    if (m_bufferIndex >= rebLen) {
        for (int i = 0; i < m_bufferIndex; ++i) {
            if (m_buffer[i] == rebootMsg[j]) {
                if (++j >= rebLen) {
                    return RET_REBOOT;
                }
            } else {
                j = 0;
            }
        }
    }

    for (int i = 0; i < m_bufferIndex; ++i) {
        if (0x0D == m_buffer[i]) {
            if ((i+5) < m_bufferIndex
                    && 0x0A == m_buffer[i+1]
                    && 0x4F == m_buffer[i+2]    //O
                    && 0x4B == m_buffer[i+3]    //K
                    && 0x0D == m_buffer[i+4]
                    && 0x0A == m_buffer[i+5]
                    ) {
                LOG(TRACE, "RET_OK\n");
                return RET_OK;
            } else if ((i+8) < m_bufferIndex
                       && 0x0A == m_buffer[i+1]
                       && 0x45 == m_buffer[i+2]    //E
                       && 0x52 == m_buffer[i+3]    //R
                       && 0x52 == m_buffer[i+4]    //R
                       && 0x4F == m_buffer[i+5]    //O
                       && 0x52 == m_buffer[i+6]    //R
                       && 0x0D == m_buffer[i+7]
                       && 0x0A == m_buffer[i+8]
                       ) {
                LOG(TRACE, "RET_ERROR\n");
                return RET_ERROR;
            }
        } else if ((i+13) < m_bufferIndex
                   && '+' == m_buffer[i]
                   && 'N' == m_buffer[i+1]
                   && 'S' == m_buffer[i+2]
                   && 'O' == m_buffer[i+3]
                   && 'N' == m_buffer[i+4]
                   && 'M' == m_buffer[i+5]
                   && 'I' == m_buffer[i+6]
                   && ':' == m_buffer[i+7]
                   && ' ' == m_buffer[i+8]
                   ) {
            for (int k = i+9; k < (m_bufferIndex-1); ++k) {
                if (0x0D == m_buffer[k] && 0x0A == m_buffer[k+1]) {
                    LOG(TRACE, "RET_NSONMI\n");
                    return RET_NSONMI;
                }
            }
        }
    }
    LOG(TRACE, "RET_INCOMPLETE\n");
    return RET_INCOMPLETE;
}

int init_saran2(void) {
    return _saran2_setup_uart();
}
