/* Winbond NOR flash driver */
#include "p25qxx.h"
#include "spi.h"
#include "sysctl.h"
#include "dmac.h"
#include "flash.h"
#include "aiva_sleep.h"
#include "syslog.h"
#include "aiva_malloc.h"
#include "udevice.h"
#include "nor_flash_dev.h"
#include <string.h>

/* clang-format off */

#define SPI_SLAVE_SELECT                    (0x01)

#define p25qxx_FLASH_PAGE_SIZE              256
#define p25qxx_FLASH_SECTOR_SIZE            4096
#define p25qxx_FLASH_PAGE_NUM_PER_SECTOR    16

#define WRITE_ENABLE                        0x06
#define WRITE_DISABLE                       0x04
#define READ_REG1                           0x05
#define READ_REG2                           0x35
#define READ_REG3                           0x15
#define WRITE_REG1                          0x01
#define WRITE_REG2                          0x31
#define WRITE_REG3                          0x11
#define READ_DATA                           0x03
#define FAST_READ                           0x0B
#define FAST_READ_DUAL_OUTPUT               0x3B
#define FAST_READ_QUAL_OUTPUT               0x6B
#define FAST_READ_DUAL_IO                   0xBB
#define FAST_READ_QUAL_IO                   0xEB
#define DUAL_READ_RESET                     0xFFFF
#define QUAL_READ_RESET                     0xFF
#define PAGE_PROGRAM                        0x02
#define QUAD_PAGE_PROGRAM                   0x32
#define SECTOR_ERASE                        0x20
#define BLOCK_32K_ERASE                     0x52
#define BLOCK_64K_ERASE                     0xD8
#define CHIP_ERASE                          0x60
#define READ_ID                             0x9F
#define READ_UNIQUE_ID                      0x4B
#define ENABLE_QPI                          0x38
#define EXIT_QPI                            0xFF
#define ENABLE_RESET                        0x66
#define RESET_DEVICE                        0x99

#define REG1_BUSY_MASK                      0x01
#define REG2_QUAL_MASK                      0x02

#define p25q16_FLASH_CHIP_SIZE             (2*1024*1024)
#define p25q32_FLASH_CHIP_SIZE             (4*1024*1024)
#define p25q64_FLASH_CHIP_SIZE             (8*1024*1024)
/* clang-format on */
typedef struct feature_prot_cfg {
    uint32_t addr_protected_bound;
    uint8_t  feature_prot_cfg_value;
} feature_prot_cfg_t;

typedef int (*page_program_func_t)(uint32_t addr, uint8_t * data_buf, uint32_t length);
typedef int (*read_func_t)(uint32_t addr, uint8_t *data_buf, uint32_t length);

static feature_prot_cfg_t feature_prot_cfg_lut_16m[]={
    {p25q16_FLASH_CHIP_SIZE / 32, (0b01001) << 2},
    {p25q16_FLASH_CHIP_SIZE / 16, (0b01010) << 2},
    {p25q16_FLASH_CHIP_SIZE /  8, (0b01011) << 2},
    {p25q16_FLASH_CHIP_SIZE /  4, (0b10100) << 2},
    {p25q16_FLASH_CHIP_SIZE /  2, (0b10101) << 2},
    {p25q16_FLASH_CHIP_SIZE /  1, (0b11110) << 2},
};

static feature_prot_cfg_t feature_prot_cfg_lut_32m[]={
    {p25q32_FLASH_CHIP_SIZE / 64, (0b01001) << 2},
    {p25q32_FLASH_CHIP_SIZE / 32, (0b01010) << 2},
    {p25q32_FLASH_CHIP_SIZE / 16, (0b01011) << 2},
    {p25q32_FLASH_CHIP_SIZE /  8, (0b01100) << 2},
    {p25q32_FLASH_CHIP_SIZE /  4, (0b10101) << 2},
    {p25q32_FLASH_CHIP_SIZE /  2, (0b10110) << 2},
    {p25q32_FLASH_CHIP_SIZE /  1, (0b11111) << 2},
};

static feature_prot_cfg_t feature_prot_cfg_lut_64m[]={
    {p25q64_FLASH_CHIP_SIZE / 64, (0b01001) << 2},
    {p25q64_FLASH_CHIP_SIZE / 32, (0b01010) << 2},
    {p25q64_FLASH_CHIP_SIZE / 16, (0b01011) << 2},
    {p25q64_FLASH_CHIP_SIZE /  8, (0b01100) << 2},
    {p25q64_FLASH_CHIP_SIZE /  4, (0b10101) << 2},
    {p25q64_FLASH_CHIP_SIZE /  2, (0b10110) << 2},
    {p25q64_FLASH_CHIP_SIZE /  1, (0b11111) << 2},
};

