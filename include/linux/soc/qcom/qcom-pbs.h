/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2023 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef _QCOM_PBS_H
#define _QCOM_PBS_H

#include <linux/errno.h>
#include <linux/types.h>

struct device_node;

#if IS_ENABLED(CONFIG_QCOM_PBS)
int qcom_pbs_trigger_event(struct device_node *dev_node, u8 bitmap);
int qcom_pbs_trigger_single_event(struct device_node *dev_node);
#else
static inline int qcom_pbs_trigger_event(struct device_node *dev_node,
					 u8 bitmap)
{
	return -ENODEV;
}

static inline int qcom_pbs_trigger_single_event(
	struct device_node *dev_node)
{
	return -ENODEV;
}

#endif

#endif
