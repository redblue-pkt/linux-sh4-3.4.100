/*
 *  drivers/serial/stasc.c
 *  Asynchronous serial controller (ASC) driver
 */

#if defined(CONFIG_SERIAL_STM_ASC_CONSOLE) && defined(CONFIG_MAGIC_SYSRQ)
#define SUPPORT_SYSRQ
#endif

#include <linux/module.h>
#include <linux/io.h>
#include <linux/irq.h>
#include <linux/uaccess.h>
#include <linux/bitops.h>
#include <linux/tty.h>
#include <linux/tty_flip.h>
#include <linux/sysrq.h>
#include <linux/serial.h>
#include <linux/init.h>
#include <linux/delay.h>
#include <linux/console.h>
#include <linux/gpio.h>
#include <linux/generic_serial.h>
#include <linux/spinlock.h>
#include <linux/pm_runtime.h>
#include <linux/platform_device.h>
#include <linux/of.h>
#include <linux/of_platform.h>
#include <linux/stm/platform.h>
#include <linux/clk.h>

#include "stm-asc.h"

#define DRIVER_NAME "stm-asc"

#ifdef CONFIG_SERIAL_STM_ASC_CONSOLE
static struct console asc_console;
#endif

struct platform_device *stm_asc_console_device;

struct asc_port asc_ports[ASC_MAX_PORTS];

/*---- Forward function declarations---------------------------*/
static int  asc_request_irq(struct uart_port *);
static void asc_free_irq(struct uart_port *);
static void asc_transmit_chars(struct uart_port *);
static int asc_remap_port(struct asc_port *ascport, int req);
static int asc_set_baud(struct asc_port *ascport, int baud);
void asc_set_termios_cflag(struct asc_port *, int, int);
static inline void asc_receive_chars(struct uart_port *);

#ifdef CONFIG_SERIAL_STM_ASC_CONSOLE
static void asc_console_write(struct console *, const char *, unsigned);
static int __init asc_console_setup(struct console *, char *);
#endif

/*---- Inline function definitions ---------------------------*/

/* Some simple utility functions to enable and disable interrupts.
 * Note that these need to be called with interrupts disabled.
 */
static inline void asc_disable_tx_interrupts(struct uart_port *port)
{
	struct asc_port *ascport = container_of(port, struct asc_port, port);

	/* Clear TE (Transmitter empty) interrupt enable in INTEN */
	if (ascport->inten & ASC_INTEN_THE) {
		ascport->inten &= ~ASC_INTEN_THE;
		asc_out(port, INTEN, ascport->inten);
		(void)asc_in(port, INTEN);	/* Defeat write posting */
	}
}

static inline void asc_enable_tx_interrupts(struct uart_port *port)
{
	struct asc_port *ascport = container_of(port, struct asc_port, port);

	/* Set TE (Transmitter empty) interrupt enable in INTEN */
	if (! (ascport->inten & ASC_INTEN_THE)) {
		ascport->inten |= ASC_INTEN_THE;
		asc_out(port, INTEN, ascport->inten);
	}
}

static inline void asc_disable_rx_interrupts(struct uart_port *port)
{
	struct asc_port *ascport = container_of(port, struct asc_port, port);

	/* Clear RBE (Receive Buffer Full Interrupt Enable) bit in INTEN */
	if (ascport->inten & ASC_INTEN_RBE) {
		ascport->inten &= ~ASC_INTEN_RBE;
		asc_out(port, INTEN, ascport->inten);
		(void)asc_in(port, INTEN);	/* Defeat write posting */
	}
}

static inline void asc_enable_rx_interrupts(struct uart_port *port)
{
	struct asc_port *ascport = container_of(port, struct asc_port, port);

	/* Set RBE (Receive Buffer Full Interrupt Enable) bit in INTEN */
	if (! (ascport->inten & ASC_INTEN_RBE)) {
		ascport->inten |= ASC_INTEN_RBE;
		asc_out(port, INTEN, ascport->inten);
	}
}

/*----------------------------------------------------------------------*/

/*
 * UART Functions
 */

