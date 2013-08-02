/*
 * Synaptics DSX touchscreen driver
 *
 * Copyright (C) 2012 Synaptics Incorporated
 *
 * Copyright (C) 2012 Alexandra Chin <alexandra.chin@tw.synaptics.com>
 * Copyright (C) 2012 Scott Lin <scott.lin@tw.synaptics.com>
 *
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/i2c.h>
#include <linux/input.h>
#include <linux/types.h>
#include <linux/delay.h>
#include <linux/slab.h>
#include <linux/platform_device.h>
#include "synaptics_dsx_core.h"

#define SYN_I2C_RETRY_TIMES	5

static int synaptics_rmi4_i2c_set_page(struct synaptics_rmi4_data *rmi4_data,
		unsigned int addr)
{
	int retval = 0;
	unsigned char retry;
	unsigned char buf[PAGE_SELECT_LEN];
	unsigned char page;
	struct i2c_client *client = to_i2c_client(rmi4_data->pdev->dev.parent);

	page = ((addr >> 8) & MASK_8BIT);
	if (page != rmi4_data->current_page) {
		buf[0] = MASK_8BIT;
		buf[1] = page;
		for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
			retval = i2c_master_send(client, buf, PAGE_SELECT_LEN);
			if (retval != PAGE_SELECT_LEN) {
				dev_err(&rmi4_data->pdev->dev,
						"%s: I2C retry %d\n",
						__func__, retry + 1);
				msleep(20);
			} else {
				rmi4_data->current_page = page;
				break;
			}
		}
	} else {
		retval = PAGE_SELECT_LEN;
	}

	return retval;
}

 /**
 * synaptics_rmi4_i2c_read()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function reads data of an arbitrary length from the sensor,
 * starting from an assigned register address of the sensor, via I2C
 * with a retry mechanism.
 */

static int synaptics_rmi4_i2c_read(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char buf;
	struct i2c_client *client = to_i2c_client(rmi4_data->pdev->dev.parent);
	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = 1,
			.buf = &buf,
		},
		{
			.addr = client->addr,
			.flags = I2C_M_RD,
			.len = length,
			.buf = data,
		},
	};

	buf = addr & MASK_8BIT;

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));
	retval = synaptics_rmi4_i2c_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN)
		goto exit;

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(client->adapter, msg, 2) == 2) {
			retval = length;
			break;
		}
		dev_err(&rmi4_data->pdev->dev,
				"%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		dev_err(&rmi4_data->pdev->dev,
				"%s: I2C read over retry limit\n",
				__func__);
		retval = -EIO;
	}

#ifdef DEBUG
	dev_dbg(&rmi4_data->pdev->dev,
		"%s: dump spi read data: addr = 0x%02x length =%d\n",
		__func__, addr, length);

	int j;
	for (j = 0; j < length ; j++)
		dev_dbg(&rmi4_data->pdev->dev, "data[%d]=0x%04x\n", j, data[j]);
#endif

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

 /**
 * synaptics_rmi4_i2c_write()
 *
 * Called by various functions in this driver, and also exported to
 * other expansion Function modules such as rmi_dev.
 *
 * This function writes data of an arbitrary length to the sensor,
 * starting from an assigned register address of the sensor, via I2C with
 * a retry mechanism.
 */
