/*
 * Written by Oron Peled <oron@actcom.co.il>
 * Copyright (C) 2004-2006, Xorcom
 *
 * All rights reserved.
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
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 675 Mass Ave, Cambridge, MA 02139, USA.
 *
 */
#include <linux/version.h>

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,0)
#  warning "This module is tested only with 2.6 kernels"
#endif

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/errno.h>
#include <linux/proc_fs.h>
#ifdef	PROTOCOL_DEBUG
#include <linux/ctype.h>
#endif
#include <linux/workqueue.h>
#include <linux/device.h>
#include <linux/delay.h>	/* for msleep() to debug */
#include "xpd.h"
#include "xpp_zap.h"
#include "xbus-core.h"
#ifdef	XPP_DEBUGFS
#include "xpp_log.h"
#endif
#include "zap_debug.h"

static const char rcsid[] = "$Id: xbus-sysfs.c 4266 2008-05-13 21:08:09Z tzafrir $";

/* Command line parameters */
extern int debug;


/* Kernel versions... */
/*
 * Hotplug replaced with uevent in 2.6.16
 */
#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,16)
#define	OLD_HOPLUG_SUPPORT	// for older kernels
#endif

#if LINUX_VERSION_CODE >= KERNEL_VERSION(2,6,14)
#define	DEVICE_ATTR_READER(name,dev,buf)	\
		ssize_t name(struct device *dev, struct device_attribute *attr, char *buf)
#define	DEVICE_ATTR_WRITER(name,dev,buf, count)	\
		ssize_t name(struct device *dev, struct device_attribute *attr, const char *buf, size_t count)
#else
#define	DEVICE_ATTR_READER(name,dev,buf)	\
		ssize_t name(struct device *dev, char *buf)
#define	DEVICE_ATTR_WRITER(name,dev,buf, count)	\
		ssize_t name(struct device *dev, const char *buf, size_t count)
#endif

/*--------- Sysfs Bus handling ----*/
static int xpp_bus_match(struct device *dev, struct device_driver *driver)
{
	DBG(GENERAL, "dev->bus_id = %s, driver->name = %s\n", dev->bus_id, driver->name);
	return 1;
}

#ifdef OLD_HOPLUG_SUPPORT
static int xpp_bus_hotplug(struct device *dev, char **envp, int envnum, char *buff, int bufsize)
{
	xbus_t	*xbus;

	if(!dev)
		return -ENODEV;
	xbus = dev_to_xbus(dev);
	envp[0] = buff;
	if(snprintf(buff, bufsize, "XBUS_NAME=%s", xbus->busname) >= bufsize)
		return -ENOMEM;
	envp[1] = NULL;
	return 0;
}
#else

#define	XBUS_VAR_BLOCK	\
	do {		\
		XBUS_ADD_UEVENT_VAR("XPP_INIT_DIR=%s", initdir);	\
		XBUS_ADD_UEVENT_VAR("XBUS_NUM=%02d", xbus->num);	\
		XBUS_ADD_UEVENT_VAR("XBUS_NAME=%s", xbus->busname);	\
	} while(0)

#if LINUX_VERSION_CODE < KERNEL_VERSION(2,6,24)
#define XBUS_ADD_UEVENT_VAR(fmt, val...)			\
	do {							\
		int err = add_uevent_var(envp, num_envp, &i,	\
				buffer, buffer_size, &len,	\
				fmt, val);			\
		if (err)					\
			return err;				\
	} while (0)

static int xpp_bus_uevent(struct device *dev, char **envp, int num_envp, char *buffer, int buffer_size)
{
	xbus_t		*xbus;
	int		i = 0;
	int		len = 0;
	extern char	*initdir;

	if(!dev)
		return -ENODEV;
	xbus = dev_to_xbus(dev);
	DBG(GENERAL, "bus_id=%s xbus=%s\n", dev->bus_id, xbus->busname);
	XBUS_VAR_BLOCK;
	envp[i] = NULL;
	return 0;
}
#else
#define XBUS_ADD_UEVENT_VAR(fmt, val...)			\
	do {							\
		int err = add_uevent_var(kenv, fmt, val);	\
		if (err)					\
			return err;				\
	} while (0)

static int xpp_bus_uevent(struct device *dev, struct kobj_uevent_env *kenv)
{
	xbus_t		*xbus;
	extern char	*initdir;

	if(!dev)
		return -ENODEV;
	xbus = dev_to_xbus(dev);
	DBG(GENERAL, "bus_id=%s xbus=%s\n", dev->bus_id, xbus->busname);
	XBUS_VAR_BLOCK;
	return 0;
}
#endif

#endif	/* OLD_HOPLUG_SUPPORT */

static void xpp_bus_release(struct device *dev)
{
	DBG(GENERAL, "\n");
}

