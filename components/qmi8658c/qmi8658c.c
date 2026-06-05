#include "qmi8658c.h"

#include <stddef.h>
#include <string.h>

#include "esp_check.h"
#include "esp_idf_version.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_manager.h"
#include "qmi8658c_regs.h"

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
#include "driver/i2c_master.h"
#else
#include "driver/i2c.h"
#endif

static const char *TAG = "qmi8658c";

static bool s_ready = false; // 组件是否已挂到共享 I2C；不表示芯片身份已验证。
static uint8_t s_i2c_addr_7bit =
    QMI8658C_I2C_ADDR_7BIT; // 板级 SA0 绑法决定；默认兼容当前开发板。

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
static i2c_master_dev_handle_t s_dev_handle = NULL; // 共享 master bus 下的 QMI8658C 设备句柄。
#endif

static esp_err_t qmi8658c_read_bytes(uint8_t reg, uint8_t *data, size_t len);
static esp_err_t qmi8658c_write_bytes(uint8_t reg, const uint8_t *data,
                                      size_t len);
static esp_err_t qmi8658c_write_byte(uint8_t reg, uint8_t value);
static esp_err_t qmi8658c_read_statusint_raw(uint8_t *statusint);
static esp_err_t qmi8658c_enable_statusint_ctrl9_handshake(void);
static esp_err_t qmi8658c_execute_ctrl9(uint8_t command);
static bool qmi8658c_accel_odr_code_valid(uint8_t odr);
static bool qmi8658c_gyro_odr_code_valid(uint8_t odr);
static int16_t qmi8658c_decode_i16(const uint8_t *data);

esp_err_t qmi8658c_init(void)
{
    const qmi8658c_bus_config_t config = {
        .i2c_addr_7bit = QMI8658C_I2C_ADDR_7BIT,
    };
    return qmi8658c_init_with_bus_config(&config);
}

esp_err_t qmi8658c_init_with_bus_config(const qmi8658c_bus_config_t *config)
{
    if (s_ready)
    {
        ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                            "bus config is null");
        ESP_RETURN_ON_FALSE(config->i2c_addr_7bit == s_i2c_addr_7bit,
                            ESP_ERR_INVALID_STATE, TAG,
                            "qmi8658c already initialized with another addr");
        return ESP_OK;
    }
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "bus config is null");
    ESP_RETURN_ON_FALSE(config->i2c_addr_7bit <= 0x7F, ESP_ERR_INVALID_ARG,
                        TAG, "i2c addr is not 7-bit");
    s_i2c_addr_7bit = config->i2c_addr_7bit;

    ESP_RETURN_ON_ERROR(i2c_manager_init(), TAG, "i2c manager init failed");

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    i2c_master_bus_handle_t bus_handle = i2c_manager_get_bus_handle();
    ESP_RETURN_ON_FALSE(bus_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "shared i2c bus is not ready");

    i2c_device_config_t dev_cfg = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = s_i2c_addr_7bit,
        .scl_speed_hz = I2C_MANAGER_FREQ_HZ,
    };

    ESP_RETURN_ON_ERROR(
        i2c_master_bus_add_device(bus_handle, &dev_cfg, &s_dev_handle), TAG,
        "add qmi8658c device failed");
#endif

    s_ready = true;
    return ESP_OK;
}