static unsigned int asc_tx_empty(struct uart_port *port)
{
	unsigned long status;

	status = asc_in(port, STA);
	if (status & ASC_STA_TE)
		return TIOCSER_TEMT;
	return 0;
}

static void asc_set_mctrl(struct uart_port *port, unsigned int mctrl)
{
	/* This routine is used for seting signals of: DTR, DCD, CTS/RTS
	 * We use ASC's hardware for CTS/RTS, so don't need any for that.
	 * Some boards have DTR and DCD implemented using PIO pins,
	 * code to do this should be hooked in here.
	 */
}

static unsigned int asc_get_mctrl(struct uart_port *port)
{
	/* This routine is used for geting signals of: DTR, DCD, DSR, RI,
	   and CTS/RTS */
	return TIOCM_CAR | TIOCM_DSR | TIOCM_CTS;
}

/*
 * There are probably characters waiting to be transmitted.
 * Start doing so.
 * The port lock is held and interrupts are disabled.
 */
static void asc_start_tx(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;

	if (!uart_circ_empty(xmit))
		asc_enable_tx_interrupts(port);
}

/*
 * Transmit stop - interrupts disabled on entry
 */
static void asc_stop_tx(struct uart_port *port)
{
	asc_disable_tx_interrupts(port);
}

/*
 * Receive stop - interrupts still enabled on entry
 */
static void asc_stop_rx(struct uart_port *port)
{
	asc_disable_rx_interrupts(port);
}

/*
 * Force modem status interrupts on - no-op for us
 */
static void asc_enable_ms(struct uart_port *port)
{
	/* Nothing here yet .. */
}

/*
 * Handle breaks - ignored by us
 */
static void asc_break_ctl(struct uart_port *port, int break_state)
{
	/* Nothing here yet .. */
}

/*
 * Enable port for reception.
 * port_sem held and interrupts disabled
 */
static int asc_startup(struct uart_port *port)
{
	int ret;

	ret = asc_request_irq(port);
	if (ret)
		return ret;

	asc_transmit_chars(port);
	asc_enable_rx_interrupts(port);

	return 0;
}

static void asc_shutdown(struct uart_port *port)
{
	asc_disable_tx_interrupts(port);
	asc_disable_rx_interrupts(port);
	asc_free_irq(port);
}

static void asc_set_termios(struct uart_port *port, struct ktermios *termios,
			    struct ktermios *old)
{
	struct asc_port *ascport = container_of(port, struct asc_port, port);
	unsigned int baud;

	baud = uart_get_baud_rate(port, termios, old, 0,
				  port->uartclk/16);

	asc_set_termios_cflag(ascport, termios->c_cflag, baud);
}
static const char *asc_type(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);
	return pdev->name;
}