static void xpp_dev_release(struct device *dev)
{
	xbus_t	*xbus;

	BUG_ON(!dev);
	xbus = dev_to_xbus(dev);
	XBUS_DBG(GENERAL, xbus, "\n");
}

static struct bus_type xpp_bus_type = {
	.name           = "astribanks",
	.match          = xpp_bus_match,
#ifdef OLD_HOPLUG_SUPPORT
	.hotplug 	= xpp_bus_hotplug,
#else
	.uevent         = xpp_bus_uevent,
#endif
};

static struct device xpp_bus = {
	.bus_id		= "xppbus",
	.release	= xpp_bus_release
};

static struct device_driver xpp_driver = {
	.name		= "xppdrv",
	.bus		= &xpp_bus_type,
#ifndef OLD_HOPLUG_SUPPORT
	.owner		= THIS_MODULE
#endif
};

int register_xpp_bus(void)
{
	int	ret;

	if((ret = bus_register(&xpp_bus_type)) < 0) {
		ERR("%s: bus_register failed. Error number %d", __FUNCTION__, ret);
		goto failed_bus;
	}
	if((ret = device_register(&xpp_bus)) < 0) {
		ERR("%s: registration of xpp_bus failed. Error number %d",
			__FUNCTION__, ret);
		goto failed_busdevice;
	}
	if((ret = driver_register(&xpp_driver)) < 0) {
		ERR("%s: driver_register failed. Error number %d", __FUNCTION__, ret);
		goto failed_driver;
	}
	return 0;
failed_driver:
	device_unregister(&xpp_bus);
failed_busdevice:
	bus_unregister(&xpp_bus_type);
failed_bus:
	return ret;
}

void unregister_xpp_bus(void)
{
	driver_unregister(&xpp_driver);
	device_unregister(&xpp_bus);
	bus_unregister(&xpp_bus_type);
}

/*--------- Sysfs Device handling ----*/
static DEVICE_ATTR_READER(connector_show, dev, buf)
{
	xbus_t	*xbus;
	int	ret;

	xbus = dev_to_xbus(dev);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", xbus->location);
	return ret;
}

static DEVICE_ATTR_READER(label_show, dev, buf)
{
	xbus_t	*xbus;
	int	ret;

	xbus = dev_to_xbus(dev);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", xbus->label);
	return ret;
}

static DEVICE_ATTR_READER(status_show, dev, buf)
{
	xbus_t	*xbus;
	int	ret;

	xbus = dev_to_xbus(dev);
	ret = snprintf(buf, PAGE_SIZE, "%s\n", (TRANSPORT_RUNNING(xbus))?"connected":"missing");
	return ret;
}

static DEVICE_ATTR_READER(timing_show, dev, buf)
{
	xbus_t			*xbus;
	struct xpp_drift	*driftinfo;
	int			len = 0;
	struct timeval		now;

	do_gettimeofday(&now);
	xbus = dev_to_xbus(dev);
	driftinfo = &xbus->drift;
	len += snprintf(buf + len, PAGE_SIZE - len, "DRIFT: %-3s", sync_mode_name(xbus->sync_mode));
	if(xbus->sync_mode == SYNC_MODE_PLL) {
		len += snprintf(buf + len, PAGE_SIZE - len,
				" %5d: jitter %4d median %4d calc_drift %3d lost (%4d,%4d) : ",
					xbus->ticker.cycle,
					driftinfo->jitter, driftinfo->median,
					driftinfo->calc_drift,
					driftinfo->lost_ticks, driftinfo->lost_tick_count);
		len += snprintf(buf + len, PAGE_SIZE - len,
				"DRIFT %3d %ld sec ago",
				xbus->sync_adjustment,
				(xbus->pll_updated_at == 0) ? 0 : now.tv_sec - xbus->pll_updated_at);
	}
	len += snprintf(buf + len, PAGE_SIZE - len, "\n");
	return len;
}

#ifdef	SAMPLE_TICKS
/*
 * tick sampling: Measure offset from reference ticker:
 *   - Recording start when writing to:
 *       /sys/bus/astribanks/devices/xbus-??/samples
 *   - Recording ends when filling SAMPLE_SIZE ticks
 *   - Results are read from the same sysfs file.
 *   - Trying to read/write during recording, returns -EBUSY.
 */
static DEVICE_ATTR_READER(samples_show, dev, buf)
{
	xbus_t			*xbus;
	int			len = 0;
	int			i;

	xbus = dev_to_xbus(dev);
	if(xbus->sample_running)
		return -EBUSY;
	for(i = 0; i < SAMPLE_SIZE; i++) {
		if(len > PAGE_SIZE - 20)
			break;
		len += snprintf(buf + len, PAGE_SIZE - len, "%d\n", xbus->sample_ticks[i]);
	}
	return len;
}

