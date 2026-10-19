/* SPDX-License-Identifier: GPL-2.0-only */
/*
 * Copyright (c) 2022-2024 Qualcomm Innovation Center, Inc. All rights reserved.
 */

#ifndef __IRIS_VIDC_H__
#define __IRIS_VIDC_H__

#include <linux/videodev2.h>

struct iris_core;

/*
 * VIDIOC_IRIS_SURFACE_FENCE - attach/signal a DMA-BUF reservation write fence.
 *
 * Userspace (the VA-API front-end) copies decoded frames into a stable
 * DMA-BUF "surface" and exports that surface to a display/compositor stage.
 * To make that hand-off race-free it needs a fence that is unsignalled while
 * the copy is still in flight and signalled once the copy has completed.
 *
 * ATTACH: @dmabuf_fd is the surface, @token a caller-chosen non-zero id. The
 *         driver installs a fresh, unsignalled fence as the write fence of the
 *         dma-buf's reservation object, so anybody waiting on that dma-buf
 *         blocks until the copy is done.
 * SIGNAL: @token selects a previously attached fence. The driver signals it and
 *         the waiters are released. @dmabuf_fd is ignored.
 *
 * Both directions are deliberately best-effort: userspace degrades gracefully
 * when the ioctl is unavailable, therefore this must never fail with a blocking
 * error code.
 *
 * Layout is byte-for-byte identical to the userspace definition
 * (iris-vaapi src/platform/qcom/surface_fence.h) - do not reorder fields.
 */
struct iris_surface_fence_cmd {
	__u32 op;
	__s32 dmabuf_fd;
	__u64 token;
};

#define IRIS_SURFACE_FENCE_ATTACH	1
#define IRIS_SURFACE_FENCE_SIGNAL	2
#define VIDIOC_IRIS_SURFACE_FENCE \
	_IOW('V', BASE_VIDIOC_PRIVATE, struct iris_surface_fence_cmd)

void iris_init_ops(struct iris_core *core);
int iris_open(struct file *filp);
int iris_close(struct file *filp);

#endif
