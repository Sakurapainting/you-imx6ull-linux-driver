#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>
#include <linux/io.h>
#include <linux/interrupt.h>
#include <linux/completion.h>
#include <linux/clk.h>
#include <linux/regulator/consumer.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/driver.h>

#define IMX6ULL_ADC_NAME "imx6ull-adc"

/* IMX ADC registers */
#define IMX6ULL_REG_ADC_HC0		0x00
#define IMX6ULL_REG_ADC_HS		0x08
#define IMX6ULL_REG_ADC_R0		0x0c
#define IMX6ULL_REG_ADC_CFG		0x14
#define IMX6ULL_REG_ADC_GC		0x18
#define IMX6ULL_REG_ADC_GS		0x1c
#define IMX6ULL_REG_ADC_CV		0x20
#define IMX6ULL_REG_ADC_OFS		0x24
#define IMX6ULL_REG_ADC_CAL		0x28

/* Configuration register field define */
#define IMX6ULL_ADC_MODE_BIT8		0x00
#define IMX6ULL_ADC_MODE_BIT10		0x04
#define IMX6ULL_ADC_MODE_BIT12		0x08
#define IMX6ULL_ADC_MODE_MASK		0x0c
#define IMX6ULL_ADC_BUSCLK2_SEL		0x01
#define IMX6ULL_ADC_ALTCLK_SEL		0x02
#define IMX6ULL_ADC_ADACK_SEL		0x03
#define IMX6ULL_ADC_ADCCLK_MASK		0x03
#define IMX6ULL_ADC_CLK_DIV2		0x20
#define IMX6ULL_ADC_CLK_DIV4		0x40
#define IMX6ULL_ADC_CLK_DIV8		0x60
#define IMX6ULL_ADC_CLK_MASK		0x60
#define IMX6ULL_ADC_ADLSMP_LONG		0x10
#define IMX6ULL_ADC_ADSTS_MASK		0x300
#define IMX6ULL_ADC_ADLPC_EN		0x80
#define IMX6ULL_ADC_ADHSC_EN		0x400
#define IMX6ULL_ADC_REFSEL_VALT		0x100
#define IMX6ULL_ADC_REFSEL_VBG		0x1000
#define IMX6ULL_ADC_ADTRG_HARD		0x2000
#define IMX6ULL_ADC_AVGS_8		0x4000
#define IMX6ULL_ADC_AVGS_16		0x8000
#define IMX6ULL_ADC_AVGS_32		0xC000
#define IMX6ULL_ADC_AVGS_MASK		0xC000
#define IMX6ULL_ADC_OVWREN		0x10000

/* General control register field define */
#define IMX6ULL_ADC_ADACKEN		0x1
#define IMX6ULL_ADC_DMAEN			0x2
#define IMX6ULL_ADC_ACREN			0x4
#define IMX6ULL_ADC_ACFGT			0x8
#define IMX6ULL_ADC_ACFE			0x10
#define IMX6ULL_ADC_AVGEN			0x20
#define IMX6ULL_ADC_ADCON			0x40
#define IMX6ULL_ADC_CAL			0x80

/* Other field define */
#define IMX6ULL_ADC_ADCHC(x)		((x) & 0x1F)
#define IMX6ULL_ADC_AIEN			(0x1 << 7)
#define IMX6ULL_ADC_CONV_DISABLE		0x1F
#define IMX6ULL_ADC_HS_COCO0		0x1
#define IMX6ULL_ADC_CALF			0x2
#define IMX6ULL_ADC_TIMEOUT		msecs_to_jiffies(100)

#define IMX6ULL_ADC_CHAN(_idx, _chan_type) {			\
	.type = (_chan_type),					\
	.indexed = 1,						\
	.channel = (_idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
}

enum clk_sel {
	IMX6ULL_ADCIOC_BUSCLK_SET,
	IMX6ULL_ADCIOC_ALTCLK_SET,
	IMX6ULL_ADCIOC_ADACK_SET,
};

enum vol_ref {
	IMX6ULL_ADCIOC_VR_VREF_SET,
	IMX6ULL_ADCIOC_VR_VALT_SET,
	IMX6ULL_ADCIOC_VR_VBG_SET,
};

enum average_sel {
	IMX6ULL_ADC_SAMPLE_1,
	IMX6ULL_ADC_SAMPLE_4,
	IMX6ULL_ADC_SAMPLE_8,
	IMX6ULL_ADC_SAMPLE_16,
	IMX6ULL_ADC_SAMPLE_32,
};

struct imx6ull_adc_feature {
	enum clk_sel	clk_sel;
	enum vol_ref	vol_ref;

	int	clk_div;
	int     sample_rate;
	int	res_mode;

	bool	lpm;
	bool	calibration;
	bool	ovwren;
};

