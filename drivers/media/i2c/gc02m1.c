// SPDX-License-Identifier: GPL-2.0
/*
 * GalaxyCore GC02M1 2MP CMOS sensor driver
 *
 * Register sequence extracted from the vendor Chromatix container.
 */

#include <linux/clk.h>
#include <linux/delay.h>
#include <linux/gpio/consumer.h>
#include <linux/i2c.h>
#include <linux/module.h>
#include <linux/pm_runtime.h>
#include <linux/regmap.h>
#include <linux/regulator/consumer.h>
#include <media/v4l2-cci.h>
#include <media/v4l2-ctrls.h>
#include <media/v4l2-device.h>
#include <media/v4l2-fwnode.h>
#include <media/v4l2-subdev.h>

#define GC02M1_NATIVE_WIDTH		1600
#define GC02M1_NATIVE_HEIGHT		1200
#define GC02M1_MCLK_FREQ		19200000
#define GC02M1_LINK_FREQ		336000000ULL
#define GC02M1_PIXEL_RATE		67200000UL
#define GC02M1_FPS			30

#define GC02M1_ID			0x02e0
#define GC02M1_REG_CHIP_ID		CCI_REG16(0x00f0)
#define GC02M1_REG_PAGE			CCI_REG8(0xfe)
#define GC02M1_REG_STREAM		CCI_REG8(0x3e)
#define GC02M1_STREAM_ON		0x90
#define GC02M1_STREAM_OFF		0x00
#define GC02M1_REG_EXPOSURE		CCI_REG16(0x03)
#define GC02M1_REG_GAIN			CCI_REG8(0x24)
#define GC02M1_EXPOSURE_DEFAULT		0x047d
#define GC02M1_EXPOSURE_MAX		0x0fff
#define GC02M1_GAIN_DEFAULT		0x00
#define GC02M1_GAIN_MAX			0xff

struct gc02m1_device {
	struct v4l2_subdev sd;
	struct media_pad pad;
	struct regmap *regmap;
	struct clk *clk;
	struct gpio_desc *reset;
	struct regulator_bulk_data supplies[2];
	struct v4l2_ctrl_handler ctrl_handler;
	struct {
		struct v4l2_ctrl *exposure;
		struct v4l2_ctrl *gain;
		struct v4l2_ctrl *link_freq;
		struct v4l2_ctrl *pixel_rate;
	} ctrls;
};

static const s64 link_freq_menu_items[] = {
	GC02M1_LINK_FREQ,
};