esp_err_t qmi8658c_probe(qmi8658c_identity_t *identity)
{
    ESP_RETURN_ON_FALSE(identity != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "identity is null");
    memset(identity, 0, sizeof(*identity));

    ESP_RETURN_ON_ERROR(qmi8658c_init(), TAG, "init failed before probe");

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    i2c_master_bus_handle_t bus_handle = i2c_manager_get_bus_handle();
    ESP_RETURN_ON_FALSE(bus_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "shared i2c bus is not ready");

    esp_err_t ret =
        i2c_master_probe(bus_handle, s_i2c_addr_7bit, 50);
    if (ret == ESP_ERR_NOT_FOUND || ret == ESP_FAIL)
    {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "qmi8658c address probe failed");
#else
    uint8_t value = 0;
    esp_err_t ret = qmi8658c_read_bytes(QMI8658C_REG_WHO_AM_I, &value, 1);
    if (ret == ESP_ERR_NOT_FOUND || ret == ESP_FAIL)
    {
        return ESP_OK;
    }
    ESP_RETURN_ON_ERROR(ret, TAG, "qmi8658c who_am_i probe failed");
#endif

    ESP_RETURN_ON_ERROR(qmi8658c_read_bytes(QMI8658C_REG_WHO_AM_I,
                                            &identity->who_am_i, 1),
                        TAG, "read who_am_i failed");
    ESP_RETURN_ON_ERROR(qmi8658c_read_bytes(QMI8658C_REG_REVISION_ID,
                                            &identity->revision_id, 1),
                        TAG, "read revision_id failed");

    identity->present = identity->who_am_i == QMI8658C_WHO_AM_I_EXPECTED;
    if (!identity->present)
    {
        ESP_LOGW(TAG, "unexpected WHO_AM_I=0x%02X revision=0x%02X",
                 identity->who_am_i, identity->revision_id);
    }
    return ESP_OK;
}

esp_err_t qmi8658c_configure(const qmi8658c_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "config is null");
    ESP_RETURN_ON_FALSE(
        config->accel_fs <= QMI8658C_CTRL2_ACCEL_FS_MASK,
        ESP_ERR_INVALID_ARG, TAG, "accel full-scale code out of range");
    ESP_RETURN_ON_FALSE(qmi8658c_accel_odr_code_valid(config->accel_odr),
                        ESP_ERR_INVALID_ARG, TAG,
                        "accel odr code is not supported by datasheet");
    ESP_RETURN_ON_FALSE(config->gyro_fs <= QMI8658C_CTRL3_GYRO_FS_MASK,
                        ESP_ERR_INVALID_ARG, TAG,
                        "gyro full-scale code out of range");
    ESP_RETURN_ON_FALSE(config->gyro_odr <= QMI8658C_CTRL3_GYRO_ODR_MASK,
                        ESP_ERR_INVALID_ARG, TAG,
                        "gyro odr code out of range");
    ESP_RETURN_ON_FALSE(qmi8658c_gyro_odr_code_valid(config->gyro_odr),
                        ESP_ERR_INVALID_ARG, TAG,
                        "gyro odr code is not supported by datasheet");
    ESP_RETURN_ON_ERROR(qmi8658c_init(), TAG, "init failed before configure");

    /*
     * 先打开地址自增，再配置量程/ODR，最后启用 sensor。
     * 原因：read_raw 依赖从 TEMP_L 开始的连续读取窗口；若未打开自增，
     * 多字节事务会反复读取同一寄存器，导致样本看似固定。
     */
    ESP_RETURN_ON_ERROR(
        qmi8658c_write_byte(QMI8658C_REG_CTRL1,
                            QMI8658C_CTRL1_ADDR_AUTO_INCREMENT),
        TAG, "write ctrl1 failed");

    uint8_t ctrl2 =
        (uint8_t)((config->accel_fs << 4) | config->accel_odr);
    uint8_t ctrl3 = (uint8_t)((config->gyro_fs << 4) | config->gyro_odr);
    uint8_t ctrl7 = 0;
    if (config->accel_enable)
    {
        ctrl7 |= QMI8658C_CTRL7_ACCEL_ENABLE;
    }
    if (config->gyro_enable)
    {
        ctrl7 |= QMI8658C_CTRL7_GYRO_ENABLE;
    }

    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CTRL2, ctrl2), TAG,
                        "write ctrl2 failed");
    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CTRL3, ctrl3), TAG,
                        "write ctrl3 failed");
    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CTRL7, ctrl7), TAG,
                        "write ctrl7 failed");
    return ESP_OK;
}