static const struct iio_chan_spec imx6ull_adc_iio_channels[] = {
	IMX6ULL_ADC_CHAN(0, IIO_VOLTAGE),
	IMX6ULL_ADC_CHAN(1, IIO_VOLTAGE),
};

struct imx6ull_adc {
	struct device *dev;
	void __iomem *regs;
	struct clk *clk;

	u32 value;
	u32 vref_uv;
	struct regulator *vref;

	struct imx6ull_adc_feature adc_feature;
	struct completion completion;
	struct mutex lock;
};

static int imx6ull_adc_read_data(struct imx6ull_adc *info)
{
	int result;

	result = readl(info->regs + IMX6ULL_REG_ADC_R0);

	switch (info->adc_feature.res_mode) {
		case 8:
			result &= 0xFF;
			break;
		case 10:
			result &= 0x3FF;
			break;
		case 12:
			result &= 0xFFF;
			break;
		default:
			break;
	}

	return result;
}

static irqreturn_t imx6ull_adc_isr(int irq, void *dev_id) {
	struct imx6ull_adc *info = (struct imx6ull_adc *)dev_id;
	int coco;

	coco = readl(info->regs + IMX6ULL_REG_ADC_HS);
	if (coco & IMX6ULL_ADC_HS_COCO0) {
		info->value = imx6ull_adc_read_data(info);
		complete(&info->completion);
	}

	return IRQ_HANDLED;
}

static const struct iio_info imx6ull_adc_iio_info = {
	.driver_module = THIS_MODULE,
};

static const struct of_device_id imx6ull_adc_match[] = {
    { .compatible = "fsl,imx6ull-adc", },
    { /* sentinel */ }
};

static int imx6ull_adc_probe(struct platform_device *pdev)
{
	struct imx6ull_adc *info;
	struct iio_dev *indio_dev;
	int ret;

	struct resource *mem;
	int irq;

	u32 channels;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(struct imx6ull_adc));
	if (!indio_dev){
		dev_err(&pdev->dev, "Failed to allocate iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);
	info->dev = &pdev->dev;

	mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	info->regs = devm_ioremap_resource(&pdev->dev, mem);
	if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);

	irq = platform_get_irq(pdev, 0);
	if (irq < 0) {
		dev_err(&pdev->dev, "no irq resource?\n");
		return irq;
	}

	ret = devm_request_irq(info->dev, irq,
				imx6ull_adc_isr, 0,
				dev_name(&pdev->dev), info);
	if (ret < 0) {
		dev_err(&pdev->dev, "failed requesting irq, irq = %d\n", irq);
		return ret;
	}

	info->clk = devm_clk_get(&pdev->dev, "adc");
	if (IS_ERR(info->clk)) {
		dev_err(&pdev->dev, "failed getting clock, err = %ld\n",
						PTR_ERR(info->clk));
		return PTR_ERR(info->clk);
	}

	info->vref = devm_regulator_get(&pdev->dev, "vref");
	if (IS_ERR(info->vref))
		return PTR_ERR(info->vref);

	ret = regulator_enable(info->vref);
	if (ret)
		return ret;

	info->vref_uv = regulator_get_voltage(info->vref);

	init_completion(&info->completion);

	platform_set_drvdata(pdev, indio_dev);

	ret  = of_property_read_u32(pdev->dev.of_node,
					"num-channels", &channels);
	if (ret)
		channels = ARRAY_SIZE(imx6ull_adc_iio_channels);

	indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &imx6ull_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = imx6ull_adc_iio_channels;
	indio_dev->num_channels = (int)channels;

	ret = clk_prepare_enable(info->clk);
	if (ret) {
		dev_err(&pdev->dev,
			"Could not prepare or enable the clock.\n");
		goto fail_adc_clk_enable;
	}

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		goto fail_iio_device_register;
	}

    printk(KERN_INFO "IMX6ULL ADC Driver Probed\n");
    return 0;

fail_iio_device_register:
	clk_disable_unprepare(info->clk);
fail_adc_clk_enable:
	regulator_disable(info->vref);
	return ret;
}

static int imx6ull_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct imx6ull_adc *info = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);
	clk_disable_unprepare(info->clk);
	regulator_disable(info->vref);

    printk(KERN_INFO "IMX6ULL ADC Driver Removed\n");
    return 0;
}

static struct platform_driver imx6ull_adc_driver = {
	.probe          = imx6ull_adc_probe,
	.remove         = imx6ull_adc_remove,
	.driver         = {
		.name   = IMX6ULL_ADC_NAME,
		.of_match_table = imx6ull_adc_match,
		// .pm     = &imx6ull_adc_pm_ops,
	},
};

module_platform_driver(imx6ull_adc_driver);

MODULE_AUTHOR("SakoroYou");
MODULE_DESCRIPTION("YOU IMX6ULL ADC Driver");
MODULE_LICENSE("GPL v2");