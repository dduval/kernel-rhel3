#include <linux/kernel.h>
#include <linux/module.h>
#include <linux/init.h>
#include <linux/string.h>

#include "qla_opts.h"

MODULE_DESCRIPTION("QLogic Persistent Binding Data Module");
MODULE_AUTHOR("QLogic Corporation");
#if defined(MODULE_LICENSE)
	 MODULE_LICENSE("GPL");
#endif

static char *qla_persistent_str = NULL ;
CONFIG_BEGIN("qla2200_conf")
CONFIG_ITEM("OPTIONS", "")
CONFIG_END

static int conf_init(void)
{

	QLOPTS_CONFIGURE(qla_persistent_str);
	inter_module_register("qla22XX_conf",
		THIS_MODULE,qla_persistent_str);
	return 0;

}

static void conf_exit (void)
{
	inter_module_unregister("qla22XX_conf");

}

module_init(conf_init);
module_exit(conf_exit);