static void asc_release_port(struct uart_port *port)
{
	struct asc_port *ascport = container_of(port, struct asc_port, port);
	struct platform_device *pdev = to_platform_device(port->dev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (res)
		release_mem_region(res->start, resource_size(res));

	if (port->flags & UPF_IOREMAP) {
		iounmap(port->membase);
		port->membase = NULL;
	}

	if (ascport->pad_state) {
		stm_pad_release(ascport->pad_state);
		ascport->pad_state = NULL;
	}
}

static int asc_request_port(struct uart_port *port)
{
	struct asc_port *ascport = container_of(port, struct asc_port, port);

	return asc_remap_port(ascport, 1);
}

/* Called when the port is opened, and UPF_BOOT_AUTOCONF flag is set */
/* Set type field if successful */
static void asc_config_port(struct uart_port *port, int flags)
{
	if ((flags & UART_CONFIG_TYPE) &&
	    (asc_request_port(port) == 0))
		port->type = PORT_ASC;
}

static int
asc_verify_port(struct uart_port *port, struct serial_struct *ser)
{
	/* No user changeable parameters */
	return -EINVAL;
}

/*---------------------------------------------------------------------*/

static struct uart_ops asc_uart_ops = {
	.tx_empty	= asc_tx_empty,
	.set_mctrl	= asc_set_mctrl,
	.get_mctrl	= asc_get_mctrl,
	.start_tx	= asc_start_tx,
	.stop_tx	= asc_stop_tx,
	.stop_rx	= asc_stop_rx,
	.enable_ms	= asc_enable_ms,
	.break_ctl	= asc_break_ctl,
	.startup	= asc_startup,
	.shutdown	= asc_shutdown,
	.set_termios	= asc_set_termios,
	.type		= asc_type,
	.release_port	= asc_release_port,
	.request_port	= asc_request_port,
	.config_port	= asc_config_port,
	.verify_port	= asc_verify_port,
};

static void __devinit asc_init_port(struct asc_port *ascport,
	struct platform_device *pdev,
	struct stm_plat_asc_data *plat_data, int id)
{
	struct uart_port *port = &ascport->port;
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);

	if (!plat_data) {
		dev_err(&pdev->dev, "Platform data not found\n");
		return;
	}

	if (!res) {
		dev_err(&pdev->dev, "Unable to get platform io_memory resource\n");
		return;
	}

	port->iotype	= UPIO_MEM;
	port->flags	= UPF_BOOT_AUTOCONF;
	port->ops	= &asc_uart_ops;
	port->fifosize	= FIFO_SIZE;
	port->line	= id;
	port->dev	= &pdev->dev;

	port->mapbase	= res->start;
	port->irq	= platform_get_irq(pdev, 0);

	spin_lock_init(&port->lock);

	if (plat_data->regs)
		port->membase = plat_data->regs;
	else {
		port->flags	|= UPF_IOREMAP;
		port->membase	= NULL;
	}

	if (plat_data->clk_id)
		ascport->clk = clk_get(&pdev->dev, plat_data->clk_id);
	else
		ascport->clk = clk_get(&pdev->dev, "comms_clk");

	if (IS_ERR(ascport->clk))
		return;
	clk_prepare_enable(ascport->clk);
	WARN_ON(clk_get_rate(ascport->clk) == 0); /* It won't work at all */

	ascport->port.uartclk = clk_get_rate(ascport->clk);
	ascport->pad_config = plat_data->pad_config;
	ascport->hw_flow_control = plat_data->hw_flow_control;
	ascport->txfifo_bug = plat_data->txfifo_bug;
	ascport->force_m1 = plat_data->force_m1;
}

#ifdef CONFIG_OF
void *stm_asc_of_get_pdata(struct platform_device *pdev)
{

	struct stm_plat_asc_data *data;
	struct device_node *np = pdev->dev.of_node;
	data = devm_kzalloc(&pdev->dev, sizeof(*data), GFP_KERNEL);
	if (!data) {
		dev_err(&pdev->dev, "Unable to allocate platform data\n");
		return ERR_PTR(-ENOMEM);
	}

	data->hw_flow_control = of_property_read_bool(np, "st,hw-flow-control");

	data->txfifo_bug = of_property_read_bool(np, "st,txfifo-bug");
	data->force_m1 =  of_property_read_bool(np, "st,force_m1");

	of_property_read_string(np, "st,clk-id", (const char **)&data->clk_id);

	data->pad_config = stm_of_get_pad_config(&pdev->dev);

	return data;
}

struct stm_plat_asc_data *stm_asc_of_get_early_pdata(struct device_node *np,
				struct platform_device **ppdev)
{
	struct stm_plat_asc_data *data;
	if (!of_device_is_available(np))
		return NULL;
	*ppdev = of_device_alloc(np, NULL, NULL);
	if (!*ppdev)
		return NULL;

	data = stm_asc_of_get_pdata(*ppdev);
	if (IS_ERR(data))
		return NULL;
	return data;
}
#else
void *stm_asc_of_get_pdata(struct platform_device *pdev)
{
	return NULL;
}
struct stm_plat_asc_data *stm_asc_of_get_early_pdata(struct device_node *np,
				struct platform_device **ppdev)
{
	return NULL;
}
#endif

static struct uart_driver asc_uart_driver = {
	.owner		= THIS_MODULE,
	.driver_name	= DRIVER_NAME,
	.dev_name	= "ttyAS",
	.major		= ASC_MAJOR,
	.minor		= ASC_MINOR_START,
	.nr		= ASC_MAX_PORTS,
#ifdef CONFIG_SERIAL_STM_ASC_CONSOLE
	.cons		= &asc_console,
#endif
};

