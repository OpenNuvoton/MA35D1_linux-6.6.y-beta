// SPDX-License-Identifier: GPL-2.0
/*
 * Copyright (C) 2025, Nuvoton Technology Corporation
 * This program is free software; you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation; either version 2 of the License, or
 * (at your option) any later version.
 *
 */

#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/mod_devicetable.h>
#include <linux/slab.h>
#include <linux/err.h>
#include <linux/clk.h>
#include <linux/io.h>
#include <linux/hw_random.h>
#include <linux/platform_device.h>
#include <linux/completion.h>
#include <linux/of_platform.h>
#include <linux/tee_drv.h>

/*
 * PTA_CMD_TRNG_INIT - Initialize TRNG hardware
 *
 * param[0] (out value) - value.a: TRNG STAT register
 *                        value.b: TRNG ISTAT register
 * param[1] unused
 * param[2] unused
 * param[3] unused
 *
 * Result:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_TRNG_BUSY - TRNG hardware busy
 * TEE_ERROR_TRNG_GEN_NOISE - Failed to generate noise or nounce
 * TEE_ERROR_TRNG_COMMAND - TRNG command failed
 */
#define PTA_CMD_TRNG_INIT	0x1

/*
 * PTA_CMD_TRNG_READ - Get TRNG data
 *
 * param[0] (inout memref) - TRNG data buffer memory reference
 * param[1] unused
 * param[2] unused
 * param[3] unused
 *
 * Result:
 * TEE_SUCCESS - Invoke command success
 * TEE_ERROR_BAD_PARAMETERS - Incorrect input param
 */
#define PTA_CMD_TRNG_READ	0x2

#define RND_DATA_SHM_SZ		(4 * 1024)

/**
 * struct optee_rng_private - OP-TEE private data of MA35 TRNG
 * @dev:		OP-TEE based RNG device.
 * @ctx:		OP-TEE context handler.
 * @session_id:		RNG TA session identifier.
 * @rdata_shm_pool:	Memory pool shared with OP-TEE MA35 TRNG
 * @optee_rng:		Linux hwrng device data
 */
struct optee_rng_private {
	struct device *dev;
	struct tee_context *ctx;
	u32 session_id;
	struct tee_shm *rdata_shm_pool;
	struct hwrng optee_rng;
};

#define to_optee_rng_private(r)  container_of(r, struct optee_rng_private, optee_rng)

static size_t get_optee_rng_data(struct optee_rng_private *pvt_data,
				 void *buf, size_t req_size)
{
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	u8 *rng_data = NULL;
	size_t rng_size = 0;
	int ret = 0;

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	/* Invoke PTA_CMD_TRNG_READ function of Trusted App */
	inv_arg.func = PTA_CMD_TRNG_READ;
	inv_arg.session = pvt_data->session_id;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_MEMREF_INOUT;
	param[0].u.memref.shm = pvt_data->rdata_shm_pool;
	param[0].u.memref.size = req_size;
	param[0].u.memref.shm_offs = 0;

	ret = tee_client_invoke_func(pvt_data->ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(pvt_data->dev, "PTA_CMD_TRNG_READ invoke err: %x\n",
			inv_arg.ret);
		return 0;
	}

	rng_data = tee_shm_get_va(pvt_data->rdata_shm_pool, 0);
	if (IS_ERR(rng_data)) {
		dev_err(pvt_data->dev, "tee_shm_get_va failed\n");
		return 0;
	}

	rng_size = param[0].u.memref.size;
	memcpy(buf, rng_data, rng_size);

	return rng_size;
}

static int optee_rng_read(struct hwrng *rng, void *buf, size_t max, bool wait)
{
	struct optee_rng_private *pvt_data = to_optee_rng_private(rng);
	size_t read = 0, rng_size = 0;
	u8 *data = buf;

	if (max > RND_DATA_SHM_SZ)
		max = RND_DATA_SHM_SZ;

	if (max % 4)
		return -EINVAL;

	while (read == 0) {
		rng_size = get_optee_rng_data(pvt_data, data, (max - read));

		data += rng_size;
		read += rng_size;
	}
	return read;
}

static int optee_rng_init(struct hwrng *rng)
{
	struct optee_rng_private *pvt_data = to_optee_rng_private(rng);
	struct tee_shm *rdata_shm_pool = NULL;

	rdata_shm_pool = tee_shm_alloc_kernel_buf(pvt_data->ctx, RND_DATA_SHM_SZ);
	if (IS_ERR(rdata_shm_pool)) {
		dev_err(pvt_data->dev, "tee_shm_alloc failed\n");
		return PTR_ERR(rdata_shm_pool);
	}

	pvt_data->rdata_shm_pool = rdata_shm_pool;

	return 0;
}

static void optee_rng_cleanup(struct hwrng *rng)
{
	struct optee_rng_private *pvt_data = to_optee_rng_private(rng);

	tee_shm_free(pvt_data->rdata_shm_pool);
}

static struct optee_rng_private pvt_data = {
	.optee_rng = {
		.name		= "ma35-trng-optee",
		.init		= optee_rng_init,
		.cleanup	= optee_rng_cleanup,
		.read		= optee_rng_read,
	}
};

