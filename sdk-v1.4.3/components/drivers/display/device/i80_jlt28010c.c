#include <stdio.h>
#include "i80_ctl.h"
#include "i80_lcd_driver.h"
#include "syslog.h"
#include "aiva_sleep.h"
#include "sysctl.h"
#include "gpio.h"
#include "udevice.h"


#define L2R_U2D     (0)     // from left to right, up to down
#define L2R_D2U     (1)     // from left to right, down to up
#define R2L_U2D     (2)     // from right to left, up to down
#define R2L_D2U     (3)     // from right to left, down to top
#define U2D_L2R     (4)     // from up to down, left to right
#define U2D_R2L     (5)     // from up to down, right to left
#define D2U_L2R     (6)     // from down to up, left to right
#define D2U_R2L     (7)     // from down to up, right to left


// static int g_dir            = 0; //0: vertical display 1: horizontal display
static int g_width          = 240;
static int g_height         = 320;
// static int g_dft_scan_dir   = U2D_R2L;


static int i80_jlt28010c_get_params(i80_lcd_driver_t *dev_driver, display_i80_params_t *param)
{
    (void)dev_driver;
    param->width   = g_width;
    param->height  = g_height;
    param->out_fmt = I80_OUT_FMT_16BIT_RGB565;

    return 0;
}

static int i80_jlt28010c_scan_dir(int scan_dir)
{
    uint32_t cmd;
    uint16_t cmd_param[16];
    uint32_t regVal = 0x0;

    switch(scan_dir) {
        case 0: scan_dir = 6; break;
        case 1: scan_dir = 7; break;
        case 2: scan_dir = 4; break;
        case 3: scan_dir = 5; break;
        case 4: scan_dir = 1; break;
        case 5: scan_dir = 0; break;
        case 6: scan_dir = 3; break;
        case 7: scan_dir = 2; break;
    }

    switch(scan_dir) {
        case L2R_U2D:
            regVal |= (0<<7) | (0<<6) | (0<<5);
            break;
        case L2R_D2U:
            regVal |= (1<<7) | (0<<6) | (0<<5);
            break;
        case R2L_U2D:
            regVal |= (0<<7) | (1<<6) | (0<<5);
            break;
        case R2L_D2U:
            regVal |= (1<<7) | (1<<6) | (0<<5);
            break;
        case U2D_L2R:
            regVal |= (0<<7) | (0<<6) | (1<<5);
            break;
        case U2D_R2L:
            regVal |= (0<<7) | (1<<6) | (1<<5);
            break;
        case D2U_L2R:
            regVal |= (1<<7) | (0<<6) | (1<<5);
            break;
        case D2U_R2L:
            regVal |= (1<<7) | (1<<6) | (1<<5);
            break;
    }
    regVal |= 0x08;

    //dir reg
    cmd             = 0x36;
    cmd_param[0]    = regVal;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 1);

    //set x
    cmd             = 0x2A;
    cmd_param[0]    = 0x0;
    cmd_param[1]    = 0x0;
    cmd_param[2]    = (g_width-1) >> 8;
    cmd_param[3]    = (g_width-1) & 0xFF;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 4);

    //set y
    cmd             = 0x2B;
    cmd_param[0]    = 0x0;
    cmd_param[1]    = 0x0;
    cmd_param[2]    = (g_height-1) >> 8;
    cmd_param[3]    = (g_height-1) & 0xFF;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 4);

    return 0;
}



static int i80_jlt28010c_display_dir(int dir)
{
    if (dir == 0) {
        //vertical display
        i80_jlt28010c_scan_dir(U2D_R2L);
    } else {
        //horizontal display
        i80_jlt28010c_scan_dir(L2R_U2D);
    }
    return 0;
}