#ifdef CONFIG_SERIAL_STM_ASC_CONSOLE
static struct console asc_console = {
	.name		= "ttyAS",
	.device		= uart_console_device,
	.write		= asc_console_write,
	.setup		= asc_console_setup,
	.flags		= CON_PRINTBUFFER,
	.index		= -1,
	.data		= &asc_uart_driver,
};
/*
 * Early console initialization.
 */
static int __init asc_console_init(void)
{
	int id  = 0;
	struct stm_plat_asc_data *data = NULL;
	struct device_node *dn = NULL;
#ifdef CONFIG_OF
	const char *name = NULL;
	if (of_chosen) {
		name = of_get_property(of_chosen, "linux,stdout-path", NULL);
		if (!name)
			return -ENODEV;

		dn = of_find_node_by_path(name);
		if (!dn)
			return -ENODEV;
		data = stm_asc_of_get_early_pdata(dn, &stm_asc_console_device);
		if (!data)
			return -ENODEV;
		id = of_alias_get_id(dn, "ttyAS");
	}
#endif

	if (!stm_asc_console_device)
		return -ENODEV;

	/* If only non-ASC consoles are specified, don't register ourselves */
	if (console_set_on_cmdline &&
		(!strstr(saved_command_line, "console=ttyAS")))
		return -ENODEV;
	if (!dn) {
		data = stm_asc_console_device->dev.platform_data;
		id = stm_asc_console_device->id;
	}
	if (id < 0)
		return -ENODEV;

	add_preferred_console("ttyAS", id, NULL);
	asc_init_port(&asc_ports[id], stm_asc_console_device, data, id);
	register_console(&asc_console);

	return 0;
}
console_initcall(asc_console_init);

/*
 * Late console initialization.
 */
static int __init asc_late_console_init(void)
{
	if (!(asc_console.flags & CON_ENABLED))
		register_console(&asc_console);

	return 0;
}
core_initcall(asc_late_console_init);
#endif

static int __devinit asc_serial_probe(struct platform_device *pdev)
{
	int ret;
	struct asc_port *ascport;
	int irq = platform_get_irq(pdev, 0);
	struct stm_plat_asc_data *plat_data = NULL;
	int id;

	if (pdev->dev.of_node) {
		plat_data = stm_asc_of_get_pdata(pdev);
		id = of_alias_get_id(pdev->dev.of_node, "ttyAS");
	} else {
		plat_data = pdev->dev.platform_data;
		id = pdev->id;
	}

	if (!plat_data || IS_ERR(plat_data)) {
		dev_err(&pdev->dev, "No platform data found\n");
		return -ENODEV;
	}

	if (id < 0) {
		dev_err(&pdev->dev,
			"No ID specified via pdev->id or in DT alias\n");
		return -ENODEV;
	}

	ascport = &asc_ports[id];
	asc_init_port(ascport, pdev, plat_data, id);

	ret = uart_add_one_port(&asc_uart_driver, &ascport->port);
	if (ret == 0) {
		platform_set_drvdata(pdev, ascport);
		/* set can_wakeup*/
		device_set_wakeup_capable(&pdev->dev, 1);
		/* enable the wakeup on console */
		device_set_wakeup_enable(&pdev->dev, 1);
		if (irq > 0)
			enable_irq_wake(irq);
		pm_runtime_set_active(&pdev->dev);
		pm_runtime_enable(&pdev->dev);
		pm_runtime_get(&pdev->dev); /* notify it's working */
	}
	return ret;
}

static int __devexit asc_serial_remove(struct platform_device *pdev)
{
	struct asc_port *ascport = platform_get_drvdata(pdev);
	struct uart_port *port = &ascport->port;

	platform_set_drvdata(pdev, NULL);
	return uart_remove_one_port(&asc_uart_driver, port);
}

