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

#include <avs_commons_posix_config.h>

#include <time.h>

#include <avsystem/commons/time.h>

VISIBILITY_SOURCE_BEGIN

avs_time_real_t avs_time_real_now(void) {
    struct timeval system_value;
    avs_time_real_t result;
    gettimeofday(&system_value, NULL);
    result.since_real_epoch.seconds = system_value.tv_sec;
    result.since_real_epoch.nanoseconds = (int32_t) system_value.tv_usec*1000;
    return result;
}

avs_time_monotonic_t avs_time_monotonic_now(void) {
    struct timeval system_value;
    avs_time_monotonic_t result;
    gettimeofday(&system_value, NULL);
    result.since_monotonic_epoch.seconds = system_value.tv_sec;
    result.since_monotonic_epoch.nanoseconds = (int32_t) system_value.tv_usec*1000;
    return result;
}
