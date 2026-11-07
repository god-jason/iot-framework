/*
 * ports/simulator/port_sim.c —— QSPI / SDIO / USB / ETH 单元测试。
 * 全部断言均以模拟实现的实际行为为准。
 */
#include "test.h"
#include "port.h"
#include "port_sim.h"

#include <string.h>

#define TEST_SDIO_BLOCK_SIZE    512U
#define TEST_SDIO_BLOCK_MAX     8192U

void ut_port_sim_optional(void)
{
    port_qspi_handle_t qspi;
    port_qspi_config_t qspi_cfg;
    port_qspi_command_t qspi_cmd;
    port_sdio_handle_t sdio;
    port_sdio_config_t sdio_cfg;
    port_usb_handle_t usb;
    port_usb_config_t usb_cfg;
    port_eth_handle_t eth;
    port_eth_config_t eth_cfg;
    uint8_t buffer[TEST_SDIO_BLOCK_SIZE];
    uint8_t readback[TEST_SDIO_BLOCK_SIZE];
    uint8_t frame[16];
    uint8_t rx[64];
    size_t received;
    size_t i;

    tf_begin("optional/QSPI 初始化、命令、收发与内存映射");
    port_sim_reset_state();
    qspi_cfg.clock_frequency = 1000000U;
    qspi_cfg.flash_size = 16U;
    qspi_cfg.clock_mode = 0U;
    qspi_cfg.dual_flash = false;
    TF_CHECK_INT(port_qspi_init(NULL, &qspi_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_qspi_init(&qspi, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_qspi_init(&qspi, &qspi_cfg), PORT_OK);
    TF_CHECK(qspi.base.initialized);
    TF_CHECK_INT(port_qspi_command(&qspi, NULL), PORT_INVALID_PARAM);
    memset(&qspi_cmd, 0, sizeof(qspi_cmd));
    qspi_cmd.instruction = 0x03U;
    qspi_cmd.address_size = 24U;
    qspi_cmd.data_lines = 4U;
    qspi_cmd.address_lines = 1U;
    qspi_cmd.address = 0x1000U;
    qspi_cmd.data_length = 16U;
    qspi_cmd.dummy_cycles = 8U;
    TF_CHECK_INT(port_qspi_command(&qspi, &qspi_cmd), PORT_OK);
    memcpy(frame, "qspi-tx", 8U);
    TF_CHECK_INT(port_qspi_transmit(&qspi, frame, 8U, 100U), PORT_OK);
    TF_CHECK_INT(port_qspi_transmit(&qspi, NULL, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_qspi_transmit(&qspi, NULL, 0U, 0U), PORT_OK);
    memset(rx, 0xAA, sizeof(rx));
    TF_CHECK_INT(port_qspi_receive(&qspi, rx, 8U, 100U), PORT_OK);
    for (i = 0U; i < 8U; i++) {
        TF_CHECK_INT(rx[i], 0U);
    }
    TF_CHECK_INT(port_qspi_receive(&qspi, NULL, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_qspi_receive(&qspi, NULL, 0U, 0U), PORT_OK);
    TF_CHECK_INT(port_qspi_memory_map(&qspi), PORT_OK);
    TF_CHECK_INT(port_qspi_memory_unmap(&qspi), PORT_OK);
    TF_CHECK_INT(port_qspi_memory_map(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_qspi_memory_unmap(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_qspi_deinit(&qspi), PORT_OK);
    TF_CHECK_INT(port_qspi_command(&qspi, &qspi_cmd), PORT_NOT_READY);
    TF_CHECK_INT(port_qspi_memory_map(&qspi), PORT_NOT_READY);
    TF_CHECK_INT(port_qspi_deinit(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("optional/SDIO 块读写与 DMA");
    port_sim_reset_state();
    sdio_cfg.clock_frequency = 400000U;
    sdio_cfg.bus_width = PORT_SDIO_BUS_4BIT;
    TF_CHECK_INT(port_sdio_init(NULL, &sdio_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_sdio_init(&sdio, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_sdio_init(&sdio, &sdio_cfg), PORT_OK);
    for (i = 0U; i < TEST_SDIO_BLOCK_SIZE; i++) {
        buffer[i] = (uint8_t)(i & 0xFFU);
    }
    TF_CHECK_INT(port_sdio_write(&sdio, 2U, buffer, 1U, 100U), PORT_OK);
    memset(readback, 0, sizeof(readback));
    TF_CHECK_INT(port_sdio_read(&sdio, 2U, readback, 1U, 100U), PORT_OK);
    TF_CHECK_MEM(readback, buffer, TEST_SDIO_BLOCK_SIZE);
    memset(buffer, 0x5A, sizeof(buffer));
    TF_CHECK_INT(port_sdio_write_dma(&sdio, 4U, buffer, 1U), PORT_OK);
    memset(readback, 0, sizeof(readback));
    TF_CHECK_INT(port_sdio_read_dma(&sdio, 4U, readback, 1U), PORT_OK);
    TF_CHECK_MEM(readback, buffer, TEST_SDIO_BLOCK_SIZE);
    TF_CHECK_INT(port_sdio_read(&sdio, TEST_SDIO_BLOCK_MAX, readback, 1U, 0U),
                 PORT_INVALID_PARAM);
    TF_CHECK_INT(port_sdio_write(&sdio, TEST_SDIO_BLOCK_MAX + 1U, buffer, 1U, 0U),
                 PORT_INVALID_PARAM);
    TF_CHECK_INT(port_sdio_read(&sdio, TEST_SDIO_BLOCK_MAX, readback, 0U, 0U), PORT_OK);
    TF_CHECK_INT(port_sdio_read(&sdio, 0U, NULL, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_sdio_write(&sdio, 0U, NULL, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_sdio_read(NULL, 0U, readback, 1U, 0U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_sdio_deinit(&sdio), PORT_OK);
    TF_CHECK_INT(port_sdio_read(&sdio, 0U, readback, 1U, 0U), PORT_NOT_READY);
    TF_CHECK_INT(port_sdio_deinit(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("optional/USB 状态机");
    port_sim_reset_state();
    usb_cfg.mode = PORT_USB_MODE_DEVICE;
    usb_cfg.speed = PORT_USB_SPEED_FULL;
    TF_CHECK_INT(port_usb_init(NULL, &usb_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_usb_init(&usb, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_usb_init(&usb, &usb_cfg), PORT_OK);
    TF_CHECK_INT(port_usb_start(&usb), PORT_OK);
    TF_CHECK_INT(port_usb_connect(&usb), PORT_OK);
    TF_CHECK_INT(port_usb_disconnect(&usb), PORT_OK);
    TF_CHECK_INT(port_usb_stop(&usb), PORT_OK);
    TF_CHECK_INT(port_usb_connect(&usb), PORT_OK);   /* 已初始化仍可连接 */
    TF_CHECK_INT(port_usb_disconnect(&usb), PORT_OK);
    TF_CHECK_INT(port_usb_start(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_usb_stop(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_usb_connect(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_usb_disconnect(NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_usb_deinit(&usb), PORT_OK);
    TF_CHECK_INT(port_usb_start(&usb), PORT_NOT_READY);
    TF_CHECK_INT(port_usb_connect(&usb), PORT_NOT_READY);
    TF_CHECK_INT(port_usb_stop(&usb), PORT_OK);
    TF_CHECK_INT(port_usb_deinit(NULL), PORT_INVALID_PARAM);
    tf_end();

    tf_begin("optional/ETH 启停与收发");
    port_sim_reset_state();
    eth_cfg.speed = 100U;
    eth_cfg.full_duplex = true;
    eth_cfg.checksum_offload = false;
    TF_CHECK_INT(port_eth_init(NULL, &eth_cfg), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_eth_init(&eth, NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_eth_init(&eth, &eth_cfg), PORT_OK);
    memcpy(frame, "eth-frame-data", 14U);
    TF_CHECK_INT(port_eth_transmit(&eth, frame, 14U), PORT_NOT_READY);
    TF_CHECK_INT(port_eth_start(&eth), PORT_OK);
    TF_CHECK_INT(port_eth_transmit(&eth, frame, 14U), PORT_OK);
    received = 123U;
    TF_CHECK_INT(port_eth_receive(&eth, rx, sizeof(rx), &received), PORT_OK);
    TF_CHECK_INT(received, 0U);
    TF_CHECK_INT(port_eth_transmit(&eth, NULL, 1U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_eth_transmit(&eth, NULL, 0U), PORT_OK);
    TF_CHECK_INT(port_eth_receive(&eth, NULL, sizeof(rx), &received), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_eth_receive(&eth, rx, sizeof(rx), NULL), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_eth_transmit(NULL, frame, 14U), PORT_INVALID_PARAM);
    TF_CHECK_INT(port_eth_stop(&eth), PORT_OK);
    TF_CHECK_INT(port_eth_transmit(&eth, frame, 14U), PORT_NOT_READY);
    TF_CHECK_INT(port_eth_deinit(&eth), PORT_OK);
    TF_CHECK_INT(port_eth_start(&eth), PORT_NOT_READY);
    TF_CHECK_INT(port_eth_transmit(&eth, frame, 14U), PORT_NOT_READY);
    TF_CHECK_INT(port_eth_deinit(NULL), PORT_INVALID_PARAM);
    tf_end();
}