static const struct reg_sequence gc02m1_init_regs[] = {
	{ 0xfc, 0x01 },
	{ 0xf4, 0x41 },
	{ 0xf5, 0xc0 },
	{ 0xf6, 0x44 },
	{ 0xf8, 0x46 },
	{ 0xf9, 0x82 },
	{ 0xfa, 0x00 },
	{ 0xfd, 0x80 },
	{ 0xfc, 0x81 },
	{ 0xfe, 0x03 },
	{ 0x01, 0x0b },
	{ 0xf7, 0x01 },
	{ 0xfc, 0x80 },
	{ 0xfc, 0x80 },
	{ 0xfc, 0x80 },
	{ 0xfc, 0x8e },
	{ 0xfe, 0x00 },
	{ 0x87, 0x09 },
	{ 0xee, 0x72 },
	{ 0xfe, 0x01 },
	{ 0x8c, 0x90 },
	{ 0xfe, 0x00 },
	{ 0x90, 0x00 },
	{ 0x03, 0x04 },
	{ 0x04, 0x7d },
	{ 0x41, 0x04 },
	{ 0x42, 0xf4 },
	{ 0x05, 0x04 },
	{ 0x06, 0x48 },
	{ 0x07, 0x00 },
	{ 0x08, 0x18 },
	{ 0x9d, 0x18 },
	{ 0x09, 0x00 },
	{ 0x0a, 0x02 },
	{ 0x0d, 0x04 },
	{ 0x0e, 0xbc },
	{ 0x17, 0x83 },
	{ 0x19, 0x04 },
	{ 0x24, 0x00 },
	{ 0x56, 0x20 },
	{ 0x5b, 0x00 },
	{ 0x5e, 0x01 },
	{ 0x21, 0x3c },
	{ 0x44, 0x20 },
	{ 0xcc, 0x01 },
	{ 0x1a, 0x04 },
	{ 0x1f, 0x11 },
	{ 0x27, 0x30 },
	{ 0x2b, 0x00 },
	{ 0x33, 0x00 },
	{ 0x53, 0x90 },
	{ 0xe6, 0x50 },
	{ 0x39, 0x07 },
	{ 0x43, 0x04 },
	{ 0x46, 0x2a },
	{ 0x7c, 0xa0 },
	{ 0xd0, 0xbe },
	{ 0xd1, 0x60 },
	{ 0xd2, 0x40 },
	{ 0xd3, 0xf3 },
	{ 0xde, 0x1d },
	{ 0xcd, 0x05 },
	{ 0xce, 0x6f },
	{ 0xfc, 0x88 },
	{ 0xfe, 0x10 },
	{ 0xfe, 0x00 },
	{ 0xfc, 0x8e },
	{ 0xfe, 0x00 },
	{ 0xfe, 0x00 },
	{ 0xfe, 0x00 },
	{ 0xfe, 0x00 },
	{ 0xfc, 0x88 },
	{ 0xfe, 0x10 },
	{ 0xfe, 0x00 },
	{ 0xfc, 0x8e },
	{ 0xfe, 0x04 },
	{ 0xe0, 0x01 },
	{ 0xfe, 0x00 },
	{ 0xfe, 0x01 },
	{ 0x53, 0x44 },
	{ 0x87, 0x51 },
	{ 0x89, 0x03 },
	{ 0xfe, 0x00 },
	{ 0xb0, 0x74 },
	{ 0xb1, 0x04 },
	{ 0xb2, 0x00 },
	{ 0xb6, 0x00 },
	{ 0xfe, 0x04 },
	{ 0xd8, 0x00 },
	{ 0xc0, 0x40 },
	{ 0xc0, 0x00 },
	{ 0xc0, 0x00 },
	{ 0xc0, 0x00 },
	{ 0xc0, 0x60 },
	{ 0xc0, 0x00 },
	{ 0xc0, 0xc0 },
	{ 0xc0, 0x2a },
	{ 0xc0, 0x80 },
	{ 0xc0, 0x00 },
	{ 0xc0, 0x00 },
	{ 0xc0, 0x40 },
	{ 0xc0, 0xa0 },
	{ 0xc0, 0x00 },
	{ 0xc0, 0x90 },
	{ 0xc0, 0x19 },
	{ 0xc0, 0xc0 },
	{ 0xc0, 0x00 },
	{ 0xc0, 0xd0 },
	{ 0xc0, 0x2f },
	{ 0xc0, 0xe0 },
	{ 0xc0, 0x00 },
	{ 0xc0, 0x90 },
	{ 0xc0, 0x39 },
	{ 0xc0, 0x00 },
	{ 0xc0, 0x01 },
	{ 0xc0, 0x20 },
	{ 0xc0, 0x04 },
	{ 0xc0, 0x20 },
	{ 0xc0, 0x01 },
	{ 0xc0, 0xe0 },
	{ 0xc0, 0x0f },
	{ 0xc0, 0x40 },
	{ 0xc0, 0x01 },
	{ 0xc0, 0xe0 },
	{ 0xc0, 0x1a },
	{ 0xc0, 0x60 },
	{ 0xc0, 0x01 },
	{ 0xc0, 0x20 },
	{ 0xc0, 0x25 },
	{ 0xc0, 0x80 },
	{ 0xc0, 0x01 },
	{ 0xc0, 0xa0 },
	{ 0xc0, 0x2c },
	{ 0xc0, 0xa0 },
	{ 0xc0, 0x01 },
	{ 0xc0, 0xe0 },
	{ 0xc0, 0x32 },
	{ 0xc0, 0xc0 },
	{ 0xc0, 0x01 },
	{ 0xc0, 0x20 },
	{ 0xc0, 0x38 },
	{ 0xc0, 0xe0 },
	{ 0xc0, 0x01 },
	{ 0xc0, 0x60 },
	{ 0xc0, 0x3c },
	{ 0xc0, 0x00 },
	{ 0xc0, 0x02 },
	{ 0xc0, 0xa0 },
	{ 0xc0, 0x40 },
	{ 0xc0, 0x80 },
	{ 0xc0, 0x02 },
	{ 0xc0, 0x18 },
	{ 0xc0, 0x5c },
	{ 0xfe, 0x00 },
	{ 0x9f, 0x10 },
	{ 0xfe, 0x00 },
	{ 0x26, 0x20 },
	{ 0xfe, 0x01 },
	{ 0x40, 0x22 },
	{ 0x46, 0x7f },
	{ 0x49, 0x0f },
	{ 0x4a, 0xf0 },
	{ 0xfe, 0x04 },
	{ 0x14, 0x80 },
	{ 0x15, 0x80 },
	{ 0x16, 0x80 },
	{ 0x17, 0x80 },
	{ 0xfe, 0x01 },
	{ 0x41, 0x20 },
	{ 0x4c, 0x00 },
	{ 0x4d, 0x0c },
	{ 0x44, 0x08 },
	{ 0x48, 0x03 },
	{ 0xfe, 0x01 },
	{ 0x90, 0x01 },
	{ 0x91, 0x00 },
	{ 0x92, 0x06 },
	{ 0x93, 0x00 },
	{ 0x94, 0x06 },
	{ 0x95, 0x04 },
	{ 0x96, 0xb0 },
	{ 0x97, 0x06 },
	{ 0x98, 0x40 },
	{ 0xfe, 0x03 },
	{ 0x01, 0x23 },
	{ 0x03, 0xce },
	{ 0x04, 0x48 },
	{ 0x15, 0x00 },
	{ 0x21, 0x10 },
	{ 0x22, 0x05 },
	{ 0x23, 0x20 },
	{ 0x25, 0x20 },
	{ 0x26, 0x08 },
	{ 0x29, 0x06 },
	{ 0x2a, 0x0a },
	{ 0x2b, 0x08 },
	{ 0xfe, 0x01 },
	{ 0x8c, 0x10 },
	{ 0xfe, 0x00 },
	{ 0x3e, 0x00 },
};

