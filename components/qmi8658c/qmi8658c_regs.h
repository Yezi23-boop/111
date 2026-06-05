#pragma once

/*
 * 当前板级原理图将 QMI8658C 标注为 0x6B。地址由 SA0 绑法决定，
 * 若后续硬件版本变化，需要重新用 I2C scan 或原理图确认。
 */
#define QMI8658C_I2C_ADDR_7BIT 0x6B

/* 通用识别寄存器，来自 QMI8658C 手册 Register Map。 */
#define QMI8658C_REG_WHO_AM_I 0x00
#define QMI8658C_WHO_AM_I_EXPECTED 0x05
#define QMI8658C_REG_REVISION_ID 0x01

/* 当前 Rev A 路径只使用基础配置寄存器，不启用 FIFO。 */
#define QMI8658C_REG_CTRL1 0x02
#define QMI8658C_REG_CTRL2 0x03
#define QMI8658C_REG_CTRL3 0x04
#define QMI8658C_REG_CTRL7 0x08
#define QMI8658C_REG_CTRL8 0x09
#define QMI8658C_REG_CTRL9 0x0A

/* CTRL9 协议使用的 CAL buffer；WoM 第一版只写 CAL1_L/H。 */
#define QMI8658C_REG_CAL1_L 0x0B
#define QMI8658C_REG_CAL1_H 0x0C

/* STATUS0 可用于后续判断 aDA/gDA；当前 read_raw 先做直接读取。 */
#define QMI8658C_REG_STATUSINT 0x2D
#define QMI8658C_REG_STATUS0 0x2E
#define QMI8658C_REG_STATUS1 0x2F

/* 连续原始数据窗口：TEMP_L/TEMP_H, AX..AZ, GX..GZ。 */
#define QMI8658C_REG_TEMP_L 0x33
#define QMI8658C_REG_AX_L 0x35
#define QMI8658C_REG_GX_L 0x3B

/* CTRL1 bit6 开启 I2C/SPI 地址自增，便于连续读取原始数据窗口。 */
#define QMI8658C_CTRL1_ADDR_AUTO_INCREMENT (1u << 6)

/* CTRL2/CTRL3 字段掩码；驱动会额外拒绝手册标为 N/A 的 ODR 编码。 */
#define QMI8658C_CTRL2_ACCEL_FS_MASK 0x07
#define QMI8658C_CTRL2_ACCEL_ODR_MASK 0x0F
#define QMI8658C_CTRL3_GYRO_FS_MASK 0x07
#define QMI8658C_CTRL3_GYRO_ODR_MASK 0x0F

/* 当前 Rev A 寄存器图中 CTRL7 只使用加速度计和陀螺仪使能位。 */
#define QMI8658C_CTRL7_ACCEL_ENABLE (1u << 0)
#define QMI8658C_CTRL7_GYRO_ENABLE (1u << 1)

/*
 * 当前板 REVISION_ID=0x7C，对应 QMI8658C Rev A。
 * 置位后 CTRL9 完成只通过 STATUSINT.bit7 握手，不再占用 INT1。
 */
#define QMI8658C_CTRL8_CTRL9_HANDSHAKE_STATUSINT (1u << 7)

/* STATUS1：手册 WoM 章节说明 bit2 表示 WoM，CTRL9 协议使用 bit0 CmdDone。 */
#define QMI8658C_STATUS1_CMD_DONE (1u << 0)
#define QMI8658C_STATUS1_WOM (1u << 2)

/* CTRL9 命令值。 */
#define QMI8658C_CTRL9_CMD_ACK 0x00
#define QMI8658C_CTRL9_CMD_WRITE_WOM_SETTING 0x08
/*
 * Rev A 中 0x0C 是 Tap 配置命令，不是旧 Rev0.6 文档中的 MoD 请求。
 * 保留该常量用于 source test 防止后续再次把它误当作 MoD。
 */
#define QMI8658C_CTRL9_CMD_CONFIGURE_TAP 0x0C

/* Rev A STATUSINT：bit7 为 CTRL9 完成；syncSmpl=0 时 bit1/bit0 镜像 INT1/INT2。 */
#define QMI8658C_STATUSINT_CTRL9_DONE (1u << 7)
#define QMI8658C_STATUSINT_INT1_LEVEL (1u << 1)
#define QMI8658C_STATUSINT_INT2_LEVEL (1u << 0)