#ifdef CONFIG_PM
static int asc_serial_suspend(struct device *dev)
{
	struct asc_port *ascport = dev_get_drvdata(dev);
	struct uart_port *port   = &(ascport->port);
	unsigned long flags;

	local_irq_save(flags);
	mdelay(10);
	ascport->pm_ctrl = asc_in(port, CTL);

	/* disable the FIFO to resume on a first button */
	asc_out(port, CTL, ascport->pm_ctrl & ~ASC_CTL_FIFOENABLE);

	if (device_can_wakeup(dev)) {
		if (!console_suspend_enabled)
			goto ret_asc_suspend;
		ascport->suspended = 1;
		asc_disable_tx_interrupts(port);
		goto ret_asc_suspend;
	}

	ascport->suspended = 1;
	asc_disable_tx_interrupts(port);
	asc_disable_rx_interrupts(port);
	clk_disable_unprepare(ascport->clk);
ret_asc_suspend:
	local_irq_restore(flags);
	return 0;
}


static int asc_serial_resume(struct device *dev)
{
	struct asc_port *ascport = dev_get_drvdata(dev);
	struct uart_port *port   = &(ascport->port);
	unsigned long flags;

	if (!device_can_wakeup(dev))
		clk_prepare_enable(ascport->clk);

	if (!ascport->pm_baud)
		return 0;

	local_irq_save(flags);
	asc_out(port, CTL, ascport->pm_ctrl);
	asc_out(port, TIMEOUT, 20);		/* hardcoded */
	asc_out(port, INTEN, ascport->inten);
	asc_set_baud(ascport, ascport->pm_baud);
	ascport->suspended = 0;
	local_irq_restore(flags);
	return 0;
}

static int asc_serial_freeze(struct device *dev)
{
	struct asc_port *ascport = dev_get_drvdata(dev);
	struct uart_port *port   = &(ascport->port);

	ascport->pm_ctrl = asc_in(port, CTL);

	clk_disable_unprepare(ascport->clk);
	return 0;
}

static int asc_serial_restore(struct device *dev)
{
	struct asc_port *ascport = dev_get_drvdata(dev);
	struct uart_port *port   = &(ascport->port);

	clk_prepare_enable(ascport->clk);

	stm_pad_setup(ascport->pad_state);

	if (!ascport->pm_baud)
		return 0;
	/* program the port but do not enable it */
	asc_out(port, CTL, ascport->pm_ctrl & ~ASC_CTL_RUN);
	asc_out(port, TIMEOUT, 20);		/* hardcoded */
	asc_out(port, INTEN, ascport->inten);
	asc_set_baud(ascport, ascport->pm_baud);
	/* reset fifo rx & tx */
	asc_out(port, TXRESET, 1);
	asc_out(port, RXRESET, 1);
	/* write final value and enable port */
	asc_out(port, CTL, ascport->pm_ctrl);
	return 0;
}

static struct dev_pm_ops asc_serial_pm_ops = {
	.suspend = asc_serial_suspend,  /* on standby/memstandby */
	.resume = asc_serial_resume,    /* resume from standby/memstandby */
	.freeze = asc_serial_freeze,	/* hibernation */
	.restore = asc_serial_restore,	/* resume from hibernation */
	.runtime_suspend = asc_serial_suspend,
	.runtime_resume = asc_serial_resume,
};
#else
static struct dev_pm_ops asc_serial_pm_ops;
#endif

#ifdef CONFIG_OF
static struct of_device_id stm_asc_match[] = {
	{
		.compatible = "st,asc",
	},
	{},
};

MODULE_DEVICE_TABLE(of, stm_asc_match);
#endif

static struct platform_driver asc_serial_driver = {
	.probe		= asc_serial_probe,
	.remove		= __devexit_p(asc_serial_remove),
	.driver	= {
		.name	= DRIVER_NAME,
		.pm	= &asc_serial_pm_ops,
		.owner	= THIS_MODULE,
		.of_match_table = of_match_ptr(stm_asc_match),
	},
};

static int __init asc_init(void)
{
	int ret;
	static char banner[] __initdata =
		KERN_INFO "STMicroelectronics ASC driver initialized\n";

	printk(banner);

	ret = uart_register_driver(&asc_uart_driver);
	if (ret)
		return ret;

	ret = platform_driver_register(&asc_serial_driver);
	if (ret)
		uart_unregister_driver(&asc_uart_driver);

	return ret;
}

