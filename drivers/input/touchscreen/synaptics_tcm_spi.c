// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2017-2020 Synaptics Incorporated.
 * Copyright (C) 2026 Luka Panio <lukapanio@gmail.com>
 */

#include <linux/unaligned.h>
#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/spi/spi.h>

#include "synaptics_tcm.h"

extern const struct dev_pm_ops synaptics_tcm_pm_ops;

static int synaptics_tcm_spi_read(struct synaptics_tcm_bus_ops *bus_ops,
				  void *buf, size_t len)
{
	struct spi_device *spi = bus_ops->dev;
	struct spi_message msg;
	struct spi_transfer xfer;
	unsigned char *tx_buf;
	int ret;

	if (!spi)
		return -ENODEV;

	tx_buf = kzalloc(len, GFP_KERNEL);
	if (!tx_buf)
		return -ENOMEM;
	memset(tx_buf, 0xff, len);

	spi_message_init(&msg);
	memset(&xfer, 0, sizeof(xfer));
	xfer.tx_buf = tx_buf;
	xfer.rx_buf = buf;
	xfer.len = len;
	spi_message_add_tail(&xfer, &msg);

	ret = spi_sync(spi, &msg);
	kfree(tx_buf);
	return ret;
}

static int synaptics_tcm_spi_write(struct synaptics_tcm_bus_ops *bus_ops,
				   void *buf, size_t len)
{
	struct spi_device *spi = bus_ops->dev;
	return spi_write(spi, buf, len);
}

static int synaptics_tcm_spi_probe(struct spi_device *spi)
{
	struct synaptics_tcm_bus_ops *bus_ops;
	int ret;

	spi->mode = SPI_MODE_0;
	spi->bits_per_word = 8;
	ret = spi_setup(spi);
	if (ret)
		return ret;

	bus_ops = devm_kzalloc(&spi->dev, sizeof(*bus_ops), GFP_KERNEL);
	if (!bus_ops)
		return -ENOMEM;

	bus_ops->dev = spi;
	bus_ops->max_xfer_size = spi_max_transfer_size(spi);
	bus_ops->read = synaptics_tcm_spi_read;
	bus_ops->write = synaptics_tcm_spi_write;

	ret = synaptics_tcm_probe(&spi->dev, spi->irq, bus_ops);

	return ret;
}

static const struct spi_device_id synaptics_tcm_spi_ids[] = {
	{ .name = "tcm-spi" },
	{ .name = "s3910" },
	{ },
};
MODULE_DEVICE_TABLE(spi, synaptics_tcm_spi_ids);

static const struct of_device_id synaptics_tcm_spi_of_match[] = {
	{ .compatible = "synaptics,tcm-spi" },
	{ .compatible = "synaptics,tcm-spi-hbp" },
	{ .compatible = "synaptics-tcm" },
	{ .compatible = "synaptics-s3910" },
	{ }
};
MODULE_DEVICE_TABLE(of, synaptics_tcm_spi_of_match);

static struct spi_driver synaptics_tcm_spi_driver = {
	.driver = {
		.name = "synaptics-tcm-spi",
		.of_match_table = synaptics_tcm_spi_of_match,
		.pm = &synaptics_tcm_pm_ops,
	},
	.probe = synaptics_tcm_spi_probe,
	.id_table = synaptics_tcm_spi_ids,
};
module_spi_driver(synaptics_tcm_spi_driver);

MODULE_LICENSE("GPL");
MODULE_DESCRIPTION("Synaptics TCM SPI Touchscreen driver");
MODULE_AUTHOR("Luka Panio <lukapanio@gmail.com>");