esp_err_t qmi8658c_read_raw(qmi8658c_raw_sample_t *sample)
{
    ESP_RETURN_ON_FALSE(sample != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "sample is null");
    ESP_RETURN_ON_ERROR(qmi8658c_init(), TAG, "init failed before raw read");

    uint8_t raw[14] = {0}; // TEMP(2) + accel(6) + gyro(6)，均为低字节在前。
    ESP_RETURN_ON_ERROR(
        qmi8658c_read_bytes(QMI8658C_REG_TEMP_L, raw, sizeof(raw)), TAG,
        "read raw sample failed");

    qmi8658c_raw_sample_t out = {
        .temperature_raw = qmi8658c_decode_i16(&raw[0]),
        .accel_x = qmi8658c_decode_i16(&raw[2]),
        .accel_y = qmi8658c_decode_i16(&raw[4]),
        .accel_z = qmi8658c_decode_i16(&raw[6]),
        .gyro_x = qmi8658c_decode_i16(&raw[8]),
        .gyro_y = qmi8658c_decode_i16(&raw[10]),
        .gyro_z = qmi8658c_decode_i16(&raw[12]),
    };
    *sample = out;
    return ESP_OK;
}

esp_err_t qmi8658c_read_status1(qmi8658c_status1_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "status is null");
    ESP_RETURN_ON_ERROR(qmi8658c_init(), TAG, "init failed before status1 read");

    uint8_t raw = 0;
    ESP_RETURN_ON_ERROR(qmi8658c_read_bytes(QMI8658C_REG_STATUS1, &raw, 1),
                        TAG, "read status1 failed");

    status->raw_status1 = raw;
    status->wake_on_motion = (raw & QMI8658C_STATUS1_WOM) != 0;
    status->cmd_done = (raw & QMI8658C_STATUS1_CMD_DONE) != 0;
    return ESP_OK;
}

esp_err_t qmi8658c_read_statusint(qmi8658c_statusint_t *status)
{
    ESP_RETURN_ON_FALSE(status != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "statusint is null");
    ESP_RETURN_ON_ERROR(qmi8658c_init(), TAG,
                        "init failed before statusint read");

    uint8_t raw = 0;
    ESP_RETURN_ON_ERROR(qmi8658c_read_statusint_raw(&raw), TAG,
                        "read statusint failed");

    status->raw_statusint = raw;
    status->ctrl9_done = (raw & QMI8658C_STATUSINT_CTRL9_DONE) != 0;
    status->int1_high = (raw & QMI8658C_STATUSINT_INT1_LEVEL) != 0;
    status->int2_high = (raw & QMI8658C_STATUSINT_INT2_LEVEL) != 0;
    return ESP_OK;
}

esp_err_t qmi8658c_configure_wake_on_motion(
    const qmi8658c_wom_config_t *config)
{
    ESP_RETURN_ON_FALSE(config != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "wom config is null");
    ESP_RETURN_ON_FALSE(config->threshold_mg > 0, ESP_ERR_INVALID_ARG, TAG,
                        "wom threshold 0 disables wom");
    ESP_RETURN_ON_FALSE(config->blanking_samples <= 0x3F,
                        ESP_ERR_INVALID_ARG, TAG,
                        "wom blanking samples out of range");
    ESP_RETURN_ON_FALSE(config->accel_fs <= QMI8658C_CTRL2_ACCEL_FS_MASK,
                        ESP_ERR_INVALID_ARG, TAG,
                        "wom accel full-scale code out of range");
    ESP_RETURN_ON_FALSE(qmi8658c_accel_odr_code_valid(config->accel_odr),
                        ESP_ERR_INVALID_ARG, TAG,
                        "wom accel odr code is not supported by datasheet");
    ESP_RETURN_ON_FALSE(config->interrupt <=
                            QMI8658C_WOM_INTERRUPT_INT2_INITIAL_HIGH,
                        ESP_ERR_INVALID_ARG, TAG,
                        "wom interrupt code out of range");
    ESP_RETURN_ON_ERROR(qmi8658c_init(), TAG, "init failed before wom config");

    /*
     * Figure 10 的 WoM 序列要求先关闭 sensor，避免旧采样状态和启动瞬态
     * 影响阈值配置。最后只打开 aEN，保持陀螺仪关闭以降低功耗。
     */
    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CTRL7, 0), TAG,
                        "disable sensors before wom failed");
    ESP_RETURN_ON_ERROR(
        qmi8658c_write_byte(QMI8658C_REG_CTRL1,
                            QMI8658C_CTRL1_ADDR_AUTO_INCREMENT),
        TAG, "write wom ctrl1 failed");

    uint8_t ctrl2 = (uint8_t)((config->accel_fs << 4) | config->accel_odr);
    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CTRL2, ctrl2), TAG,
                        "write wom ctrl2 failed");

    const uint8_t cal1_h =
        (uint8_t)(((uint8_t)config->interrupt << 6) |
                  (config->blanking_samples & 0x3F));
    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CAL1_L,
                                            config->threshold_mg),
                        TAG, "write wom threshold failed");
    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CAL1_H, cal1_h),
                        TAG, "write wom interrupt config failed");
    ESP_RETURN_ON_ERROR(qmi8658c_execute_ctrl9(
                            QMI8658C_CTRL9_CMD_WRITE_WOM_SETTING),
                        TAG, "execute wom ctrl9 failed");
    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CTRL7,
                                            QMI8658C_CTRL7_ACCEL_ENABLE),
                        TAG, "enable accel for wom failed");
    return ESP_OK;
}

