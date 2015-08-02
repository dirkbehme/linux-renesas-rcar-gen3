/*
 * vsp1_drm.c  --  R-Car VSP1 DRM API
 *
 * Copyright (C) 2015 Renesas Electronics Corporation
 *
 * Contact: Laurent Pinchart (laurent.pinchart@ideasonboard.com)
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 */

#include <linux/device.h>
#include <linux/slab.h>
#include <linux/vsp1.h>

#include <media/media-entity.h>
#include <media/v4l2-subdev.h>

#include "vsp1.h"
#include "vsp1_bru.h"
#include "vsp1_drm.h"
#include "vsp1_lif.h"
#include "vsp1_pipe.h"
#include "vsp1_rwpf.h"

/* -----------------------------------------------------------------------------
 * DU Driver API
 */

int vsp1_du_init(struct device *dev)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);

	if (!vsp1)
		return -ENXIO;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_init);

static int vsp1_du_enable(struct vsp1_device *vsp1)
{
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe;
	struct media_entity *output = &pipe->output->entity.subdev.entity;
	struct vsp1_entity *entity;
	unsigned long flags;
	int ret;

	dev_dbg(vsp1->dev, "%s\n", __func__);

	ret = media_entity_pipeline_start(output, &pipe->pipe);
	if (ret < 0)
		return ret;

	ret = vsp1_device_get(vsp1);
	if (ret < 0)
		goto error;
		return ret;

	list_for_each_entry(entity, &pipe->entities, list_pipe) {
		vsp1_entity_route_setup(entity);

		ret = v4l2_subdev_call(&entity->subdev, video,
				       s_stream, 1);
		if (ret < 0) {
			mutex_unlock(&pipe->lock);
			goto error;
		}
	}

	spin_lock_irqsave(&pipe->irqlock, flags);
	if (vsp1_pipeline_ready(pipe))
		vsp1_pipeline_run(pipe);
	spin_unlock_irqrestore(&pipe->irqlock, flags);

	dev_dbg(vsp1->dev, "%s: done\n", __func__);
	return 0;

error:
	media_entity_pipeline_stop(output);
	return ret;
}

static void vsp1_du_disable(struct vsp1_device *vsp1)
{
	struct vsp1_pipeline *pipe = &vsp1->drm->pipe;
	struct media_entity *output = &pipe->output->entity.subdev.entity;
	int ret;

	ret = vsp1_pipeline_stop(pipe);
	if (ret == -ETIMEDOUT)
		dev_err(vsp1->dev, "pipeline stop timeout\n");

	media_entity_pipeline_stop(output);

	vsp1_device_put(vsp1);

	dev_dbg(vsp1->dev, "%s\n", __func__);
}

int vsp1_du_setup_lif(struct device *dev, unsigned int width,
		      unsigned int height)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	struct vsp1_bru *bru = vsp1->bru;
	struct v4l2_subdev_format format;
	unsigned int i;
	int ret;

	dev_dbg(vsp1->dev, "%s: configuring LIF with format %ux%u\n",
		__func__, width, height);

	if (width == 0 || height == 0) {
		vsp1_du_disable(vsp1);
		return 0;
	}

	/* Configure the format at the RPFs and propagate it through the
	 * pipeline.
	 */
	memset(&format, 0, sizeof(format));
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;

	for (i = 0; i < bru->entity.source_pad; ++i) {
		struct vsp1_rwpf *rpf = bru->inputs[i].rpf;

		if (!rpf)
			continue;

		format.pad = RWPF_PAD_SINK;

		format.format.width = width;
		format.format.height = height;
		format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
		format.format.field = V4L2_FIELD_NONE;

		ret = v4l2_subdev_call(&rpf->entity.subdev, pad,
				       set_fmt, NULL, &format);
		if (ret < 0)
			return ret;

		dev_dbg(vsp1->dev,
			"%s: set format %ux%u (%x) on RPF%u sink\n",
			__func__, format.format.width, format.format.height,
			format.format.code, rpf->entity.index);

		format.pad = RWPF_PAD_SOURCE;

		ret = v4l2_subdev_call(&rpf->entity.subdev, pad,
				       get_fmt, NULL, &format);
		if (ret < 0)
			return ret;

		dev_dbg(vsp1->dev,
			"%s: got format %ux%u (%x) on RPF%u source\n",
			__func__, format.format.width, format.format.height,
			format.format.code, rpf->entity.index);

		format.pad = i;

		ret = v4l2_subdev_call(&bru->entity.subdev, pad,
				       set_fmt, NULL, &format);
		if (ret < 0)
			return ret;

		dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on BRU pad %u\n",
			__func__, format.format.width, format.format.height,
			format.format.code, i);
	}

	format.pad = bru->entity.source_pad;
	format.format.width = width;
	format.format.height = height;
	format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
	format.format.field = V4L2_FIELD_NONE;

	ret = v4l2_subdev_call(&bru->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on BRU pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, i);

	format.pad = RWPF_PAD_SINK;
	ret = v4l2_subdev_call(&vsp1->wpf[0]->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on WPF0 sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code);

	format.pad = RWPF_PAD_SOURCE;
	ret = v4l2_subdev_call(&vsp1->wpf[0]->entity.subdev, pad, get_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: got format %ux%u (%x) on WPF0 source\n",
		__func__, format.format.width, format.format.height,
		format.format.code);

	format.pad = LIF_PAD_SINK;
	ret = v4l2_subdev_call(&vsp1->lif->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on LIF sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code);

	/* Verify that the format at the output of the pipeline matches the
	 * requested frame size and media bus code.
	 */
	if (format.format.width != width || format.format.height != height ||
	    format.format.code != MEDIA_BUS_FMT_ARGB8888_1X32)
		return -EPIPE;

	ret = vsp1_du_enable(vsp1);
	if (ret < 0)
		return ret;

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_setup_lif);

