/*
 * Copyright (c) 2006-2021, RT-Thread Development Team
 *
 * SPDX-License-Identifier: Apache-2.0
 *
 * Change Logs:
 * Date           Author       Notes
 * 2022-08-01     ywx       the first version
 */
#ifndef APPLICATIONS_APP_LOGIC_APP_INIT_H_
#define APPLICATIONS_APP_LOGIC_APP_INIT_H_

#include <rtthread.h>
#include <rtdevice.h>
#include <board.h>
#include "rtconfig.h"

#include "dev_sign_api.h"
#include "mqtt_api.h"
#include "mqtt.h"
#include "logic.h"

int start_rt_thread(void);

#endif /* APPLICATIONS_APP_LOGIC_APP_INIT_H_ */