static const struct reg_sequence gc02m1_stream_on_regs[] = {
	{ 0xfe, 0x00 },
	{ 0x3e, GC02M1_STREAM_ON },
};

static const struct reg_sequence gc02m1_stream_off_regs[] = {
	{ 0x3e, GC02M1_STREAM_OFF },
};

static inline struct gc02m1_device *to_gc02m1_sensor(struct v4l2_subdev *sd)
{
	return container_of(sd, struct gc02m1_device, sd);
}

static int gc02m1_power_on(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc02m1_device *sensor = to_gc02m1_sensor(sd);
	int ret;

	ret = regulator_bulk_enable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
	if (ret)
		return ret;

	ret = clk_prepare_enable(sensor->clk);
	if (ret)
		goto err_regulators;

	/* RESETB is active low and asserted by probe (GPIO_ACTIVE_LOW). */
	fsleep(10 * USEC_PER_MSEC);
	gpiod_set_value_cansleep(sensor->reset, 0);
	fsleep(10 * USEC_PER_MSEC);

	return 0;

err_regulators:
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
	return ret;
}

static void gc02m1_power_off(struct device *dev)
{
	struct v4l2_subdev *sd = dev_get_drvdata(dev);
	struct gc02m1_device *sensor = to_gc02m1_sensor(sd);

	gpiod_set_value_cansleep(sensor->reset, 1);
	clk_disable_unprepare(sensor->clk);
	regulator_bulk_disable(ARRAY_SIZE(sensor->supplies), sensor->supplies);
}

static int gc02m1_enable_streams(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *state,
				 u32 pad, u64 streams_mask)
{
	struct gc02m1_device *sensor = to_gc02m1_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret)
		return ret;

	ret = regmap_multi_reg_write(sensor->regmap, gc02m1_init_regs,
				     ARRAY_SIZE(gc02m1_init_regs));
	if (ret)
		goto err_pm;

	ret = __v4l2_ctrl_handler_setup(&sensor->ctrl_handler);
	if (ret)
		goto err_pm;

	ret = regmap_multi_reg_write(sensor->regmap, gc02m1_stream_on_regs,
				     ARRAY_SIZE(gc02m1_stream_on_regs));
	if (ret)
		goto err_pm;

	return 0;

