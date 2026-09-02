// SPDX-FileCopyrightText: 2026 Antchain (SHANGHAI) Digital Technology Co., Ltd.
// SPDX-License-Identifier: Apache-2.0

#include "actrust.h"

static actrust_err_t core_error(uint32_t reason)
{
    return ACTRUST_ERR_MAKE(ACTRUST_ERR_MODULE_CORE, reason);
}

int main(void)
{
    if (actrust_set_callback(NULL, NULL) !=
        core_error(ACTRUST_ERR_INVALID_ARG)) {
        return 1;
    }
    if (actrust_init(NULL) != core_error(ACTRUST_ERR_NOT_READY)) {
        return 1;
    }
    if (actrust_connect() != core_error(ACTRUST_ERR_BAD_STATE)) {
        return 1;
    }
    if (actrust_disconnect() != core_error(ACTRUST_ERR_BAD_STATE)) {
        return 1;
    }
    if (actrust_register() != core_error(ACTRUST_ERR_BAD_STATE)) {
        return 1;
    }
    if (actrust_data_publish(NULL, 0u) != core_error(ACTRUST_ERR_INVALID_ARG)) {
        return 1;
    }
    if (actrust_deinit() != core_error(ACTRUST_ERR_BAD_STATE)) {
        return 1;
    }
    return 0;
}