esp_err_t qmi8658c_disable_wake_on_motion(void)
{
    ESP_RETURN_ON_ERROR(qmi8658c_init(), TAG, "init failed before wom disable");
    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CTRL7, 0), TAG,
                        "disable sensors before wom off failed");

    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CAL1_L, 0), TAG,
                        "write wom off threshold failed");
    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CAL1_H, 0), TAG,
                        "write wom off interrupt config failed");
    ESP_RETURN_ON_ERROR(qmi8658c_execute_ctrl9(
                            QMI8658C_CTRL9_CMD_WRITE_WOM_SETTING),
                        TAG, "execute wom off ctrl9 failed");
    return ESP_OK;
}

static esp_err_t qmi8658c_read_bytes(uint8_t reg, uint8_t *data, size_t len)
{
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "data is null");
    ESP_RETURN_ON_FALSE(len > 0, ESP_ERR_INVALID_ARG, TAG, "len must be > 0");

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    ESP_RETURN_ON_FALSE(s_dev_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "qmi8658c device not ready");
    return i2c_master_transmit_receive(s_dev_handle, &reg, 1, data, len, 1000);
#else
    i2c_port_t port = i2c_manager_get_port();
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_RETURN_ON_FALSE(cmd != NULL, ESP_ERR_NO_MEM, TAG, "cmd alloc failed");

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_i2c_addr_7bit << 1) | I2C_MASTER_WRITE,
                          true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_i2c_addr_7bit << 1) | I2C_MASTER_READ,
                          true);
    if (len > 1)
    {
        i2c_master_read(cmd, data, len - 1, I2C_MASTER_ACK);
    }
    i2c_master_read_byte(cmd, &data[len - 1], I2C_MASTER_NACK);
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
#endif
}

static esp_err_t qmi8658c_write_byte(uint8_t reg, uint8_t value)
{
    return qmi8658c_write_bytes(reg, &value, 1);
}

static esp_err_t qmi8658c_write_bytes(uint8_t reg, const uint8_t *data,
                                      size_t len)
{
    ESP_RETURN_ON_FALSE(data != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "write data is null");
    ESP_RETURN_ON_FALSE(len > 0, ESP_ERR_INVALID_ARG, TAG,
                        "write len must be > 0");

#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 3, 0)
    ESP_RETURN_ON_FALSE(s_dev_handle != NULL, ESP_ERR_INVALID_STATE, TAG,
                        "qmi8658c device not ready");

    uint8_t buffer[17] = {0};
    ESP_RETURN_ON_FALSE(len < sizeof(buffer), ESP_ERR_INVALID_ARG, TAG,
                        "write len too large");
    buffer[0] = reg;
    memcpy(&buffer[1], data, len);
    return i2c_master_transmit(s_dev_handle, buffer, len + 1, 1000);
#else
    i2c_port_t port = i2c_manager_get_port();
    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    ESP_RETURN_ON_FALSE(cmd != NULL, ESP_ERR_NO_MEM, TAG, "cmd alloc failed");

    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (s_i2c_addr_7bit << 1) | I2C_MASTER_WRITE,
                          true);
    i2c_master_write_byte(cmd, reg, true);
    for (size_t i = 0; i < len; ++i)
    {
        i2c_master_write_byte(cmd, data[i], true);
    }
    i2c_master_stop(cmd);

    esp_err_t ret = i2c_master_cmd_begin(port, cmd, pdMS_TO_TICKS(1000));
    i2c_cmd_link_delete(cmd);
    return ret;