err_pm:
	pm_runtime_put(&client->dev);
	return ret;
}

static int gc02m1_disable_streams(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *state,
				  u32 pad, u64 streams_mask)
{
	struct gc02m1_device *sensor = to_gc02m1_sensor(sd);
	struct i2c_client *client = v4l2_get_subdevdata(sd);
	int ret;

	ret = regmap_multi_reg_write(sensor->regmap, gc02m1_stream_off_regs,
				     ARRAY_SIZE(gc02m1_stream_off_regs));
	pm_runtime_put(&client->dev);

	return ret;
}

static int gc02m1_s_ctrl(struct v4l2_ctrl *ctrl)
{
	struct gc02m1_device *sensor =
		container_of(ctrl->handler, struct gc02m1_device, ctrl_handler);
	struct i2c_client *client = v4l2_get_subdevdata(&sensor->sd);
	int ret;

	ret = pm_runtime_resume_and_get(&client->dev);
	if (ret)
		return ret;

	switch (ctrl->id) {
	case V4L2_CID_EXPOSURE:
		cci_write(sensor->regmap, GC02M1_REG_EXPOSURE, ctrl->val, &ret);
		break;
	case V4L2_CID_ANALOGUE_GAIN:
		cci_write(sensor->regmap, GC02M1_REG_GAIN, ctrl->val, &ret);
		break;
	default:
		ret = -EINVAL;
		break;
	}

	pm_runtime_put(&client->dev);

	return ret;
}

static const struct v4l2_ctrl_ops gc02m1_ctrl_ops = {
	.s_ctrl = gc02m1_s_ctrl,
};

static int gc02m1_enum_mbus_code(struct v4l2_subdev *sd,
				 struct v4l2_subdev_state *sd_state,
				 struct v4l2_subdev_mbus_code_enum *code)
{
	if (code->index)
		return -EINVAL;

	code->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	return 0;
}

static int gc02m1_enum_frame_size(struct v4l2_subdev *sd,
				  struct v4l2_subdev_state *sd_state,
				  struct v4l2_subdev_frame_size_enum *fse)
{
	if (fse->index)
		return -EINVAL;

	fse->min_width = GC02M1_NATIVE_WIDTH;
	fse->max_width = GC02M1_NATIVE_WIDTH;
	fse->min_height = GC02M1_NATIVE_HEIGHT;
	fse->max_height = GC02M1_NATIVE_HEIGHT;

	return 0;
}

static int gc02m1_init_state(struct v4l2_subdev *sd,
			     struct v4l2_subdev_state *sd_state)
{
	struct v4l2_mbus_framefmt *fmt =
		v4l2_subdev_state_get_format(sd_state, 0);

	fmt->width = GC02M1_NATIVE_WIDTH;
	fmt->height = GC02M1_NATIVE_HEIGHT;
	fmt->code = MEDIA_BUS_FMT_SRGGB10_1X10;
	fmt->field = V4L2_FIELD_NONE;

	return 0;
}

static const struct v4l2_subdev_video_ops gc02m1_video_ops = {
	.s_stream = v4l2_subdev_s_stream_helper,
};

static const struct v4l2_subdev_pad_ops gc02m1_pad_ops = {
	.enum_mbus_code = gc02m1_enum_mbus_code,
	.enum_frame_size = gc02m1_enum_frame_size,
	.get_fmt = v4l2_subdev_get_fmt,
	.set_fmt = v4l2_subdev_get_fmt,
	.enable_streams = gc02m1_enable_streams,
	.disable_streams = gc02m1_disable_streams,
};

static const struct v4l2_subdev_ops gc02m1_ops = {
	.video = &gc02m1_video_ops,
	.pad = &gc02m1_pad_ops,
};

static const struct v4l2_subdev_internal_ops gc02m1_internal_ops = {
	.init_state = gc02m1_init_state,
};