static uint32_t m_spi_bus_no = 0;
static uint32_t m_spi_chip_select = 0;
static uint32_t m_chip_size = (2 * 1024 * 1024);
static nor_flash_param_t m_param = {0};

static int _p25qxx_quad_page_program(uint32_t addr, uint8_t *data_buf, uint32_t length);
static int _p25qxx_page_program(uint32_t addr, uint8_t *data_buf, uint32_t length);
static int p25qxx_quad_read_data(uint32_t addr, uint8_t *data_buf, uint32_t length);
static int p25qxx_read_data(uint32_t addr, uint8_t *data_buf, uint32_t length);

static page_program_func_t m_page_program_func;
static read_func_t m_read_func;
static int p25qxx_wait_for_ready();


static int p25qxx_receive_data(uint8_t *cmd_buff, uint8_t cmd_len, uint8_t *rx_buff, uint32_t rx_len)
{
    int ret;
    spi_init(m_spi_bus_no, SPI_WORK_MODE_0, SPI_FF_STANDARD, FIFO_W8);
#ifndef FLASH_USE_DMA
    ret = spi_receive_data_standard(m_spi_bus_no, m_spi_chip_select, cmd_buff, cmd_len, rx_buff, rx_len);
#else
    ret = spi_receive_data_standard_dma(m_spi_bus_no, m_spi_chip_select, cmd_buff, cmd_len, rx_buff, rx_len);
#endif
    if (ret < 0) {
        return -FLASH_ERROR_SPI_TRANS;
    }
    return FLASH_OK;
}


static int p25qxx_send_data(uint8_t *cmd_buff, uint8_t cmd_len, uint8_t *tx_buff, uint32_t tx_len)
{
    int ret;
    spi_init(m_spi_bus_no, SPI_WORK_MODE_0, SPI_FF_STANDARD, FIFO_W8);
#ifndef FLASH_USE_DMA
    ret = spi_send_data_standard(m_spi_bus_no, m_spi_chip_select, cmd_buff, cmd_len, tx_buff, tx_len);
#else
    ret = spi_send_data_standard_dma(m_spi_bus_no, m_spi_chip_select, cmd_buff, cmd_len, tx_buff, tx_len);
#endif
    if (ret < 0) {
        return -FLASH_ERROR_SPI_TRANS;
    }
    return FLASH_OK;
}


static int p25qxx_receive_data_enhanced(uint32_t *cmd_buff, uint8_t cmd_len, uint8_t *rx_buff, uint32_t rx_len)
{
    int ret;

    spi_init(m_spi_bus_no, SPI_WORK_MODE_0, SPI_FF_QUAD, FIFO_W32);
    spi_init_non_standard(m_spi_bus_no, 8/*instrction length*/, 32/*address length*/, 4/*wait cycles*/,
                            SPI_AITM_ADDR_STANDARD/*spi address trans mode*/);
#ifndef FLASH_USE_DMA
    ret = spi_receive_data_multiple(m_spi_bus_no, m_spi_chip_select, cmd_buff, cmd_len, rx_buff, rx_len);
#else
    ret = spi_receive_data_multiple_dma(m_spi_bus_no, m_spi_chip_select, cmd_buff, cmd_len, rx_buff, rx_len);
#endif
    if (ret < 0) {
        return -FLASH_ERROR_SPI_TRANS;
    }
    return FLASH_OK;
}


static int p25qxx_send_data_enhanced(uint32_t *cmd_buff, uint8_t cmd_len, uint8_t *tx_buff, uint32_t tx_len)
{
    int ret;

    spi_init(m_spi_bus_no, SPI_WORK_MODE_0, SPI_FF_QUAD, FIFO_W32);
    spi_init_non_standard(m_spi_bus_no, 8/*instrction length*/, 24/*address length*/, 0/*wait cycles*/,
                            SPI_AITM_STANDARD/*spi address trans mode*/);
#ifndef FLASH_USE_DMA
    ret = spi_send_data_multiple(m_spi_bus_no, m_spi_chip_select, cmd_buff, cmd_len, tx_buff, tx_len);
#else
    ret = spi_send_data_multiple_dma(m_spi_bus_no, m_spi_chip_select, cmd_buff, cmd_len, tx_buff, tx_len);
#endif
    if (ret < 0) {
        return -FLASH_ERROR_SPI_TRANS;
    }

    return FLASH_OK;
}


