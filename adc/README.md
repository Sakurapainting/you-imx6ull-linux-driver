# ADC

## 寄存器地址定义

在IMX6ULL参考手册中的ADC部分找到 `ADC memory map` ，这个表写了ADC相关寄存器地址，在驱动中宏定义这些地址。

```c
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
```

```c
module_platform_driver(imx6ull_adc_driver);
```

这句宏可以省略 module_init 和 module_exit 函数。

在init和exit中 不需要额外的初始化、清理函数时，用这个可以使代码简洁

modprobe 模块之后，去 /proc/device-tree 里找 soc

具体在哪里，要看 `imx6ull-dtsi` 里的节点

完成了platform框架

```c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/driver.h>

#define IMX6ULL_ADC_NAME "imx6ull-adc"

/* IMX ADC registers */
#define IMX6ULL_REG_ADC_HC0		0x00
#define IMX6ULL_REG_ADC_HC1		0x04
#define IMX6ULL_REG_ADC_HS		0x08
#define IMX6ULL_REG_ADC_R0		0x0c
#define IMX6ULL_REG_ADC_R1		0x10
#define IMX6ULL_REG_ADC_CFG		0x14
#define IMX6ULL_REG_ADC_GC		0x18
#define IMX6ULL_REG_ADC_GS		0x1c
#define IMX6ULL_REG_ADC_CV		0x20
#define IMX6ULL_REG_ADC_OFS		0x24
#define IMX6ULL_REG_ADC_CAL		0x28
#define IMX6ULL_REG_ADC_PCTL	0x30

static const struct of_device_id imx6ull_adc_match[] = {
    { .compatible = "fsl,vf610-adc", },
    { /* sentinel */ }
};

static int imx6ull_adc_probe(struct platform_device *pdev)
{
    printk(KERN_INFO "IMX6ULL ADC Driver Probed\n");
    return 0;
}

static int imx6ull_adc_remove(struct platform_device *pdev)
{
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
```

modprobe后，在/sys/bus/platform/drivers可以找到 `imx6ull-adc` 

## 关于使用 iio_dev->mlock 的问题

- 内核版本兼容性：在较新的内核版本中（4.18+），iio_dev->mlock 已经被弃用，因为它主要是为IIO核心内部使用而设计的。

- 推荐做法：IIO子系统建议驱动程序使用自己的互斥锁来保护设备特定的操作。

## 建立完platform之后

定义结构体

```c
struct imx6ull_adc {
	struct device *dev;

	struct mutex lock;
};
```

然后写probe 函数

```c
struct imx6ull_adc *info;
	struct iio_dev *indio_dev;

	iio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(*info));
	if (!iio_dev){
		dev_err(&pdev->dev, "Failed to allocate iio device\n");
		return -ENOMEM;
	}
```

申请 iio device 设备

```c
info = iio_priv(indio_dev);
info->dev = &pdev->dev;
```

保存dev指针，下次要访问platform_device 里的dev指针，就可以获取priv获取iio私有空间里保存的指针，来访问自定义结构体下的 device 结构体

这个info可以在probe函数外使用，因为 info 指向的内存由设备管理器管理，直到设备被移除才释放

## 需要显式设置 of_node

这个ADC控制器是平台设备，和i2c/spi设备不一样，不会在i2c/spi框架下自动获取of_node，需要手动设置 indio_dev->dev.of_node

## iio 初始化和注册

```c
indio_dev->name = dev_name(&pdev->dev);
	indio_dev->dev.parent = &pdev->dev;
	indio_dev->dev.of_node = pdev->dev.of_node;
	indio_dev->info = &imx6ull_adc_iio_info;
	indio_dev->modes = INDIO_DIRECT_MODE;
	indio_dev->channels = imx6ull_adc_iio_channels;
	indio_dev->num_channels = (int)channels;

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		goto fail_iio_device_register;
	}
```

## imx6ull手册里有Control register、 Status register、Data result register、Configuration register 、 General control register、General status register、Compare value register、Offset correction value register、 Calibration value register。这么多的fied description，但是为什么这个vf610_adc代码里只写了这三种的定义？

代码只定义了实际使用的寄存器位域，这是一种务实的方法：

- CFG 寄存器：配置核心功能，位域定义最多
- GC 寄存器：控制基本操作，定义了关键位
- 其他寄存器：只定义了必要的状态检查位
如果需要扩展功能（如比较、偏移校正等），再添加相应的位域定义即可。

## struct imx6ull_adc_feature

是一个配置管理结构体，它的作用是：