static int gc02m1_init_controls(struct gc02m1_device *sensor)
{
	struct v4l2_ctrl_handler *hdl = &sensor->ctrl_handler;

	v4l2_ctrl_handler_init(hdl, 4);
	sensor->sd.ctrl_handler = hdl;

	sensor->ctrls.exposure =
		v4l2_ctrl_new_std(hdl, &gc02m1_ctrl_ops, V4L2_CID_EXPOSURE,
				  0, GC02M1_EXPOSURE_MAX, 1,
				  GC02M1_EXPOSURE_DEFAULT);
	sensor->ctrls.gain =
		v4l2_ctrl_new_std(hdl, &gc02m1_ctrl_ops, V4L2_CID_ANALOGUE_GAIN,
				  0, GC02M1_GAIN_MAX, 1, GC02M1_GAIN_DEFAULT);
	sensor->ctrls.link_freq =
		v4l2_ctrl_new_int_menu(hdl, NULL, V4L2_CID_LINK_FREQ, 0, 0,
				       link_freq_menu_items);
	sensor->ctrls.pixel_rate =
		v4l2_ctrl_new_std(hdl, NULL, V4L2_CID_PIXEL_RATE, 0,
				  GC02M1_PIXEL_RATE, 1, GC02M1_PIXEL_RATE);

	if (hdl->error)
		return hdl->error;

	sensor->ctrls.link_freq->flags |= V4L2_CTRL_FLAG_READ_ONLY;
	sensor->ctrls.pixel_rate->flags |= V4L2_CTRL_FLAG_READ_ONLY;

	return 0;
}

static int gc02m1_check_hwcfg(struct device *dev)
{
	struct v4l2_fwnode_endpoint bus_cfg = {
		.bus_type = V4L2_MBUS_CSI2_DPHY,
	};
	struct fwnode_handle *ep;
	unsigned long link_freq_bitmap;
	int ret;

	ep = fwnode_graph_get_endpoint_by_id(dev_fwnode(dev), 0, 0, 0);
	if (!ep)
		return dev_err_probe(dev, -EPROBE_DEFER,
				     "waiting for fwnode graph endpoint\n");

	ret = v4l2_fwnode_endpoint_alloc_parse(ep, &bus_cfg);
	fwnode_handle_put(ep);
	if (ret)
		return dev_err_probe(dev, ret, "parsing endpoint failed\n");

	ret = v4l2_link_freq_to_bitmap(dev, bus_cfg.link_frequencies,
				       bus_cfg.nr_of_link_frequencies,
				       link_freq_menu_items,
				       ARRAY_SIZE(link_freq_menu_items),
				       &link_freq_bitmap);

	if (ret == 0 && bus_cfg.bus.mipi_csi2.num_data_lanes != 1)
		ret = dev_err_probe(dev, -EINVAL,
				    "number of CSI2 data lanes %u is not supported\n",
				    bus_cfg.bus.mipi_csi2.num_data_lanes);

	v4l2_fwnode_endpoint_free(&bus_cfg);
	return ret;
}

