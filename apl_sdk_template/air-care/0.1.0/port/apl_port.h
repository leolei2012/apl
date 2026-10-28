#ifndef APL_PORT_H
#define APL_PORT_H

#include "apl.h"

/** 用户移植文件。实现硬件访问后调用；self 由应用持有。 */
void apl_port_init(struct apl *self);

#endif
