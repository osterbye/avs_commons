/*
 * Copyright 2017-2018 AVSystem <avsystem@avsystem.com>
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

#ifndef NET_API_H
#define NET_API_H

VISIBILITY_PRIVATE_HEADER_BEGIN

typedef enum {
    AVS_NET_DATA_SOURCE_EMPTY,
    AVS_NET_DATA_SOURCE_FILE,
    AVS_NET_DATA_SOURCE_PATH,
    AVS_NET_DATA_SOURCE_BUFFER
} avs_net_data_source_t;

typedef enum {
    AVS_NET_SECURITY_INFO_TRUSTED_CERT,
    AVS_NET_SECURITY_INFO_CLIENT_CERT,
    AVS_NET_SECURITY_INFO_CLIENT_KEY
} avs_net_security_info_tag_t;

VISIBILITY_PRIVATE_HEADER_END

#endif // NET_API_H
