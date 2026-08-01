#ifndef RUNTIME_RESOURCE_GATE_BOARD_TEST_H
#define RUNTIME_RESOURCE_GATE_BOARD_TEST_H

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief 启动一次性 Runtime Coordinator 模拟 participant 板测。
     *
     * 该测试只在 `CONFIG_RUNTIME_RESOURCE_GATE_BOARD_TEST=y` 时创建后台任务；
     * 默认配置下本函数为空操作。测试覆盖请求覆盖、陈旧 ACK、拒绝、超时、
     * grant 失败与 rollback fail-closed 状态。
     *
     * @return `ESP_OK` 表示未启用、已启动或本次启动成功。
     */
    esp_err_t runtime_resource_gate_board_test_start(void);

#ifdef __cplusplus
}
#endif

#endif // RUNTIME_RESOURCE_GATE_BOARD_TEST_H