int vsp1_du_setup_rpf(struct device *dev, unsigned int rpf_index,
		      u32 pixelformat, unsigned int pitch,
		      dma_addr_t mem[2], const struct v4l2_rect *src,
		      const struct v4l2_rect *dst)
{
	struct vsp1_device *vsp1 = dev_get_drvdata(dev);
	const struct vsp1_format_info *fmtinfo;
	struct v4l2_subdev_selection sel;
	struct v4l2_subdev_format format;
	struct vsp1_rwpf_memory memory;
	struct vsp1_rwpf *rpf;
	int ret;

	dev_dbg(vsp1->dev,
		"%s: RPF%u: (%u,%u)/%ux%u -> (%u,%u)/%ux%u (%08x), pitch %u dma[0] %pad dma[1] %pad\n",
		__func__, rpf_index,
		src->left, src->top, src->width, src->height,
		dst->left, dst->top, dst->width, dst->height,
		pixelformat, pitch, &mem[0], &mem[1]);

	if (rpf_index >= vsp1->pdata.rpf_count)
		return -EINVAL;

	rpf = vsp1->rpf[rpf_index];

	if (pixelformat == 0) {
		/* TODO: Disable the RPF and the BRU input. */
		return 0;
	}

	/* Set the stride at the RPF input. */
	fmtinfo = vsp1_get_format_info(pixelformat);
	if (!fmtinfo) {
		dev_dbg(vsp1->dev, "Unsupport pixel format %08x for RPF\n",
			pixelformat);
		return -EINVAL;
	}

	rpf->fmtinfo = fmtinfo;
	rpf->format.num_planes = fmtinfo->planes;
	rpf->format.plane_fmt[0].bytesperline = pitch;
	rpf->format.plane_fmt[1].bytesperline = pitch;

	/* Configure the format on the RPF sink pad and propagate it up to the
	 * BRU sink pad.
	 */
	memset(&format, 0, sizeof(format));
	format.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	format.pad = RWPF_PAD_SINK;
	format.format.width = src->width + src->left;
	format.format.height = src->height + src->top;
	format.format.code = MEDIA_BUS_FMT_ARGB8888_1X32;
	format.format.field = V4L2_FIELD_NONE;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set format %ux%u (%x) on RPF%u sink\n",
		__func__, format.format.width, format.format.height,
		format.format.code, rpf->entity.index);

	memset(&sel, 0, sizeof(sel));
	sel.which = V4L2_SUBDEV_FORMAT_ACTIVE;
	sel.pad = RWPF_PAD_SINK;
	sel.target = V4L2_SEL_TGT_CROP;
	sel.r = *src;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, set_selection, NULL,
			       &sel);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set selection (%u,%u)/%ux%u on RPF%u sink\n",
		__func__, sel.r.left, sel.r.top, sel.r.width, sel.r.height,
		rpf->entity.index);

	format.pad = RWPF_PAD_SOURCE;

	ret = v4l2_subdev_call(&rpf->entity.subdev, pad, get_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: got format %ux%u (%x) on RPF%u source\n",
		__func__, format.format.width, format.format.height,
		format.format.code, rpf->entity.index);

	/* TODO: Don't hardcode RPF->BRU input index. */
	format.pad = rpf->entity.index;

	ret = v4l2_subdev_call(&vsp1->bru->entity.subdev, pad, set_fmt, NULL,
			       &format);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev, "%s: set format %ux%u (%x) on BRU pad %u\n",
		__func__, format.format.width, format.format.height,
		format.format.code, format.pad);

	sel.pad = rpf->entity.index;
	sel.target = V4L2_SEL_TGT_COMPOSE;
	sel.r = *dst;

	ret = v4l2_subdev_call(&vsp1->bru->entity.subdev, pad, set_selection,
			       NULL, &sel);
	if (ret < 0)
		return ret;

	dev_dbg(vsp1->dev,
		"%s: set selection (%u,%u)/%ux%u on BRU pad %u\n",
		__func__, sel.r.left, sel.r.top, sel.r.width, sel.r.height,
		sel.pad);

	/* Set the memory buffer address. */
	memory.num_planes = fmtinfo->planes;
	memory.addr[0] = mem[0];
	memory.addr[1] = mem[1];

	rpf->ops->set_memory(rpf, &memory);

	return 0;
}
EXPORT_SYMBOL_GPL(vsp1_du_setup_rpf);