static DEVICE_ATTR_WRITER(samples_store, dev, buf, count)
{
	xbus_t			*xbus;

	xbus = dev_to_xbus(dev);
	if(xbus->sample_running)
		return -EBUSY;
	memset(xbus->sample_ticks, 0, sizeof(*xbus->sample_ticks));
	xbus->sample_pos = 0;
	xbus->sample_running = 1;
	return count;
}
#endif

/*
 * Clear statistics
 */
static DEVICE_ATTR_WRITER(cls_store, dev, buf, count)
{
	xbus_t			*xbus;
	struct xpp_drift	*driftinfo;

	xbus = dev_to_xbus(dev);
	driftinfo = &xbus->drift;
	driftinfo->lost_ticks = 0;
	driftinfo->lost_tick_count = 0;
	xbus->min_tx_sync = INT_MAX;
	xbus->max_tx_sync = 0;
	xbus->min_rx_sync = INT_MAX;
	xbus->max_rx_sync = 0;
#ifdef	SAMPLE_TICKS
	memset(xbus->sample_ticks, 0, sizeof(*xbus->sample_ticks));
#endif 
	return count;
}

static	DEVICE_ATTR(connector, S_IRUGO, connector_show, NULL);
static	DEVICE_ATTR(label, S_IRUGO, label_show, NULL);
static	DEVICE_ATTR(status, S_IRUGO, status_show, NULL);
static	DEVICE_ATTR(timing, S_IRUGO, timing_show, NULL);
static	DEVICE_ATTR(cls, S_IWUSR, NULL, cls_store);
#ifdef	SAMPLE_TICKS
static	DEVICE_ATTR(samples, S_IWUSR | S_IRUGO, samples_show, samples_store);
#endif

void xbus_sysfs_remove(xbus_t *xbus)
{
	struct device	*astribank;

	BUG_ON(!xbus);
	XBUS_DBG(GENERAL, xbus, "\n");
	astribank = &xbus->astribank;
	BUG_ON(!astribank);
	if(!astribank->driver_data)
		return;
	BUG_ON(astribank->driver_data != xbus);
#ifdef	SAMPLE_TICKS
	device_remove_file(&xbus->astribank, &dev_attr_samples);
#endif 
	device_remove_file(&xbus->astribank, &dev_attr_cls);
	device_remove_file(&xbus->astribank, &dev_attr_timing);
	device_remove_file(&xbus->astribank, &dev_attr_status);
	device_remove_file(&xbus->astribank, &dev_attr_label);
	device_remove_file(&xbus->astribank, &dev_attr_connector);
	device_unregister(&xbus->astribank);
}

int xbus_sysfs_create(xbus_t *xbus)
{
	struct device	*astribank;
	int		ret = 0;

	BUG_ON(!xbus);
	astribank = &xbus->astribank;
	BUG_ON(!astribank);
	XBUS_DBG(GENERAL, xbus, "\n");
	device_initialize(astribank);
	astribank->bus = &xpp_bus_type;
	astribank->parent = &xpp_bus;
	snprintf(astribank->bus_id, BUS_ID_SIZE, "xbus-%02d", xbus->num);
	astribank->driver_data = NULL;	/* override below */
	astribank->release = xpp_dev_release;
	ret = device_register(astribank);
	if(ret) {
		XBUS_ERR(xbus, "%s: device_add failed: %d\n", __FUNCTION__, ret);
		goto out;
	}
	ret = device_create_file(astribank, &dev_attr_connector);
	if(ret) {
		XBUS_ERR(xbus, "%s: device_create_file failed: %d\n", __FUNCTION__, ret);
		goto out;
	}
	ret = device_create_file(astribank, &dev_attr_label);
	if(ret) {
		XBUS_ERR(xbus, "%s: device_create_file failed: %d\n", __FUNCTION__, ret);
		goto out;
	}
	ret = device_create_file(astribank, &dev_attr_status);
	if(ret) {
		XBUS_ERR(xbus, "%s: device_create_file failed: %d\n", __FUNCTION__, ret);
		goto out;
	}
	ret = device_create_file(astribank, &dev_attr_timing);
	if(ret) {
		XBUS_ERR(xbus, "%s: device_create_file failed: %d\n", __FUNCTION__, ret);
		goto out;
	}
	ret = device_create_file(astribank, &dev_attr_cls);
	if(ret) {
		XBUS_ERR(xbus, "%s: device_create_file failed: %d\n", __FUNCTION__, ret);
		goto out;
	}
#ifdef	SAMPLE_TICKS
	ret = device_create_file(astribank, &dev_attr_samples);
	if(ret) {
		XBUS_ERR(xbus, "%s: device_create_file failed: %d\n", __FUNCTION__, ret);
		goto out;
	}
#endif 
	astribank->driver_data = xbus;	/* Everything is good */
out:
	return ret;
}
