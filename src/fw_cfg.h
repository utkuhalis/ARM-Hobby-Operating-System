#ifndef HOBBY_OS_FW_CFG_H
#define HOBBY_OS_FW_CFG_H

#include <stdint.h>

int fw_cfg_write_named(const char *name, const void *data, uint32_t size);

#endif