/* -----------------------------------------------------------------------------
 * Frame Handling
 */

static void vsp1_drm_frame_end(struct vsp1_pipeline *pipe,
			       struct vsp1_rwpf *rpf)
{
	/* TODO */
}

/* -----------------------------------------------------------------------------
 * Initialization
 */

int vsp1_drm_create_links(struct vsp1_device *vsp1)
{
	const u32 flags = MEDIA_LNK_FL_ENABLED | MEDIA_LNK_FL_IMMUTABLE;
	unsigned int i;
	int ret;

	for (i = 0; i < vsp1->pdata.rpf_count; ++i) {
		struct vsp1_rwpf *rpf = vsp1->rpf[i];

		ret = media_entity_create_link(&rpf->entity.subdev.entity,
					       RWPF_PAD_SOURCE,
					       &vsp1->bru->entity.subdev.entity,
					       i, flags);
		if (ret < 0)
			return ret;

		rpf->entity.sink = &vsp1->bru->entity.subdev.entity;
		rpf->entity.sink_pad = i;
	}

	ret = media_entity_create_link(&vsp1->bru->entity.subdev.entity,
				       vsp1->bru->entity.source_pad,
				       &vsp1->wpf[0]->entity.subdev.entity,
				       RWPF_PAD_SINK, flags);
	if (ret < 0)
		return ret;

	vsp1->bru->entity.sink = &vsp1->wpf[0]->entity.subdev.entity;
	vsp1->bru->entity.sink_pad = RWPF_PAD_SINK;

	ret = media_entity_create_link(&vsp1->wpf[0]->entity.subdev.entity,
				       RWPF_PAD_SOURCE,
				       &vsp1->lif->entity.subdev.entity,
				       LIF_PAD_SINK, flags);
	if (ret < 0)
		return ret;

	return 0;
}

int vsp1_drm_init(struct vsp1_device *vsp1)
{
	struct vsp1_pipeline *pipe;
	unsigned int i;

	vsp1->drm = devm_kzalloc(vsp1->dev, sizeof(*vsp1->drm), GFP_KERNEL);
	if (!vsp1->drm)
		return -ENOMEM;

	pipe = &vsp1->drm->pipe;

	vsp1_pipeline_init(pipe);
	pipe->frame_end = vsp1_drm_frame_end;

	/* The DRM pipeline is static, add entities manually. */
	pipe->num_inputs = vsp1->pdata.rpf_count;

	for (i = 0; i < pipe->num_inputs; ++i) {
		struct vsp1_rwpf *input = vsp1->rpf[i];
		struct v4l2_rect *rect = &vsp1->bru->inputs[i].compose;

		list_add_tail(&input->entity.list_pipe, &pipe->entities);
		pipe->inputs[i] = input;

		input->location.left = rect->left;
		input->location.top = rect->top;

		vsp1->bru->inputs[i].rpf = input;
	}

	list_add_tail(&vsp1->bru->entity.list_pipe, &pipe->entities);
	list_add_tail(&vsp1->wpf[0]->entity.list_pipe, &pipe->entities);
	list_add_tail(&vsp1->lif->entity.list_pipe, &pipe->entities);

	pipe->bru = &vsp1->bru->entity;
	pipe->lif = &vsp1->lif->entity;
	pipe->output = vsp1->wpf[0];

	return 0;
}