static int p25qxx_write_enable(void)
{
    int ret;
    uint8_t cmd[1] = {WRITE_ENABLE};

    ret = p25qxx_send_data(cmd, 1, 0, 0);
    return ret;
}


static int p25qxx_write_status_reg1(uint8_t reg1_data)
{
    int ret;
    uint8_t cmd[2] = {WRITE_REG1, reg1_data};

    ret = p25qxx_write_enable();
    if (ret < 0) {
        return ret;
    }
    ret = p25qxx_send_data(cmd, 2, 0, 0);
    if (ret < 0) {
        return ret;
    }
    ret = p25qxx_wait_for_ready();
    return ret;
}


static int p25qxx_write_status_reg2(uint8_t reg2_data)
{
    int ret;
    uint8_t cmd[2] = {WRITE_REG2, reg2_data};

    ret = p25qxx_write_enable();
    if (ret < 0) {
        return ret;
    }
    ret = p25qxx_send_data(cmd, 2, 0, 0);
    if (ret < 0) {
        return ret;
    }
    ret = p25qxx_wait_for_ready();
    return ret;
}


static int p25qxx_read_status_reg1(uint8_t *reg_data)
{
    int ret;
    uint8_t cmd[1] = {READ_REG1};
    uint8_t data[1] = {0};

    ret = p25qxx_receive_data(cmd, 1, data, 1);
    if (ret < 0) {
        return ret;
    }
    *reg_data = data[0];
    return FLASH_OK;
}


static int p25qxx_read_status_reg2(uint8_t *reg_data)
{
    int ret;
    uint8_t cmd[1] = {READ_REG2};
    uint8_t data[1] = {0};

    ret = p25qxx_receive_data(cmd, 1, data, 1);
    if (ret < 0) {
        return ret;
    }
    *reg_data = data[0];
    return FLASH_OK;
}


static int p25qxx_enable_quad_mode(void)
{
    int ret;
    uint8_t reg2_data = 0;

    ret = p25qxx_read_status_reg2(&reg2_data);
    if (ret < 0) {
        return ret;
    }
    if (!(reg2_data & REG2_QUAL_MASK)) {
        reg2_data |= REG2_QUAL_MASK;
        p25qxx_write_status_reg2(reg2_data);
    }

    // Read status register 2 and check the QE bit is enabled
    ret = p25qxx_read_status_reg2(&reg2_data);
    if (ret < 0) {
        return ret;
    }
    if (!(reg2_data & REG2_QUAL_MASK)) {
        return -FLASH_ERROR_STATUS;
    }

    m_read_func = p25qxx_quad_read_data;
    m_page_program_func = _p25qxx_quad_page_program;

    return FLASH_OK;
}


static int p25qxx_write_prot_cfg(uint8_t prot_value)
{
    int ret;
    uint8_t reg_data1 = 0;

    ret = p25qxx_read_status_reg1(&reg_data1);
    if (ret < 0) {
        return ret;
    }

    reg_data1 &= 0x83;
    reg_data1 |= prot_value;
    p25qxx_write_status_reg1(reg_data1);

    return FLASH_OK;
}


static int p25qxx_disable_quad_mode(void)
{
    int ret;
    uint8_t reg2_data = 0;
    ret = p25qxx_read_status_reg2(&reg2_data);
    if (ret < 0) {
        return ret;
    }
    if (reg2_data & REG2_QUAL_MASK) {
        reg2_data &= (~REG2_QUAL_MASK);
        ret = p25qxx_write_status_reg2(reg2_data);
    }

    // Read status register 2 and check the QE bit disabled
    ret = p25qxx_read_status_reg2(&reg2_data);
    if (ret < 0) {
        return ret;
    }
    if (reg2_data & REG2_QUAL_MASK) {
        return -FLASH_ERROR_STATUS;
    }

    m_read_func = p25qxx_read_data;
    m_page_program_func = _p25qxx_page_program;
    return ret;
}


static int p25qxx_is_busy(void)
{
    uint8_t status = 0;

    p25qxx_read_status_reg1(&status);
    if (status & REG1_BUSY_MASK) {
        return 1;
    }
    return 0;
}


static int p25qxx_wait_for_ready()
{
    uint32_t timeout;

    timeout = 20 * 1000 * 100; // 20 sec

    while(timeout--) {
        if (!p25qxx_is_busy()) {
            return FLASH_OK;
        }
        aiva_usleep(10);
    }
    return -FLASH_ERROR_TIMEOUT;
}