static void __exit asc_exit(void)
{
	platform_driver_unregister(&asc_serial_driver);
	uart_unregister_driver(&asc_uart_driver);
}

module_init(asc_init);
module_exit(asc_exit);

MODULE_AUTHOR("Stuart Menefy <stuart.menefy@st.com>");
MODULE_DESCRIPTION("STMicroelectronics ASC serial port driver");
MODULE_LICENSE("GPL");

/*----------------------------------------------------------------------*/

/* This sections contains code to support the use of the ASC as a
 * generic serial port.
 */

static int asc_remap_port(struct asc_port *ascport, int req)
{
	struct uart_port *port = &ascport->port;
	struct platform_device *pdev = to_platform_device(port->dev);
	struct resource *res = platform_get_resource(pdev, IORESOURCE_MEM, 0);
	int size;

	if (!res)
		return -ENODEV;

	size = resource_size(res);

	if (!ascport->pad_state) {
		/* Can't use dev_name() here as we can be called early */
		ascport->pad_state = stm_pad_claim(ascport->pad_config,
				"stasc");
		if (!ascport->pad_state)
			return -EBUSY;
	}

	if (req && !request_mem_region(port->mapbase, size, pdev->name))
		return -EBUSY;

	/* We have already been remapped for the console */
	if (port->membase)
		return 0;

	if (port->flags & UPF_IOREMAP) {
		port->membase = ioremap(port->mapbase, size);
		if (port->membase == NULL) {
			release_mem_region(port->mapbase, size);
			return -ENOMEM;
		}
	}

	return 0;
}

static int asc_set_baud(struct asc_port *ascport, int baud)
{
	struct uart_port *port = &ascport->port;
	unsigned long rate = clk_get_rate(ascport->clk);
	unsigned int t;

	if ((baud < 19200) && !ascport->force_m1) {
		t = BAUDRATE_VAL_M0(baud, rate);
		asc_out(port, BAUDRATE, t);
		return 0;
	} else {
		t = BAUDRATE_VAL_M1(baud, rate);
		asc_out(port, BAUDRATE, t);
		return ASC_CTL_BAUDMODE;
	}
}

void asc_set_termios_cflag(struct asc_port *ascport, int cflag, int baud)
{
	struct uart_port *port = &ascport->port;
	unsigned int ctrl_val;
	unsigned long flags;

	spin_lock_irqsave(&port->lock, flags);

	/* read control register */
	ctrl_val = asc_in(port, CTL);

	/* stop serial port and reset value */
	asc_out(port, CTL, (ctrl_val & ~ASC_CTL_RUN));
	ctrl_val = ASC_CTL_RXENABLE | ASC_CTL_FIFOENABLE;

	/* reset fifo rx & tx */
	asc_out(port, TXRESET, 1);
	asc_out(port, RXRESET, 1);

	/* set character length */
	if ((cflag & CSIZE) == CS7)
		ctrl_val |= ASC_CTL_MODE_7BIT_PAR;
	else {
		if (cflag & PARENB)
			ctrl_val |= ASC_CTL_MODE_8BIT_PAR;
		else
			ctrl_val |= ASC_CTL_MODE_8BIT;
	}

	ascport->check_parity = (cflag & PARENB) ? 1 : 0;

	/* set stop bit */
	if (cflag & CSTOPB)
		ctrl_val |= ASC_CTL_STOP_2BIT;
	else
		ctrl_val |= ASC_CTL_STOP_1BIT;

	/* odd parity */
	if (cflag & PARODD)
		ctrl_val |= ASC_CTL_PARITYODD;

	/* hardware flow control */
	if ((cflag & CRTSCTS) && ascport->hw_flow_control)
		ctrl_val |= ASC_CTL_CTSENABLE;

	/* set speed and baud generator mode */
#ifdef CONFIG_PM
	ascport->pm_baud = baud;	/* save the latest baudrate request */
#endif
	ctrl_val |= asc_set_baud(ascport, baud);
	uart_update_timeout(port, cflag, baud);

	/* Undocumented feature: use max possible baud */
	if (cflag & 0020000)
		asc_out(port, BAUDRATE, 0x0000ffff);

	/* Undocumented feature: use local loopback */
	if (cflag & 0100000)
		ctrl_val |= ASC_CTL_LOOPBACK;
	else
		ctrl_val &= ~ASC_CTL_LOOPBACK;

	/* Set the timeout */
	asc_out(port, TIMEOUT, 20);

	/* write final value and enable port */
	asc_out(port, CTL, (ctrl_val | ASC_CTL_RUN));

	spin_unlock_irqrestore(&port->lock, flags);
}