static int i80_jlt28010c_init(i80_lcd_driver_t *dev_driver)
{
    uint32_t cmd;
    uint16_t cmd_param[16];
    uint16_t cmd_result[16];

    i80_ctl_reset_device();

    cmd = 0x04; //read display identification information
    i80_ctl_send_rd_cmd(cmd, &cmd_result[0], 4);
    LOGI(__func__, "DISPLAY ID: 0x%x, 0x%x, 0x%x, 0x%x\n", (int)cmd_result[0], (int)cmd_result[1], (int)cmd_result[2], (int)cmd_result[3]);

    cmd             = 0xCF;
    cmd_param[0]    = 0x00;
    cmd_param[1]    = 0x81;
    cmd_param[2]    = 0x30;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 3);

    cmd             = 0xED;
    cmd_param[0]    = 0x64;
    cmd_param[1]    = 0x03;
    cmd_param[2]    = 0x12;
    cmd_param[3]    = 0x81;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 4);

    cmd             = 0xE8;
    cmd_param[0]    = 0x85;
    cmd_param[1]    = 0x10;
    cmd_param[2]    = 0x78;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 3);

    cmd             = 0xCB;
    cmd_param[0]    = 0x39;
    cmd_param[1]    = 0x2C;
    cmd_param[2]    = 0x00;
    cmd_param[3]    = 0x34;
    cmd_param[4]    = 0x02;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 5);

    cmd             = 0xF7;
    cmd_param[0]    = 0x20;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 1);

    cmd             = 0xEA;
    cmd_param[0]    = 0x00;
    cmd_param[1]    = 0x00;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 2);

    cmd             = 0xB1;
    cmd_param[0]    = 0x00;
    cmd_param[1]    = 0x1B;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 2);

    cmd             = 0xB6;
    cmd_param[0]    = 0x0A;
    cmd_param[1]    = 0xA2;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 2);

    cmd             = 0xC0; //Power control
    cmd_param[0]    = 0x21; //VRH[5:0]
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 1);

    cmd             = 0xC1; //Power control
    cmd_param[0]    = 0x11; //SAP[2:0]; BT[3:0]
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 1);

    cmd             = 0xC5; //VCM control
    cmd_param[0]    = 0x3F;
    cmd_param[1]    = 0x3C;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 2);

    cmd             = 0xC7; //VCM control
    cmd_param[0]    = 0xB5;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 1);

    cmd             = 0x36; //Memory Access Control
    cmd_param[0]    = 0x48;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 1);

    cmd             = 0x3A; //Memory Access Control
    cmd_param[0]    = 0x55;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 1);

    //cmd             = 0xB1;
    //cmd_param[0]    = 0x00;
    //cmd_param[1]    = 0x17;
    //i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 2);


    //cmd             = 0xB6; //Display Function Control
    //cmd_param[0]    = 0x0A;
    //cmd_param[1]    = 0xA2; //82
    //i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 2);

    cmd             = 0xF2; //3Gamma Function Disable
    cmd_param[0]    = 0x00;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 1);

    cmd             = 0x26; //Gamma curve selected
    cmd_param[0]    = 0x01;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 1);

    cmd             = 0xE0; //Set Gamma
    cmd_param[0]    = 0x0F;
    cmd_param[1]    = 0x26;
    cmd_param[2]    = 0x24;
    cmd_param[3]    = 0x0B;
    cmd_param[4]    = 0x0E;
    cmd_param[5]    = 0x09;
    cmd_param[6]    = 0x54;
    cmd_param[7]    = 0xA8;
    cmd_param[8]    = 0x46;
    cmd_param[9]    = 0x0C;
    cmd_param[10]   = 0x17;
    cmd_param[11]   = 0x09;
    cmd_param[12]   = 0x0F;
    cmd_param[13]   = 0x07;
    cmd_param[14]   = 0x00;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 15);


    cmd             = 0xE1; //Set Gamma
    cmd_param[0]    = 0x00;
    cmd_param[1]    = 0x19;
    cmd_param[2]    = 0x1B;
    cmd_param[3]    = 0x04;
    cmd_param[4]    = 0x10;
    cmd_param[5]    = 0x07;
    cmd_param[6]    = 0x2A;
    cmd_param[7]    = 0x47;
    cmd_param[8]    = 0x39;
    cmd_param[9]    = 0x03;
    cmd_param[10]   = 0x06;
    cmd_param[11]   = 0x06;
    cmd_param[12]   = 0x30;
    cmd_param[13]   = 0x38;
    cmd_param[14]   = 0x0F;
    i80_ctl_send_wr_cmd(cmd, &cmd_param[0], 15);


    cmd             = 0x11; //Exit Sleep
    i80_ctl_send_wr_cmd(cmd, NULL, 0);

    aiva_msleep(150);

    cmd             = 0x29; //Display on
    i80_ctl_send_wr_cmd(cmd, NULL, 0);

    //TODO: Light on backlight

    i80_jlt28010c_display_dir(0);

    dev_driver->set_backlight(dev_driver, 1);

    return 0;
}

static int i80_jlt28010c_suspend(i80_lcd_driver_t *dev_driver)
{
    dev_driver->set_backlight(dev_driver, 0);

    return 0;
}


static int i80_jlt28010c_resume(i80_lcd_driver_t *dev_driver)
{
    dev_driver->set_backlight(dev_driver, 1);

    return 0;
}

static void i80_jlt28010c_set_backlight(i80_lcd_driver_t *dev_driver, uint32_t value)
{
	int backlight_pin = dev_driver->backlight_pin;

    if (value > 0) {
        sysctl_set_io_switch(IO_SWITCH_PWM2, 0x0);
        gpio_set_drive_mode(backlight_pin, GPIO_DM_OUTPUT);
        gpio_set_pin(backlight_pin, GPIO_PV_LOW);
    } else {
        sysctl_set_io_switch(IO_SWITCH_PWM2, 0x0);
        gpio_set_drive_mode(backlight_pin, GPIO_DM_OUTPUT);
        gpio_set_pin(backlight_pin, GPIO_PV_HIGH);
    }
}


static i80_lcd_driver_t i80_jlt28010c = {
    .name                   = "i80_jlt28010c",
    .context                = NULL,
    .backlight_pin          = GPIO_PIN2,

    .init                   = i80_jlt28010c_init,
    .suspend                = i80_jlt28010c_suspend,
    .resume                 = i80_jlt28010c_resume,
    .set_backlight          = i80_jlt28010c_set_backlight,
    .get_interface_param    = i80_jlt28010c_get_params,
};

/* Export Device:
 * 	    [uclass ID], [device name], [flags], [driver], [private pointer]
 */
UDEVICE_EXPORT(UCLASS_DISPLAY_I80,     i80_jlt28010c,    0,      &i80_jlt28010c,    NULL);