int p25qxx_sector_erase(uint32_t addr)
{
    uint8_t cmd[4] = {SECTOR_ERASE};
    int ret;

    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)(addr);
    ret = p25qxx_write_enable();
    if (ret < 0) {
        return ret;
    }
    ret = p25qxx_send_data(cmd, 4, 0, 0);
    if (ret < 0) {
        return ret;
    }
    if (p25qxx_wait_for_ready() < 0) {
        return -FLASH_ERROR_TIMEOUT;
    }
    return FLASH_OK;
}


static int _p25qxx_quad_page_program(uint32_t addr, uint8_t *data_buf, uint32_t length)
{
    uint32_t cmd[2] = {0};
    int ret;

    cmd[0] = QUAD_PAGE_PROGRAM;
    cmd[1] = addr;
    ret = p25qxx_write_enable();
    if (ret < 0) {
        return ret;
    }

    ret = p25qxx_send_data_enhanced(cmd, 2, data_buf, length);
    if (ret < 0) {
        return ret;
    }
    if (p25qxx_wait_for_ready() < 0) {
        return -FLASH_ERROR_TIMEOUT;
    }
    return FLASH_OK;
}


static int _p25qxx_page_program(uint32_t addr, uint8_t *data_buf, uint32_t length)
{
    uint8_t cmd[4];
    int ret;

    cmd[0] = PAGE_PROGRAM;
    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)(addr);

    ret = p25qxx_write_enable();
    if (ret < 0) {
        return ret;
    }
    ret = p25qxx_send_data(cmd, 4, data_buf, length);
    if (ret < 0) {
        return ret;
    }

    if (p25qxx_wait_for_ready() < 0) {
        return -FLASH_ERROR_TIMEOUT;
    }
    return FLASH_OK;
}


int p25qxx_page_program(uint32_t addr, uint8_t *data_buf, uint32_t length)
{
    (void)(length);

    int ret;
    ret = m_page_program_func(addr, data_buf, p25qxx_FLASH_PAGE_SIZE);
    return ret;
}

static int p25qxx_sector_program(uint32_t addr, uint8_t *data_buf)
{
    uint8_t index = 0;
    int ret;

    for (index = 0; index < p25qxx_FLASH_PAGE_NUM_PER_SECTOR; index++)
    {
        ret = p25qxx_page_program(addr, data_buf, p25qxx_FLASH_PAGE_SIZE);
        if (ret < 0) {
            return ret;
        }
        addr += p25qxx_FLASH_PAGE_SIZE;
        data_buf += p25qxx_FLASH_PAGE_SIZE;
    }
    return FLASH_OK;
}


static int p25qxx_quad_read_data(uint32_t addr, uint8_t *data_buf, uint32_t length)
{
    uint32_t cmd[2] = {0};
    int ret;

    cmd[0] = FAST_READ_QUAL_IO;
    cmd[1] = addr << 8;

    ret = p25qxx_receive_data_enhanced(cmd, 2, data_buf, length);

    return ret;
}


static int p25qxx_read_data(uint32_t addr, uint8_t *data_buf, uint32_t length)
{
    uint8_t cmd[4];
    int ret;

    cmd[0] = READ_DATA;
    cmd[1] = (uint8_t)(addr >> 16);
    cmd[2] = (uint8_t)(addr >> 8);
    cmd[3] = (uint8_t)(addr);

    ret = p25qxx_receive_data(cmd, 4, data_buf, length);

    return ret;
}


int p25qxx_read_id(uint8_t *manuf_id, uint16_t *device_id)
{
    uint8_t cmd[1] = {READ_ID};
    uint8_t data[3] = {0};
    int ret;

    ret = p25qxx_receive_data(cmd, 1, data, 3);
    if (ret < 0) {
        return ret;
    }
    if (data[0] != 0x85) {
        return -FLASH_UNSPPORT;
    }
    *manuf_id = data[0];
    *device_id = ((uint16_t)(data[1]) << 8) | (uint16_t)(data[2]);
    return FLASH_OK;
}


int p25qxx_read_unique_id(flash_unique_id_t *unique_id)
{
    uint8_t cmd[1] = {READ_UNIQUE_ID};
    uint8_t data[20] = {0};
    int ret;

    ret = p25qxx_receive_data(cmd, 1, data, 20);
    if (ret < 0) {
        return ret;
    }

    unique_id->id_length = 16;
    memcpy(unique_id->uid, &data[4], 16);

    return FLASH_OK;
}