static int synaptics_rmi4_i2c_write(struct synaptics_rmi4_data *rmi4_data,
		unsigned short addr, unsigned char *data, unsigned short length)
{
	int retval;
	unsigned char retry;
	unsigned char buf[length + 1];
	struct i2c_client *client = to_i2c_client(rmi4_data->pdev->dev.parent);

	struct i2c_msg msg[] = {
		{
			.addr = client->addr,
			.flags = 0,
			.len = length + 1,
			.buf = buf,
		}
	};

#ifdef DEBUG
	dev_dbg(&rmi4_data->pdev->dev,
			"%s: dump spi write data: addr = 0x%04x length =%d\n",
			__func__, addr, length);
	int j;
	for (j = 0; j < length ; j++)
		dev_dbg(&rmi4_data->pdev->dev, "data[%d]=0x%02x\n", j, data[j]);
#endif

	mutex_lock(&(rmi4_data->rmi4_io_ctrl_mutex));

	retval = synaptics_rmi4_i2c_set_page(rmi4_data, addr);
	if (retval != PAGE_SELECT_LEN)
		goto exit;

	buf[0] = addr & MASK_8BIT;
	memcpy(&buf[1], &data[0], length);

	for (retry = 0; retry < SYN_I2C_RETRY_TIMES; retry++) {
		if (i2c_transfer(client->adapter, msg, 1) == 1) {
			retval = length;
			break;
		}
		dev_err(&rmi4_data->pdev->dev,
				"%s: I2C retry %d\n",
				__func__, retry + 1);
		msleep(20);
	}

	if (retry == SYN_I2C_RETRY_TIMES) {
		dev_err(&rmi4_data->pdev->dev,
				"%s: I2C write over retry limit\n",
				__func__);
		retval = -EIO;
	}

exit:
	mutex_unlock(&(rmi4_data->rmi4_io_ctrl_mutex));

	return retval;
}

static struct synaptics_dsx_bus_data rmi4_i2c_bus_data = {
	.type		= BUS_I2C,
	.read		= synaptics_rmi4_i2c_read,
	.write		= synaptics_rmi4_i2c_write,
};

static int synaptics_rmi4_i2c_probe(struct i2c_client *client,
		const struct i2c_device_id *dev_id)
{
	int retval;
	struct platform_device *pdev;
	struct synaptics_dsx_hw_info hw_info = {
		.board = client->dev.platform_data,
		.bus = &rmi4_i2c_bus_data,
	};

	if (!i2c_check_functionality(client->adapter,
			I2C_FUNC_SMBUS_BYTE_DATA)) {
		dev_err(&client->dev,
			"%s: SMBus byte data not supported\n", __func__);
		return -EIO;
	}

	pdev = platform_device_alloc(DRIVER_NAME, -1);
	if (!pdev) {
		dev_err(&client->dev,
			"%s: Failed to allocate platform device\n", __func__);
		return -ENOMEM;
	}

	pdev->dev.parent = &client->dev;

	retval = platform_device_add_data(pdev, &hw_info, sizeof(hw_info));
	if (retval) {
		dev_err(&client->dev,
			 "%s: Failed to add platform data\n" , __func__);
		goto err_device_put;
	}

	retval = platform_device_add(pdev);
	if (retval) {
		dev_err(&client->dev,
			"%s: Failed to add platform device\n" , __func__);
		goto err_device_put;
	}

	if (!pdev->dev.driver) {
		retval = -ENODEV;
		goto err_device_put;
	}

	dev_info(&client->dev, "%s: success\n", __func__);
	return 0;

err_device_put:
	platform_device_put(pdev);
	return retval;

}

static int synaptics_rmi4_i2c_remove(struct i2c_client *client)
{
	struct synaptics_rmi4_data *rmi4_data = dev_get_drvdata(&client->dev);

	platform_device_unregister(rmi4_data->pdev);

	return 0;
}


static const struct i2c_device_id synaptics_rmi4_id_table[] = {
	{"synaptics_dsx_i2c", 0},
	{},
};

MODULE_DEVICE_TABLE(i2c, synaptics_rmi4_id_table);

static struct i2c_driver synaptics_rmi4_i2c_driver = {
	.driver = {
		.name = "synaptics_dsx_i2c",
		.owner = THIS_MODULE,
	},
	.probe = synaptics_rmi4_i2c_probe,
	.remove = __devexit_p(synaptics_rmi4_i2c_remove),
	.id_table = synaptics_rmi4_id_table,
};


int synaptics_rmi4_bus_init(void)
{
	return i2c_add_driver(&synaptics_rmi4_i2c_driver);
}
EXPORT_SYMBOL(synaptics_rmi4_bus_init);

void synaptics_rmi4_bus_exit(void)
{
	return i2c_del_driver(&synaptics_rmi4_i2c_driver);
}
EXPORT_SYMBOL(synaptics_rmi4_bus_exit);

MODULE_AUTHOR("Synaptics, Inc.");
MODULE_DESCRIPTION("Synaptics DSX I2C Touch Driver");
MODULE_LICENSE("GPL v2");