static int ma35_optee_trng_init(struct device *dev)
{
	struct tee_ioctl_invoke_arg inv_arg;
	struct tee_param param[4];
	int ret = 0;

	memset(&inv_arg, 0, sizeof(inv_arg));
	memset(&param, 0, sizeof(param));

	/* Invoke PTA_CMD_TRNG_INIT function of Trusted App */
	inv_arg.func = PTA_CMD_TRNG_INIT;
	inv_arg.session = pvt_data.session_id;
	inv_arg.num_params = 4;

	/* Fill invoke cmd params */
	param[0].attr = TEE_IOCTL_PARAM_ATTR_TYPE_VALUE_OUTPUT;

	ret = tee_client_invoke_func(pvt_data.ctx, &inv_arg, param);
	if ((ret < 0) || (inv_arg.ret != 0)) {
		dev_err(dev, "PTA_CMD_TRNG_INIT invoke err: %x\n", inv_arg.ret);
		return -EINVAL;
	}

	pr_info("optee get TRNG STAT=0x%llx, ISTAT=0x%llx\n",
		param[0].u.value.a, param[0].u.value.b);

	return 0;
}

static int optee_ctx_match(struct tee_ioctl_version_data *ver, const void *data)
{
	if (ver->impl_id == TEE_IMPL_ID_OPTEE)
		return 1;
	else
		return 0;
}

static int optee_rng_probe(struct device *dev)
{
	struct tee_client_device *rng_device = to_tee_client_device(dev);
	struct tee_ioctl_open_session_arg sess_arg;
	int ret = 0, err = -ENODEV;

	memset(&sess_arg, 0, sizeof(sess_arg));

	/* Open context with TEE driver */
	pvt_data.ctx = tee_client_open_context(NULL, optee_ctx_match, NULL, NULL);
	if (IS_ERR(pvt_data.ctx))
		return -ENODEV;

	/* Open session with hwrng Trusted App */
	memcpy(sess_arg.uuid, rng_device->id.uuid.b, TEE_IOCTL_UUID_LEN);
	sess_arg.clnt_login = TEE_IOCTL_LOGIN_PUBLIC;
	sess_arg.num_params = 0;

	ret = tee_client_open_session(pvt_data.ctx, &sess_arg, NULL);
	if ((ret < 0) || (sess_arg.ret != 0)) {
		dev_err(dev, "TRNG tee_client_open_session failed, err: %x\n", sess_arg.ret);
		err = -EINVAL;
		goto out_ctx;
	}
	pvt_data.session_id = sess_arg.session;

	err = ma35_optee_trng_init(dev);
	if (err)
		goto out_sess;

	err = hwrng_register(&pvt_data.optee_rng);
	if (err) {
		dev_err(dev, "optee hwrng registration failed (%d)\n", err);
		goto out_sess;
	}

	pvt_data.dev = dev;

	return 0;

out_sess:
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
out_ctx:
	tee_client_close_context(pvt_data.ctx);

	return err;
}

static int optee_rng_remove(struct device *dev)
{
	hwrng_unregister(&pvt_data.optee_rng);
	tee_client_close_session(pvt_data.ctx, pvt_data.session_id);
	tee_client_close_context(pvt_data.ctx);

	return 0;
}

static const struct tee_client_device_id optee_rng_id_table[] = {
	{ UUID_INIT(0x9f831ffa, 0x1823, 0x4ee9,
		   0x8f, 0xb2, 0x41, 0x57, 0x1f, 0x64, 0x32, 0xe1) },
	{}
};

MODULE_DEVICE_TABLE(tee, optee_rng_id_table);

static struct tee_client_driver optee_rng_driver = {
	.id_table	= optee_rng_id_table,
	.driver		= {
		.name		= "optee-nvt-trng",
		.bus		= &tee_bus_type,
		.probe		= optee_rng_probe,
		.remove		= optee_rng_remove,
	},
};

static int ma35_trng_probe(struct platform_device *pdev)
{
	int err;

	err = driver_register(&optee_rng_driver.driver);
	if (err)
		pr_debug("Failed to register ma35-trng-optee!\n");
	else
		pr_info("ma35-trng-optee initialized.\n");

	return err;
}

static int ma35_trng_remove(struct platform_device *pdev)
{
	return 0;
}

static const struct of_device_id ma35_trng_dt_ids[] = {
	{ .compatible = "nuvoton,ma35d1-trng-optee" },
	{ /* sentinel */ }
};
MODULE_DEVICE_TABLE(of, ma35_trng_dt_ids);

static struct platform_driver ma35_trng_driver = {
	.probe		= ma35_trng_probe,
	.remove		= ma35_trng_remove,
	.driver		= {
		.name = "ma35-trng-optee",
		.of_match_table = ma35_trng_dt_ids,
	},
};

module_platform_driver(ma35_trng_driver);

MODULE_LICENSE("GPL v2");
MODULE_DESCRIPTION("Nuvoton MA35 TRNG optee driver");