int p25qxx_read(uint32_t addr, uint8_t *data_buf, uint32_t length)
{
    int ret;
    uint32_t len = 0;

    // 1. Check whether the addr is legal
    if (addr >= m_chip_size) {
        return -FLASH_WRONG_PARAM;
    }
    // 2. Make sure the length is no longer than the max flash size
    length = (addr + length) > m_chip_size ? (m_chip_size - addr) : length;


    while (length) {
        len = ((length >= FIFO_NOR_LEN) ? FIFO_NOR_LEN : length);
        ret = m_read_func(addr, data_buf, len);
        if (ret < 0) {
            return ret;
        }
        addr += len;
        data_buf += len;
        length -= len;
    }

    return FLASH_OK;
}


int p25qxx_write(uint32_t addr, uint8_t *data_buf, uint32_t length)
{
    int ret;
    uint32_t sector_addr = 0;
    uint32_t sector_offset = 0;
    uint32_t sector_remain = 0;
    uint32_t write_len = 0;
    uint32_t index = 0;
    uint8_t *pread = NULL;
    uint8_t *pwrite = NULL;
    /*uint8_t swap_buf[p25qxx_FLASH_SECTOR_SIZE] = {0};*/
    uint8_t *swap_buf = aiva_malloc(p25qxx_FLASH_SECTOR_SIZE);

    configASSERT(swap_buf != NULL);

    // 1. Check whether the addr is legal
    if (addr >= m_chip_size) {
        ret = -FLASH_WRONG_PARAM;
        goto out;
    }
    // 2. Make sure the length is no longer than the max flash size
    length = (addr + length) > m_chip_size ? (m_chip_size - addr) : length;

    while (length)
    {
        sector_addr   = addr & (~(p25qxx_FLASH_SECTOR_SIZE - 1));
        sector_offset = addr & (p25qxx_FLASH_SECTOR_SIZE - 1);
        sector_remain = p25qxx_FLASH_SECTOR_SIZE - sector_offset;
        write_len     = ((length < sector_remain) ? length : sector_remain);
        ret = p25qxx_read(sector_addr, swap_buf, p25qxx_FLASH_SECTOR_SIZE);
        if (ret < 0) {
            goto out;
        }
        pread = swap_buf + sector_offset;
        pwrite = data_buf;
        for (index = 0; index < write_len; index++) {
            if ((*pwrite) != ((*pwrite) & (*pread))) {
                ret = p25qxx_sector_erase(sector_addr);
                if (ret < 0) {
                    goto out;
                }
                break;
            }
            pwrite++;
            pread++;
        }
        if (write_len == p25qxx_FLASH_SECTOR_SIZE) {
            ret = p25qxx_sector_program(sector_addr, data_buf);
        }
        else {
            pread = swap_buf + sector_offset;
            pwrite = data_buf;
            for (index = 0; index < write_len; index++)
                *pread++ = *pwrite++;
            ret = p25qxx_sector_program(sector_addr, swap_buf);
        }
        if (ret < 0) {
            goto out;
        }
        length   -= write_len;
        addr     += write_len;
        data_buf += write_len;
    }

out:
    aiva_free(swap_buf);
    return FLASH_OK;
}


int p25qxx_init(uint8_t spi_index, uint8_t spi_ss, uint32_t rate, int quad_rw)
{
    int ret;
    uint8_t manuf_id;
    uint16_t device_id;

    m_spi_bus_no = spi_index;
    m_spi_chip_select = spi_ss;
    spi_init(m_spi_bus_no, SPI_WORK_MODE_0, SPI_FF_STANDARD, FIFO_W8);
    spi_set_clk_rate(m_spi_bus_no, rate);

    ret = p25qxx_read_id(&manuf_id, &device_id);
    if (ret < 0) {
        return ret;
    }

    if (manuf_id != 0x85) {
        return -FLASH_ERROR;
    }

    m_param.mid = manuf_id;
    m_param.did = device_id;

    switch (device_id) {
        case 0x6018: // p25q128
            m_chip_size = 16 * 1024 * 1024; // 16MB
            m_param.dev_name = "p25q128";
            break;
        case 0x2018:// p25q128HA
            m_chip_size = 16 * 1024 * 1024; // 16MB
            m_param.dev_name = "p25q128HA";
            break;
        default:
            return -FLASH_UNSPPORT;
    }

    m_param.chip_size   = m_chip_size;
    m_param.sector_size = p25qxx_FLASH_SECTOR_SIZE;
    m_param.page_size   = p25qxx_FLASH_PAGE_SIZE;

    if (quad_rw) {
        ret = p25qxx_enable_quad_mode();
    }
    else {
        ret = p25qxx_disable_quad_mode();
    }

    return ret;
}