#endif
}

static esp_err_t qmi8658c_execute_ctrl9(uint8_t command)
{
    /*
     * Rev A 默认用 INT1 做 CTRL9 握手。当前 WoM 也需要 INT1，因此先切换到
     * STATUSINT.bit7 握手，避免配置命令本身污染 GPIO21 的 WoM 边沿。
     */
    ESP_RETURN_ON_ERROR(qmi8658c_enable_statusint_ctrl9_handshake(), TAG,
                        "enable statusint ctrl9 handshake failed");
    ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CTRL9, command), TAG,
                        "write ctrl9 failed");

    for (int i = 0; i < 1000; ++i)
    {
        uint8_t statusint = 0;
        ESP_RETURN_ON_ERROR(qmi8658c_read_statusint_raw(&statusint), TAG,
                            "read ctrl9 statusint failed");
        if ((statusint & QMI8658C_STATUSINT_CTRL9_DONE) != 0)
        {
            ESP_RETURN_ON_ERROR(qmi8658c_write_byte(QMI8658C_REG_CTRL9,
                                                    QMI8658C_CTRL9_CMD_ACK),
                                TAG, "ack ctrl9 failed");
            for (int clear_try = 0; clear_try < 1000; ++clear_try)
            {
                ESP_RETURN_ON_ERROR(qmi8658c_read_statusint_raw(&statusint),
                                    TAG,
                                    "read statusint after ctrl9 ack failed");
                if ((statusint & QMI8658C_STATUSINT_CTRL9_DONE) == 0)
                {
                    return ESP_OK;
                }
                vTaskDelay(pdMS_TO_TICKS(1));
            }

            ESP_LOGW(TAG, "ctrl9 command 0x%02x ack clear timeout", command);
            return ESP_ERR_TIMEOUT;
        }
        vTaskDelay(pdMS_TO_TICKS(1));
    }

    ESP_LOGW(TAG, "ctrl9 command 0x%02x timeout", command);
    return ESP_ERR_TIMEOUT;
}

static esp_err_t qmi8658c_enable_statusint_ctrl9_handshake(void)
{
    uint8_t ctrl8 = 0;
    ESP_RETURN_ON_ERROR(qmi8658c_read_bytes(QMI8658C_REG_CTRL8, &ctrl8, 1),
                        TAG, "read ctrl8 failed");
    if ((ctrl8 & QMI8658C_CTRL8_CTRL9_HANDSHAKE_STATUSINT) != 0)
    {
        return ESP_OK;
    }

    ctrl8 |= QMI8658C_CTRL8_CTRL9_HANDSHAKE_STATUSINT;
    return qmi8658c_write_byte(QMI8658C_REG_CTRL8, ctrl8);
}

static esp_err_t qmi8658c_read_statusint_raw(uint8_t *statusint)
{
    ESP_RETURN_ON_FALSE(statusint != NULL, ESP_ERR_INVALID_ARG, TAG,
                        "statusint is null");
    return qmi8658c_read_bytes(QMI8658C_REG_STATUSINT, statusint, 1);
}

static bool qmi8658c_accel_odr_code_valid(uint8_t odr)
{
    /*
     * 手册 CTRL2 表中 accel ODR 0x0-0x8 为 normal mode，
     * 0xC-0xF 为 low-power mode，0x9-0xB 标为 N/A。
     */
    return odr <= 0x08 || (odr >= 0x0C && odr <= 0x0F);
}

static bool qmi8658c_gyro_odr_code_valid(uint8_t odr)
{
    // 手册 CTRL3 表中 gyro ODR 仅 0x0-0x8 有效，0x9-0xF 标为 N/A。
    return odr <= 0x08;
}

static int16_t qmi8658c_decode_i16(const uint8_t *data)
{
    return (int16_t)(((uint16_t)data[1] << 8) | data[0]);
}