集中管理：将所有ADC硬件配置参数集中在一个结构体中
参数验证：提供类型安全的配置选项
硬件抽象：将复杂的寄存器操作抽象为简单的参数设置
配置复用：可以方便地保存、恢复、修改配置
代码清晰：使配置逻辑更加清晰和易于维护

## 设备树中的追加规则中的compatible

在设备树中，当您在 &adc1 节点中添加 compatible 属性时，它会覆盖原有的 compatible 属性，而不是追加到后面。

设备树属性覆盖规则
设备树的覆盖（overlay）机制遵循以下规则：

- 同名属性会被完全替换
- 新属性会被添加
- 子节点会递归合并

所以，驱动中这样写的话

```c
static const struct of_device_id imx6ull_adc_match[] = {
    { .compatible = "fsl,imx6ull-adc", },
    { /* sentinel */ }
};
```

就需要在设备树中追加

```c
/*you ADC1_CH1*/
&adc1 {
    compatible = "fsl,imx6ul-adc", "fsl,vf610-adc", "fsl,imx6ull-adc";
    pinctrl-names = "default";
    pinctrl-0 = <&pinctrl_adc1>;
    num-channels = <2>;
    vref-supply = <&reg_vref_adc>;
    status = "okay";
};
```

## 补充 adc1 在 imx6ull.dtsi 中的定义

```c
adc1: adc@02198000 {
				compatible = "fsl,imx6ul-adc", "fsl,vf610-adc";
				reg = <0x02198000 0x4000>;
				interrupts = <GIC_SPI 100 IRQ_TYPE_LEVEL_HIGH>;
				clocks = <&clks IMX6UL_CLK_ADC1>;
				num-channels = <2>;
				clock-names = "adc";
				status = "disabled";
                        };
```

## ADC初始化序列

参考手册上：


在使用 ADC 模块完成转换之前，必须执行初始化程序。典型序列如下：

• 按照校准功能中的校准说明校准 ADC

• 更新配置寄存器（ADC_CFG），以选择输入时钟源和用于生成内部时钟 ADCK 的分频比。此寄存器也用于选择采样时间和低功耗配置。

• 更新通用控制寄存器（ADC_GC），选择转换是连续进行还是仅完成一次（ADCO），以及选择是否执行硬件平均等。

• 更新触发控制寄存器（ADC_HCn），选择转换触发方式（硬件或软件，即配置 ADTRG 位）和比较功能选项，如果启用。

## 通道

设备树配置 num-channels = 2 的情况下，确实可以删除 VF610_ADC_CHAN(2, IIO_VOLTAGE) 及其后面的所有通道定义。

那么我们就不需要像imx6ull提供的VF610示例代码那样了：

```c
static const struct iio_chan_spec vf610_adc_iio_channels[] = {
	VF610_ADC_CHAN(0, IIO_VOLTAGE),
	VF610_ADC_CHAN(1, IIO_VOLTAGE),
	VF610_ADC_CHAN(2, IIO_VOLTAGE),
	VF610_ADC_CHAN(3, IIO_VOLTAGE),
	VF610_ADC_CHAN(4, IIO_VOLTAGE),
	VF610_ADC_CHAN(5, IIO_VOLTAGE),
	VF610_ADC_CHAN(6, IIO_VOLTAGE),
	VF610_ADC_CHAN(7, IIO_VOLTAGE),
	VF610_ADC_CHAN(8, IIO_VOLTAGE),
	VF610_ADC_CHAN(9, IIO_VOLTAGE),
	VF610_ADC_CHAN(10, IIO_VOLTAGE),
	VF610_ADC_CHAN(11, IIO_VOLTAGE),
	VF610_ADC_CHAN(12, IIO_VOLTAGE),
	VF610_ADC_CHAN(13, IIO_VOLTAGE),
	VF610_ADC_CHAN(14, IIO_VOLTAGE),
	VF610_ADC_CHAN(15, IIO_VOLTAGE),
	VF610_ADC_TEMPERATURE_CHAN(26, IIO_TEMP),
	/* sentinel */
};

```

1. 减少内存占用
减少内核代码段大小
减少不必要的数据结构定义
2. 提高代码可读性
代码更简洁，一目了然
避免混淆：定义的通道数与实际使用的通道数一致
3. 避免潜在错误
防止意外访问未使用的通道定义
减少维护负担

```c
static const struct iio_chan_spec imx6ull_adc_iio_channels[] = {
	IMX6ULL_ADC_CHAN(0, IIO_VOLTAGE),
	IMX6ULL_ADC_CHAN(1, IIO_VOLTAGE),
};
```

定义IMX6ULL_ADC_CHAN