static inline unsigned asc_hw_txroom(struct uart_port *port)
{
	unsigned long status;
	struct asc_port *ascport = container_of(port, struct asc_port, port);

	status = asc_in(port, STA);

	if (ascport->txfifo_bug) {
		if (status & ASC_STA_THE)
			return (FIFO_SIZE / 2) - 1;
	} else {
		if (status & ASC_STA_THE)
			return FIFO_SIZE / 2;
		else if (!(status & ASC_STA_TF))
			return 1;
	}

	return 0;
}

/*
 * Start transmitting chars.
 * This is called from both interrupt and task level.
 * Either way interrupts are disabled.
 */
static void asc_transmit_chars(struct uart_port *port)
{
	struct circ_buf *xmit = &port->state->xmit;
	int txroom;
	unsigned char c;

	txroom = asc_hw_txroom(port);

	if ((txroom != 0) && port->x_char) {
		c = port->x_char;
		port->x_char = 0;
		asc_out(port, TXBUF, c);
		port->icount.tx++;
		txroom = asc_hw_txroom(port);
	}

	if (uart_tx_stopped(port)) {
		/*
		 * We should try and stop the hardware here, but I
		 * don't think the ASC has any way to do that.
		 */
		asc_disable_tx_interrupts(port);
		return;
	}

	if (uart_circ_empty(xmit)) {
		asc_disable_tx_interrupts(port);
		return;
	}

	if (txroom == 0)
		return;

	do {
		c = xmit->buf[xmit->tail];
		xmit->tail = (xmit->tail + 1) & (UART_XMIT_SIZE - 1);
		asc_out(port, TXBUF, c);
		port->icount.tx++;
		txroom--;
	} while ((txroom > 0) && (!uart_circ_empty(xmit)));

	if (uart_circ_chars_pending(xmit) < WAKEUP_CHARS)
		uart_write_wakeup(port);

	if (uart_circ_empty(xmit))
		asc_disable_tx_interrupts(port);
}

static inline void asc_receive_chars(struct uart_port *port)
{
	int count;
	struct asc_port *ascport = container_of(port, struct asc_port, port);
	struct tty_struct *tty = port->state->port.tty;
	int copied = 0;
	unsigned long status;
	unsigned long c = 0;
	char flag;
	int overrun;

	while (1) {
		status = asc_in(port, STA);
		if (status & ASC_STA_RHF) {
			count = FIFO_SIZE / 2;
		} else if (status & ASC_STA_RBF) {
			count = 1;
		} else {
			break;
		}

		/* Check for overrun before reading any data from the
		 * RX FIFO, as this clears the overflow error condition. */
		overrun = status & ASC_STA_OE;

		for (; count != 0; count--) {
			c = asc_in(port, RXBUF);
			flag = TTY_NORMAL;
			port->icount.rx++;

			if (unlikely(c & ASC_RXBUF_FE)) {
				if (c == ASC_RXBUF_FE) {
					port->icount.brk++;
					if (uart_handle_break(port))
						continue;
					flag = TTY_BREAK;
				} else {
					port->icount.frame++;
					flag = TTY_FRAME;
				}
			} else if (ascport->check_parity &&
				   unlikely(c & ASC_RXBUF_PE)) {
				port->icount.parity++;
				flag = TTY_PARITY;
			}

			if (uart_handle_sysrq_char(port, c))
				continue;
			tty_insert_flip_char(tty, c & 0xff, flag);
		}
		if (overrun) {
			port->icount.overrun++;
			tty_insert_flip_char(tty, 0, TTY_OVERRUN);
		}

		copied = 1;
	}

	if (copied) {
		/* Tell the rest of the system the news. New characters! */
		spin_unlock(&port->lock);
		tty_flip_buffer_push(tty);
		spin_lock(&port->lock);
	}
}

