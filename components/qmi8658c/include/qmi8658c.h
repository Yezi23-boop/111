#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C"
{
#endif

    /**
     * @brief QMI8658C 芯片识别结果。
     *
     * 第一阶段只用该结构证明板级 I2C 通信与 `WHO_AM_I` 成立。
     * `revision_id` 只用于日志和排障，不作为硬件版本的固定断言。
     */
    typedef struct
    {
        bool present;        /**< true 表示 `0x6B` 地址应答且 `WHO_AM_I` 读取成功。 */
        uint8_t who_am_i;    /**< `WHO_AM_I` 原始值，期望为 `0x05`。 */
        uint8_t revision_id; /**< `REVISION_ID` 原始值，手册版本间可能存在差异。 */
    } qmi8658c_identity_t;

    /**
     * @brief QMI8658C 总线挂载配置。
     *
     * 地址由板级 `SA0` 绑法决定，因此允许上层在初始化前传入当前板地址。
     */
    typedef struct
    {
        uint8_t i2c_addr_7bit; /**< 7-bit I2C 地址，当前板默认为 `0x6B`。 */
    } qmi8658c_bus_config_t;

    /**
     * @brief QMI8658C 原始温度、加速度和陀螺仪样本。
     *
     * 字段保持芯片寄存器的二补码原始单位，便于第一阶段先验证数据变化。
     * 物理量换算、姿态语义和滤波应由后续 service 或算法层负责。
     */
    typedef struct
    {
        int16_t temperature_raw; /**< 温度原始值，来自 `TEMP_L/TEMP_H`。 */
        int16_t accel_x;         /**< X 轴加速度原始值，来自 `AX_L/AX_H`。 */
        int16_t accel_y;         /**< Y 轴加速度原始值，来自 `AY_L/AY_H`。 */
        int16_t accel_z;         /**< Z 轴加速度原始值，来自 `AZ_L/AZ_H`。 */
        int16_t gyro_x;          /**< X 轴角速度原始值，来自 `GX_L/GX_H`。 */
        int16_t gyro_y;          /**< Y 轴角速度原始值，来自 `GY_L/GY_H`。 */
        int16_t gyro_z;          /**< Z 轴角速度原始值，来自 `GZ_L/GZ_H`。 */
    } qmi8658c_raw_sample_t;

    /**
     * @brief QMI8658C 第一阶段原始数据模式配置。
     *
     * `accel_fs/accel_odr/gyro_fs/gyro_odr` 直接对应 CTRL2/CTRL3 的字段值，
     * 调用方必须传入手册允许的编码。第一版不启用 FIFO、AttitudeEngine 或磁力计。
     */
    typedef struct
    {
        uint8_t accel_fs;    /**< CTRL2 bit[6:4]，加速度 full-scale 编码。 */
        uint8_t accel_odr;   /**< CTRL2 bit[3:0]，加速度 ODR 编码。 */
        uint8_t gyro_fs;     /**< CTRL3 bit[6:4]，陀螺仪 full-scale 编码。 */
        uint8_t gyro_odr;    /**< CTRL3 bit[3:0]，陀螺仪 ODR 编码。 */
        bool accel_enable;   /**< true 时在 CTRL7 置位 aEN。 */
        bool gyro_enable;    /**< true 时在 CTRL7 置位 gEN。 */
    } qmi8658c_config_t;

    /**
     * @brief QMI8658C Wake on Motion 事件状态。
     */
    typedef struct
    {
        uint8_t raw_status1; /**< `STATUS1` 原始值；读取会清除 WoM/CmdDone 锁存位。 */
        bool wake_on_motion; /**< true 表示 `STATUS1 bit2` 记录了 WoM 事件。 */
        bool cmd_done;       /**< true 表示 `STATUS1 bit0` 记录了 CTRL9 命令完成。 */
    } qmi8658c_status1_t;

    /**
     * @brief QMI8658C 中断与 CTRL9 完成状态。
     *
     * 当前驱动使用 `CTRL8.bit7=1` 的 STATUSINT CTRL9 握手模式；在
     * `CTRL7.syncSmpl=0` 时，`STATUSINT bit1/bit0` 同时镜像 INT1/INT2 电平。
     */
    typedef struct
    {
        uint8_t raw_statusint; /**< `STATUSINT` 原始值，读取不会清除 WoM 事件。 */
        bool ctrl9_done;       /**< true 表示 `STATUSINT bit7` 的 CTRL9 命令完成位已置位。 */
        bool int1_high;        /**< true 表示 `STATUSINT bit1` 镜像到 INT1 高电平。 */
        bool int2_high;        /**< true 表示 `STATUSINT bit0` 镜像到 INT2 高电平。 */
    } qmi8658c_statusint_t;

    /**
     * @brief Wake on Motion 使用的中断线和初始电平。
     *
     * 手册 Table 33 将中断选择编码在 `CAL1_H bit[7:6]`：
     * `00/10` 选择 INT1，`01/11` 选择 INT2；高位同时决定初始电平。
     */
    typedef enum
    {
        QMI8658C_WOM_INTERRUPT_INT1_INITIAL_LOW = 0,  /**< INT1 初始低电平，事件时翻转。 */
        QMI8658C_WOM_INTERRUPT_INT1_INITIAL_HIGH = 2, /**< INT1 初始高电平，事件时翻转。 */
        QMI8658C_WOM_INTERRUPT_INT2_INITIAL_LOW = 1,  /**< INT2 初始低电平，事件时翻转。 */
        QMI8658C_WOM_INTERRUPT_INT2_INITIAL_HIGH = 3, /**< INT2 初始高电平，事件时翻转。 */
    } qmi8658c_wom_interrupt_t;

    /**
     * @brief QMI8658C Wake on Motion 配置。
     *
     * WoM 阈值以 `mg` 为单位，手册定义为 `1 mg/LSB`，`0` 表示关闭 WoM。
     * Blanking 以加速度计样本数为单位，用于过滤启动瞬态。
     */
    typedef struct
    {
        uint8_t threshold_mg;              /**< WoM 运动阈值，1 mg/LSB；0 表示关闭。 */
        uint8_t blanking_samples;          /**< 中断屏蔽样本数，仅低 6 bit 有效。 */
        uint8_t accel_fs;                  /**< CTRL2 bit[6:4]，加速度 full-scale 编码。 */
        uint8_t accel_odr;                 /**< CTRL2 bit[3:0]，加速度 ODR 编码。 */
        qmi8658c_wom_interrupt_t interrupt; /**< WoM 事件输出到 INT1 或 INT2 的方式。 */
    } qmi8658c_wom_config_t;

    /**
     * @brief 初始化 QMI8658C 驱动并绑定共享 I2C 设备句柄。
     *
     * 该函数复用 `i2c_manager` 已定义的 `GPIO14/GPIO15` 共享总线，
     * 不会创建新的 I2C bus，也不会开始连续采样。
     *
     * @return `ESP_OK` 表示成功或之前已初始化；其他错误表示共享 I2C 不可用或设备句柄创建失败。
     *
     * @note 仅允许在任务上下文调用；内部 I2C 初始化和设备挂载可能阻塞。
     */
    esp_err_t qmi8658c_init(void);

    /**
     * @brief 按板级地址初始化 QMI8658C 驱动。
     *
     * 若驱动已经初始化，传入相同地址会直接返回成功；传入不同地址会返回
     * `ESP_ERR_INVALID_STATE`，避免运行中切换 I2C 设备句柄。
     *
     * @param[in] config 总线挂载配置，不能为空，`i2c_addr_7bit` 必须为 7-bit 地址。
     * @return `ESP_OK` 表示初始化成功或已按相同地址初始化。
     */
    esp_err_t qmi8658c_init_with_bus_config(
        const qmi8658c_bus_config_t *config);

    /**
     * @brief 探测 QMI8658C 并读取芯片识别寄存器。
     *
     * 该接口先探测当前板级地址 `0x6B`，再读取 `WHO_AM_I` 与 `REVISION_ID`。
     * 若地址无应答，函数返回 `ESP_OK` 且 `identity->present=false`，便于板级日志继续运行。
     *
     * @param[out] identity 输出芯片识别结果，不能为空。
     * @return `ESP_OK` 表示探测流程完成；其他错误表示共享 I2C 或寄存器读取出现异常。
     *
     * @note 该接口只做一次性探测，不配置采样模式，不应在 UI 高频路径调用。
     */
    esp_err_t qmi8658c_probe(qmi8658c_identity_t *identity);

    /**
     * @brief 配置 QMI8658C 的原始加速度和陀螺仪输出模式。
     *
     * 第一阶段只写 CTRL2、CTRL3 和 CTRL7，保持 FIFO、AttitudeEngine、磁力计和中断配置关闭。
     *
     * @param[in] config 原始数据模式配置，不能为空。
     * @return `ESP_OK` 表示配置成功；`ESP_ERR_INVALID_ARG` 表示字段编码越界；
     *         其他错误表示 I2C 写入失败。
     *
     * @note 该接口会改变芯片工作模式，但不会创建采样任务。
     */
    esp_err_t qmi8658c_configure(const qmi8658c_config_t *config);

    /**
     * @brief 读取一次温度、三轴加速度和三轴陀螺仪原始数据。
     *
     * 读取窗口从 `TEMP_L` 连续到 `GZ_H`，避免多次分散 I2C 事务造成样本不一致。
     *
     * @param[out] sample 输出原始样本，不能为空。
     * @return `ESP_OK` 表示读取成功；其他错误表示 I2C 访问失败。
     *
     * @note 返回值不做物理单位换算；调用方应在 service/算法层解释量程。
     */
    esp_err_t qmi8658c_read_raw(qmi8658c_raw_sample_t *sample);

    /**
     * @brief 读取一次 `STATUS1`，并解析 CmdDone / Wake on Motion 位。
     *
     * @param[out] status 输出状态，不能为空。
     * @return `ESP_OK` 表示读取成功；其他错误表示 I2C 访问失败。
     *
     * @note 手册说明读取 `STATUS1` 会清除 `CmdDone` 和 WoM 锁存状态，并复位所选中断线。
     */
    esp_err_t qmi8658c_read_status1(qmi8658c_status1_t *status);

    /**
     * @brief 读取 `STATUSINT`，用于对照 CTRL9 完成状态和 INT1/INT2 实际电平。
     *
     * @param[out] status 输出状态，不能为空。
     * @return `ESP_OK` 表示读取成功；其他错误表示 I2C 访问失败。
     *
     * @note 该读取不会清除 WoM；只有在 `CTRL7.syncSmpl=0` 时 bit1/bit0 才是中断电平镜像。
     */
    esp_err_t qmi8658c_read_statusint(qmi8658c_statusint_t *status);

    /**
     * @brief 配置并启用 QMI8658C 内部 Wake on Motion。
     *
     * 配置顺序按手册 Figure 10：先关闭 sensor，写 CTRL2，写 CAL1_L/H，
     * 执行 CTRL9 `WRITE_WOM_SETTING`，最后只打开加速度计。
     *
     * @param[in] config WoM 配置，不能为空，`threshold_mg` 不能为 0。
     * @return `ESP_OK` 表示配置完成；其他错误表示参数或 I2C/CTRL9 握手失败。
     */
    esp_err_t qmi8658c_configure_wake_on_motion(
        const qmi8658c_wom_config_t *config);

    /**
     * @brief 关闭 QMI8658C 内部 Wake on Motion。
     *
     * @return `ESP_OK` 表示关闭命令完成。
     */
    esp_err_t qmi8658c_disable_wake_on_motion(void);

#ifdef __cplusplus
}
#endif