```c
#define IMX6ULL_ADC_CHAN(_idx, _chan_type) {			\
	.type = (_chan_type),					\
	.indexed = 1,						\
	.channel = (_idx),					\
	.info_mask_separate = BIT(IIO_CHAN_INFO_RAW),		\
	.info_mask_shared_by_type = BIT(IIO_CHAN_INFO_SCALE) |	\
				BIT(IIO_CHAN_INFO_SAMP_FREQ),	\
}
```

- .indexed = 1, ： 索引通道，sysfs 路径包含通道编号。反之为0则是非索引通道。

```bash
/sys/bus/iio/devices/iio:device0/in_voltage0_raw    # 通道 0
/sys/bus/iio/devices/iio:device0/in_voltage1_raw    # 通道 1
/sys/bus/iio/devices/iio:device0/in_voltage2_raw    # 通道 2
```

```bash
/sys/bus/iio/devices/iio:device0/in_voltage_raw     # 只有一个通道
```

## 部分remove

```c
static int imx6ull_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct imx6ull_adc *info = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

    printk(KERN_INFO "IMX6ULL ADC Driver Removed\n");
    return 0;
}
```

`platform_get_drvdata` 能通过平台设备指针访问iio设备前提是在probe的时候 `platform_set_drvdata(pdev, indio_dev);`

```c
platform_set_drvdata(pdev, indio_dev);
```

## 阶段性测试 iio框架

```c
#include <linux/module.h>
#include <linux/platform_device.h>
#include <linux/kernel.h>

#include <linux/iio/iio.h>
#include <linux/iio/sysfs.h>
#include <linux/iio/driver.h>

#define IMX6ULL_ADC_NAME "imx6ull-adc"

/* IMX ADC registers */
#define IMX6ULL_REG_ADC_HC0		0x00
#define IMX6ULL_REG_ADC_HC1		0x04
#define IMX6ULL_REG_ADC_HS		0x08
#define IMX6ULL_REG_ADC_R0		0x0c
#define IMX6ULL_REG_ADC_R1		0x10
#define IMX6ULL_REG_ADC_CFG		0x14
#define IMX6ULL_REG_ADC_GC		0x18
#define IMX6ULL_REG_ADC_GS		0x1c
#define IMX6ULL_REG_ADC_CV		0x20
#define IMX6ULL_REG_ADC_OFS		0x24
#define IMX6ULL_REG_ADC_CAL		0x28
#define IMX6ULL_REG_ADC_PCTL	0x30

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

	struct mutex lock;
};

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

	u32 channels;

	indio_dev = devm_iio_device_alloc(&pdev->dev, sizeof(struct imx6ull_adc));
	if (!indio_dev){
		dev_err(&pdev->dev, "Failed to allocate iio device\n");
		return -ENOMEM;
	}

	info = iio_priv(indio_dev);
	info->dev = &pdev->dev;

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

	ret = iio_device_register(indio_dev);
	if (ret) {
		dev_err(&pdev->dev, "Couldn't register the device.\n");
		goto fail_iio_device_register;
	}

    printk(KERN_INFO "IMX6ULL ADC Driver Probed\n");
    return 0;
	
fail_iio_device_register:
	return ret;
}

static int imx6ull_adc_remove(struct platform_device *pdev)
{
	struct iio_dev *indio_dev = platform_get_drvdata(pdev);
	struct imx6ull_adc *info = iio_priv(indio_dev);

	iio_device_unregister(indio_dev);

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
```

## 中断（和其中的completion）

IRQ 不保存在结构体中是因为：

- 使用模式：一次注册，自动处理，无需后续操作
- 资源管理：devm_request_irq() 自动管理生命周期
- 设计原则：只保存需要反复使用的资源

```c
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
```

imx6ull_adc_isr里处理adc转换完成中断

```c
static irqreturn_t vf610_adc_isr(int irq, void *dev_id)
{
    struct vf610_adc *info = (struct vf610_adc *)dev_id;
    int coco;

    coco = readl(info->regs + VF610_REG_ADC_HS);
    if (coco & VF610_ADC_HS_COCO0) {
        info->value = vf610_adc_read_data(info);
        complete(&info->completion);
    }

    return IRQ_HANDLED;
}
```

### 1. **检查中断状态**
````c
coco = readl(info->regs + VF610_REG_ADC_HS);
````
- 读取 ADC 硬件状态寄存器 (`VF610_REG_ADC_HS`)
- `coco` 代表 "Conversion Complete" 状态

readl 需要 #include <linux/io.h>