static irqreturn_t asc_interrupt(int irq, void *ptr)
{
	struct uart_port *port = ptr;
	struct asc_port *ascport = container_of(port, struct asc_port, port);
	unsigned long status;

	spin_lock(&port->lock);

	status = asc_in(port, STA);

	if (status & ASC_STA_RBF) {
		/* Receive FIFO not empty */
		asc_receive_chars(port);
	}

	if ((status & ASC_STA_THE) &&
	    (ascport->inten & ASC_INTEN_THE)) {
		/* Transmitter FIFO at least half empty */
		asc_transmit_chars(port);
	}

	spin_unlock(&port->lock);

	return IRQ_HANDLED;
}

static int asc_request_irq(struct uart_port *port)
{
	struct platform_device *pdev = to_platform_device(port->dev);

	if (request_irq(port->irq, asc_interrupt, 0,
			pdev->name, port)) {
		printk(KERN_ERR "stasc: cannot allocate irq.\n");
		return -ENODEV;
	}
	return 0;
}

static void asc_free_irq(struct uart_port *port)
{
	free_irq(port->irq, port);
}

/*----------------------------------------------------------------------*/

#ifdef CONFIG_SERIAL_STM_ASC_CONSOLE
static int asc_txfifo_is_full(struct asc_port *ascport, unsigned long status)
{
	if (ascport->txfifo_bug)
		return !(status & ASC_STA_THE);

	return status & ASC_STA_TF;
}

static void asc_console_putchar(struct uart_port *port, int ch)
{
	struct asc_port *ascport = container_of(port, struct asc_port, port);

	unsigned int timeout;
	unsigned long status;

	/* Wait for upto 1 second in case flow control is stopping us. */
	for (timeout = 1000000; timeout; timeout--) {
		status = asc_in(port, STA);
		if (!asc_txfifo_is_full(ascport, status))
			break;
		udelay(1);
	}

	asc_out(port, TXBUF, ch);
}

/*
 *  Print a string to the serial port trying not to disturb
 *  any possible real use of the port...
 */

static void asc_console_write(struct console *co, const char *s, unsigned count)
{
	struct uart_port *port = &asc_ports[co->index].port;
	struct asc_port *ascport = container_of(port, struct asc_port, port);
	unsigned long status;
	int locked = 1;

	if (ascport->suspended)
		return;

	if (port->sysrq) {
		/* asc_interrupt has already claimed the lock */
		locked = 0;
	} else if (oops_in_progress) {
		locked = spin_trylock(&port->lock);
	} else
		spin_lock(&port->lock);

	/*
	 * Disable interrupts so we don't get the IRQ line bouncing
	 * up and down while interrupts are disabled.
	 */
	asc_out(port, INTEN, 0);
	(void)asc_in(port, INTEN);	/* Defeat write posting */

	uart_console_write(port, s, count, asc_console_putchar);

	do {
		status = asc_in(port, STA);
	} while (!(status & ASC_STA_TE));

	asc_out(port, INTEN, ascport->inten);

	if (locked)
		spin_unlock(&port->lock);
}

/*----------------------------------------------------------------------*/

/*
 *  Setup initial baud/bits/parity. We do two things here:
 *	- construct a cflag setting for the first rs_open()
 *	- initialize the serial port
 *  Return non-zero if we didn't find a serial port.
 */

static int __init asc_console_setup(struct console *co, char *options)
{
	struct asc_port *ascport;
	int     baud = 9600;
	int     bits = 8;
	int     parity = 'n';
	int     flow = 'n';
	int ret;

	if (co->index >= ASC_MAX_PORTS)
		co->index = 0;

	ascport = &asc_ports[co->index];
	if ((ascport->port.mapbase == 0))
		return -ENODEV;

	ret = asc_remap_port(ascport, 0);
	if (ret != 0)
		return ret;

	if (options)
		uart_parse_options(options, &baud, &parity, &bits, &flow);

	return uart_set_options(&ascport->port, co, baud, parity, bits, flow);
}
#endif /* CONFIG_SERIAL_STM_ASC_CONSOLE */