int p25qxx_get_param(nor_flash_param_t *pparam)
{
    *pparam = m_param;
    return 0;
}


int p25qxx_lock(uint32_t addr, uint32_t length)
{
    uint32_t addr_bound;
    uint8_t feature_prot_cfg_value = 0;
    uint8_t i;

    addr_bound = addr + length;

    if(m_param.chip_size == p25q64_FLASH_CHIP_SIZE)
    {
        for(i = 0; i < sizeof(feature_prot_cfg_lut_64m) / sizeof(feature_prot_cfg_lut_64m[0]); i++)
        {
            if(addr_bound < feature_prot_cfg_lut_64m[i].addr_protected_bound)
            {
                feature_prot_cfg_value = feature_prot_cfg_lut_64m[i].feature_prot_cfg_value;
                break;
            }
        }
        if(i == sizeof(feature_prot_cfg_lut_64m) / sizeof(feature_prot_cfg_lut_64m[0]))
        {
            feature_prot_cfg_value = feature_prot_cfg_lut_64m[i - 1].feature_prot_cfg_value;
        }
    }
    else if(m_param.chip_size == p25q32_FLASH_CHIP_SIZE)
    {
        for(i = 0; i < sizeof(feature_prot_cfg_lut_32m) / sizeof(feature_prot_cfg_lut_32m[0]); i++)
        {
            if(addr_bound < feature_prot_cfg_lut_32m[i].addr_protected_bound)
            {
                feature_prot_cfg_value = feature_prot_cfg_lut_32m[i].feature_prot_cfg_value;
                break;
            }
        }
        if(i == sizeof(feature_prot_cfg_lut_32m) / sizeof(feature_prot_cfg_lut_32m[0]))
        {
            feature_prot_cfg_value = feature_prot_cfg_lut_32m[i - 1].feature_prot_cfg_value;
        }
    }
    else if(m_param.chip_size == p25q16_FLASH_CHIP_SIZE)
    {
        for(i = 0; i < sizeof(feature_prot_cfg_lut_16m) / sizeof(feature_prot_cfg_lut_16m[0]); i++)
        {
            if(addr_bound < feature_prot_cfg_lut_16m[i].addr_protected_bound)
            {
                feature_prot_cfg_value = feature_prot_cfg_lut_16m[i].feature_prot_cfg_value;
                break;
            }
        }
        if(i == sizeof(feature_prot_cfg_lut_16m) / sizeof(feature_prot_cfg_lut_16m[0]))
        {
            feature_prot_cfg_value = feature_prot_cfg_lut_16m[i - 1].feature_prot_cfg_value;
        }
    }
    else {
        feature_prot_cfg_value = 0;
    }
    p25qxx_write_prot_cfg(feature_prot_cfg_value);
    return FLASH_OK;
}


int p25qxx_unlock(uint32_t addr, uint32_t length)
{
    (void)(addr);
    (void)(length);

    p25qxx_write_prot_cfg(0);
    return FLASH_OK;
}


#define PUYA_NOR_OPERATION \
        .init         = p25qxx_init,\
        .read         = p25qxx_read,\
        .write        = p25qxx_write,\
        .page_prog    = p25qxx_page_program,\
        .sector_erase = p25qxx_sector_erase,\
        .get_param    = p25qxx_get_param,\
        .lock         = p25qxx_lock,\
        .unlock       = p25qxx_unlock,\
        .read_unique_id = p25qxx_read_unique_id

static nor_flash_drv_t p25qxx_nor_drivers[] = {
    // PuYa NOR
    {
        .manuf_id     = 0x85,
        .device_id    = 0x2018,
        PUYA_NOR_OPERATION,
    },
    {
        .manuf_id     = 0x85,
        .device_id    = 0x6018,
        PUYA_NOR_OPERATION,
    },
};

/* Export Device:
 * 	    [uclass ID], [device name], [flags], [driver], [private pointer]
 */
UDEVICE_EXPORT(UCLASS_SPI_FLASH,     p25qxx_nor_85_2018,    0,      &p25qxx_nor_drivers[0],    NULL);
UDEVICE_EXPORT(UCLASS_SPI_FLASH,     p25qxx_nor_85_6018,    0,      &p25qxx_nor_drivers[1],    NULL);
