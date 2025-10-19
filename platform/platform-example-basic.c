#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>

#define IMX6ULL_PLATFORM_NAME "imx6ull-platform"

static const struct of_device_id imx6ull_platform_match[] = {
    { .compatible = "nxp,imx6ull-platform", },
    { /* sentinel */ }
};

static int imx6ull_platform_probe(struct platform_device *pdev)
{
    printk(KERN_INFO "IMX6ULL PLATFORM Driver Probed\n");
    return 0;
}

static int imx6ull_platform_remove(struct platform_device *pdev)
{
    printk(KERN_INFO "IMX6ULL PLATFORM Driver Removed\n");
    return 0;
}

static struct platform_driver imx6ull_platform_driver = {
	.probe          = imx6ull_platform_probe,
	.remove         = imx6ull_platform_remove,
	.driver         = {
		.name   = IMX6ULL_PLATFORM_NAME,
		.of_match_table = imx6ull_platform_match,
		// .pm     = &imx6ull_platform_pm_ops,
	},
};

module_platform_driver(imx6ull_platform_driver);

MODULE_AUTHOR("SakoroYou");
MODULE_DESCRIPTION("YOU IMX6ULL PLATFORM Driver");
MODULE_LICENSE("GPL v2");