### 2. **验证中断源**
````c
if (coco & VF610_ADC_HS_COCO0) {
````
- 检查是否是通道 0 的转换完成中断
- `VF610_ADC_HS_COCO0` 是转换完成标志位

// 转换完成时:
coco = 0x00000001;  // COCO0 = 1
coco & 0x00000001 = 0x00000001;  // 非零 → 条件为真

// 转换未完成时:
coco = 0x00000000;  // COCO0 = 0  
coco & 0x00000001 = 0x00000000;  // 零 → 条件为假

### 3. **读取转换结果**
````c
info->value = vf610_adc_read_data(info);
````
- 调用 `vf610_adc_read_data()` 读取 ADC 转换结果
- 将结果保存到 `info->value` 中

### 4. **通知等待任务**
````c
complete(&info->completion);
````
- 通过 `completion` 机制通知等待中的任务
- 唤醒在 `wait_for_completion_*()` 中等待的代码

completion 是 Linux 内核提供的一种同步原语，专门用于一个线程等待另一个线程完成某项工作的场景，需要 #include <linux/completion.h>

```c
struct vf610_adc {
    // ...
    struct completion completion;  // 定义 completion 对象
};

// 初始化
init_completion(&info->completion);

// 重新初始化（清除之前的完成状态）
reinit_completion(&info->completion);

// 等待完成（带超时和可中断）
ret = wait_for_completion_interruptible_timeout(&info->completion, VF610_ADC_TIMEOUT);

// 通知完成
complete(&info->completion);
```

````c
static int vf610_adc_read_data(struct vf610_adc *info)
{
	int result;

	result = readl(info->regs + VF610_REG_ADC_R0);

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
````

### 1. **读取原始数据**

````c
result = readl(info->regs + VF610_REG_ADC_R0);
````
- 从 ADC 结果寄存器 `VF610_REG_ADC_R0`（偏移 0x0c）读取 32 位数据
- 这个寄存器包含最新的 ADC 转换结果

### 2. **数据格式化**

根据当前设置的分辨率模式，屏蔽掉高位无效数据：

````c
switch (info->adc_feature.res_mode) {
case 8:
    result &= 0xFF;      // 保留低 8 位，范围 0-255
    break;
case 10:
    result &= 0x3FF;     // 保留低 10 位，范围 0-1023
    break;
case 12:
    result &= 0xFFF;     // 保留低 12 位，范围 0-4095
    break;
}
````

```c
// 原始值
result = 0x12345678;  // 二进制：00010010001101000101011001111000

// 8位模式
result &= 0xFF;       // 结果：0x78 = 120
// 计算过程：
// 0x12345678 & 0x000000FF = 0x00000078

// 10位模式  
result &= 0x3FF;      // 结果：0x278 = 632
// 计算过程：
// 0x12345678 & 0x000003FF = 0x00000278

// 12位模式
result &= 0xFFF;      // 结果：0x678 = 1656
// 计算过程：
// 0x12345678 & 0x00000FFF = 0x00000678
```

result返回数据用于电压计算：

```c
// 在 vf610_read_raw 中
case IIO_CHAN_INFO_SCALE:
    *val = info->vref_uv / 1000;           // 参考电压（mV）
    *val2 = info->adc_feature.res_mode;    // 分辨率位数
    return IIO_VAL_FRACTIONAL_LOG2;

// 用户空间计算实际电压：
// 实际电压 = (ADC值 / 2^分辨率) * 参考电压
// 例如：12位模式，参考电压3.3V，ADC值2048
// 实际电压 = (2048 / 4096) * 3.3V = 1.65V
```

`vf610_adc_read_data` 函数的作用是：

1. **读取硬件寄存器**：从 ADC_R0 获取原始转换结果
2. **数据清理**：根据分辨率模式屏蔽无效位
3. **范围控制**：确保数据在有效范围内
4. **提供标准接口**：为上层提供格式化的 ADC 数据

## 内存映射

```c
    mem = platform_get_resource(pdev, IORESOURCE_MEM, 0);
    info->regs = devm_ioremap_resource(&pdev->dev, mem);
    if (IS_ERR(info->regs))
		return PTR_ERR(info->regs);
```

手册不会明显提到内存映射，因为这是软件实现细节

- 手册关注的是硬件功能：中断、时钟、ADC 转换
- 内存映射是 Linux 驱动访问硬件的标准方法

物理地址: 0x4003b000 (设备树中定义)
    ↓ ioremap_resource()
虚拟地址: info->regs (驱动中使用)

访问示例:
- ADC_CFG 寄存器: info->regs + 0x14
- ADC_R0 寄存器:  info->regs + 0x0c