static int gc02m1_probe(struct i2c_client *client)
{
	struct gc02m1_device *sensor;
	unsigned long freq;
	u64 id = 0;
	int ret;

	ret = gc02m1_check_hwcfg(&client->dev);
	if (ret)
		return ret;

	sensor = devm_kzalloc(&client->dev, sizeof(*sensor), GFP_KERNEL);
	if (!sensor)
		return -ENOMEM;

	sensor->clk = devm_v4l2_sensor_clk_get(&client->dev, NULL);
	if (IS_ERR(sensor->clk))
		return dev_err_probe(&client->dev, PTR_ERR(sensor->clk),
				     "failed to get clock\n");

	freq = clk_get_rate(sensor->clk);
	if (freq != GC02M1_MCLK_FREQ)
		return dev_err_probe(&client->dev, -EINVAL,
				     "external clock %lu is not supported\n", freq);

	sensor->reset = devm_gpiod_get(&client->dev, "reset", GPIOD_OUT_HIGH);
	if (IS_ERR(sensor->reset))
		return dev_err_probe(&client->dev, PTR_ERR(sensor->reset),
				     "getting reset GPIO\n");

	sensor->supplies[0].supply = "vana";
	sensor->supplies[1].supply = "vio";
	ret = devm_regulator_bulk_get(&client->dev, ARRAY_SIZE(sensor->supplies),
				      sensor->supplies);
	if (ret)
		return dev_err_probe(&client->dev, ret,
				     "getting regulators failed\n");

	v4l2_i2c_subdev_init(&sensor->sd, client, &gc02m1_ops);
	sensor->sd.internal_ops = &gc02m1_internal_ops;
	sensor->sd.flags |= V4L2_SUBDEV_FL_HAS_DEVNODE;

	sensor->regmap = devm_cci_regmap_init_i2c(client, 8);
	if (IS_ERR(sensor->regmap))
		return PTR_ERR(sensor->regmap);

	pm_runtime_set_active(&client->dev);
	pm_runtime_get_noresume(&client->dev);
	pm_runtime_enable(&client->dev);

	ret = gc02m1_power_on(&client->dev);
	if (ret)
		goto err_pm;

	cci_read(sensor->regmap, GC02M1_REG_CHIP_ID, &id, &ret);
	if (ret)
		goto err_power_off;

	if ((id & 0xffff) != GC02M1_ID) {
		dev_err(&client->dev, "sensor ID error, read 0x%llx, expected 0x%x\n",
			id, GC02M1_ID);
		ret = -ENODEV;
		goto err_power_off;
	}

	dev_info(&client->dev, "detected GC02M1 (id 0x%llx)\n", id);

	ret = gc02m1_init_controls(sensor);
	if (ret)
		goto err_power_off;

	sensor->sd.entity.function = MEDIA_ENT_F_CAM_SENSOR;
	sensor->pad.flags = MEDIA_PAD_FL_SOURCE;
	ret = media_entity_pads_init(&sensor->sd.entity, 1, &sensor->pad);
	if (ret)
		goto err_free_ctrls;

	ret = v4l2_subdev_init_finalize(&sensor->sd);
	if (ret)
		goto err_entity;

	ret = v4l2_async_register_subdev_sensor(&sensor->sd);
	if (ret)
		goto err_subdev;

	pm_runtime_set_suspended(&client->dev);
	pm_runtime_put(&client->dev);

	return 0;

err_subdev:
	v4l2_subdev_cleanup(&sensor->sd);
err_entity:
	media_entity_cleanup(&sensor->sd.entity);
err_free_ctrls:
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
err_power_off:
	gc02m1_power_off(&client->dev);
err_pm:
	pm_runtime_disable(&client->dev);
	pm_runtime_put_noidle(&client->dev);
	return ret;
}

static void gc02m1_remove(struct i2c_client *client)
{
	struct v4l2_subdev *sd = i2c_get_clientdata(client);
	struct gc02m1_device *sensor = to_gc02m1_sensor(sd);

	v4l2_async_unregister_subdev(sd);
	v4l2_subdev_cleanup(sd);
	media_entity_cleanup(&sensor->sd.entity);
	v4l2_ctrl_handler_free(&sensor->ctrl_handler);
	pm_runtime_disable(&client->dev);
	if (!pm_runtime_status_suspended(&client->dev)) {
		gc02m1_power_off(&client->dev);
		pm_runtime_set_suspended(&client->dev);
	}
}

static int gc02m1_runtime_resume(struct device *dev)
{
	return gc02m1_power_on(dev);
}

static int gc02m1_runtime_suspend(struct device *dev)
{
	gc02m1_power_off(dev);
	return 0;
}

static const struct dev_pm_ops gc02m1_pm_ops = {
	RUNTIME_PM_OPS(gc02m1_runtime_suspend, gc02m1_runtime_resume, NULL)
};

static const struct of_device_id gc02m1_of_match[] = {
	{ .compatible = "galaxycore,gc02m1" },
	{ }
};
MODULE_DEVICE_TABLE(of, gc02m1_of_match);

static struct i2c_driver gc02m1_driver = {
	.driver = {
		.name = "gc02m1",
		.of_match_table = gc02m1_of_match,
		.pm = pm_ptr(&gc02m1_pm_ops),
	},
	.probe = gc02m1_probe,
	.remove = gc02m1_remove,
};
module_i2c_driver(gc02m1_driver);

MODULE_DESCRIPTION("GalaxyCore GC02M1 sensor driver");
MODULE_LICENSE("GPL");
