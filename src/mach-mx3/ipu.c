/* ipu.c

   written by Marc Singer
   17 Apr 2007

   Copyright (C) 2007 Marc Singer

   -----------
   DESCRIPTION
   -----------

   o I2C Timing.  This code appears to have a slightly different wave
     form from the code in Matt Jakuc's test code.  The start of the
     frame, signaled by the initial falling clock pulse from the
     master has a long delay in this code, but a very short delay in
     Matt's.  I'm not sure why and it doesn't appear to make a
     difference in the correctness of the I2C frames.

   o Releasing the I2C bus.  It appears to take some time for the bus
     to go not-busy once it is released (in i2c_stop).  The trouble
     with this code is that we should allow for the bus to be taken
     over by another master.  In this boot loader, it isn't really an
     issue, but a driver should cope with the fact that sending STOP
     is enough and we really need to wait for the bus to go idle when
     we want to start a transaction.

   o Calculating the IFDR divisor.  Unfortunately, the Freescale I2C
     core doesn't use a simple divisor register to divide the base
     frequency of 66MHz.  We're using a divisor (1280) that yields
     52KHz as the I2C clock.  If we want to be able to calculate the
     divisor from the target frequency, we'll have a tough row to hoe.

   o I2C Node Addressing.  The I2C bus uses a seven bit address field
     where the node address is in the upper seven bits of the byte and
     the R/W bit is in the lowest bit.  The addresses passed around
     are the 'real' address of 0-127.

   o I2C_I2SR_RXAK.  This bit is set when the ACK is not found on the
     transfer of a byte.  It really should be named NRXAK.  We use the
     name that exists in the documentation.

*/

#include <config.h>
#include <apex.h>
#include <command.h>
#include <service.h>
#include <error.h>
#include <linux/string.h>

#include "hardware.h"

#undef ENTRY
//#define ENTRY printf ("%s\n", __FUNCTION__)
#define ENTRY

#define FRAME_WIDTH	(720)
#define FRAME_HEIGHT	(480)

	/* Definitions for GPIO pins to make the code readable. */
#define PIN_SENSOR_PWR_EN     MX31_PIN_CSI_D4
#define PIN_SENSOR_BUF_EN     MX31_PIN_KEY_ROW4
#define PIN_CMOS_STBY	      MX31_PIN_KEY_ROW7
#define PIN_NCMOS_RESET	      MX31_PIN_KEY_ROW6
#define PIN_CMOS_EXPOSURE     MX31_PIN_KEY_ROW5
#define PIN_ILLUMINATION_EN1  MX31_PIN_KEY_COL5
#define PIN_ILLUMINATION_EN2  MX31_PIN_KEY_COL4

#define I2C_IFDR_V	      (0x19)		/* Divisor of 1280 */


#define IPU_CONF		__REG(PHYS_IPU + 0x00)
#define IPU_CHA_BUF0_RDY	__REG(PHYS_IPU + 0x04)
#define IPU_CHA_BUF1_RDY	__REG(PHYS_IPU + 0x08)
#define IPU_CHA_DB_MODE_SEL	__REG(PHYS_IPU + 0x0c)
#define IPU_CHA_CUR_BUF		__REG(PHYS_IPU + 0x10)
#define IPU_FS_PROC_FLOW	__REG(PHYS_IPU + 0x14)
#define IPU_DISP_PROC_FLOW	__REG(PHYS_IPU + 0x18)
#define IPU_TASK_STAT		__REG(PHYS_IPU + 0x1c)
#define IPU_IMA_ADDR		__REG(PHYS_IPU + 0x20)
#define IPU_IMA_DATA		__REG(PHYS_IPU + 0x24)
#define IPU_INT_CTRL1		__REG(PHYS_IPU + 0x28)
#define IPU_INT_CTRL2		__REG(PHYS_IPU + 0x2c)
#define IPU_INT_CTRL3		__REG(PHYS_IPU + 0x30)
#define IPU_INT_CTRL4		__REG(PHYS_IPU + 0x34)
#define IPU_INT_CTRL5		__REG(PHYS_IPU + 0x38)
#define IPU_INT_STAT1		__REG(PHYS_IPU + 0x3c)
#define IPU_INT_STAT2		__REG(PHYS_IPU + 0x40)
#define IPU_INT_STAT3		__REG(PHYS_IPU + 0x44)
#define IPU_INT_STAT4		__REG(PHYS_IPU + 0x48)
#define IPU_INT_STAT5		__REG(PHYS_IPU + 0x4c)
#define IPU_BRK_CTRL1		__REG(PHYS_IPU + 0x50)
#define IPU_BRK_CTRL2		__REG(PHYS_IPU + 0x54)
#define IPU_BRK_STAT		__REG(PHYS_IPU + 0x58)
#define IPU_DIAGB_CTRL		__REG(PHYS_IPU + 0x5c)
#define CSI_CONF		__REG(PHYS_IPU + 0x60)
#define CSI_SENS_CONF		__REG(PHYS_IPU + 0x60)
#define CSI_SENS_FRM_SIZE	__REG(PHYS_IPU + 0x64)
#define CSI_ACT_FRM_SIZE	__REG(PHYS_IPU + 0x68)
#define CSI_OUT_FRM_CTRL	__REG(PHYS_IPU + 0x6c)
#define CSI_TST_CTRL		__REG(PHYS_IPU + 0x70)
#define CSI_CCIR_CODE1		__REG(PHYS_IPU + 0x74)
#define CSI_CCIR_CODE2		__REG(PHYS_IPU + 0x78)
#define CSI_CCIR_CODE3		__REG(PHYS_IPU + 0x7c)
#define CSI_FLASH_STROBE1	__REG(PHYS_IPU + 0x80)
#define CSI_FLASH_STROBE2	__REG(PHYS_IPU + 0x84)
#define IC_CONF			__REG(PHYS_IPU + 0x88)
#define IC_PRP_ENC_RSC		__REG(PHYS_IPU + 0x8c)
#define IC_PRP_VF_RSC		__REG(PHYS_IPU + 0x90)
#define	IC_PP_RSC		__REG(PHYS_IPU + 0x94)
#define	PF_CONF			__REG(PHYS_IPU + 0xa0)
#define	IDMAC_CONF		__REG(PHYS_IPU + 0xa4)
#define	IDMAC_CHA_EN		__REG(PHYS_IPU + 0xa8)
#define	IDMAC_CHA_PRI		__REG(PHYS_IPU + 0xac)
#define	IDMAC_CHA_BUSY		__REG(PHYS_IPU + 0xb0)

#define IPU_CONF_CSI_EN		(1<<0)
#define IPU_CONF_IC_EN		(1<<1)
#define IPU_CONF_PXL_ENDIAN	(1<<8)

#define IDMAC_CONF_PRYM_RR		(0<<0) /* Round robin */
#define IDMAC_CONF_PRYM_RAND		(1<<0) /* Random */
#define IDMAC_CONF_SRCNT_SH		(4)
#define IDMAC_CONF_SRCNT_MSK		(0x7)
#define IDMAC_CONF_SINGLE_AHB_M_EN	(1<<8)

#define CSI_CONF_VSYNC_POL		(1<<0)
#define CSI_CONF_HSYNC_POL		(1<<1)
#define CSI_CONF_DATA_POL		(1<<2)
#define CSI_CONF_SENS_PIX_CLK_POL	(1<<3)
#define CSI_CONF_SENS_PRTCL_SH		(4)
#define CSI_CONF_SENS_PRTCL_GATED	(0<<4)
#define CSI_CONF_SENS_PRTCL_NOGATED	(1<<4)
#define CSI_CONF_SENS_PRTCL_CCIR_P	(2<<4)
#define CSI_CONF_SENS_PRTCL_CCIR_NP	(3<<4)
#define CSI_CONF_SENS_CLK_SRC		(1<<7)
#define CSI_CONF_SENS_DATA_FORMAT_RGB	(0<<8)
#define CSI_CONF_SENS_DATA_FORMAT_YUV444 (0<<8)
#define CSI_CONF_SENS_DATA_FORMAT_YUV422 (2<<8)
#define CSI_CONF_SENS_DATA_FORMAT_BAYER (3<<8)
#define CSI_CONF_SENS_DATA_FORMAT_GENERIC (3<<8)
#define CSI_CONF_DATA_WIDTH_4BIT	(0<<10)
#define CSI_CONF_DATA_WIDTH_8BIT	(1<<10)
#define CSI_CONF_DATA_WIDTH_10BIT	(2<<10)
#define CSI_CONF_DATA_WIDTH_15BIT	(3<<10)	/* Bayer or generic */
#define CSI_CONF_EXT_VSYNC		(1<<15)
#define CSI_CONF_DIV_RATIO_SH		(16)

#define CSI_FRM_SIZE_WIDTH_SH		(0)
#define CSI_FRM_SIZE_HEIGHT_SH		(16)
#define CSI_FRM_SIZE_WIDTH_MSK		(0xfff)
#define CSI_FRM_SIZE_HEIGHT_MSK		(0xfff)

#define IC_CONF_CSI_MEM_WR_EN		(1<<31)
#define IC_CONF_CSI_RWS_EN		(1<<30)
#define IC_CONF_IC_KEY_COLOR_EN		(1<<29)
#define IC_CONF_IC_GLB_LOC_A		(1<<28)
#define IC_CONF_PP_PROT_EN		(1<<20)
#define IC_CONF_PP_CMB			(1<<19)
#define IC_CONF_PP_CSC2			(1<<18)
#define IC_CONF_PP_CSC1			(1<<17)
#define IC_CONF_PP_EN			(1<<16)
#define IC_CONF_PRPVF_ROT_EN		(1<<12)
#define IC_CONF_PRPVF_CMB		(1<<11)
#define IC_CONF_PRPVF_CSC2		(1<<10)
#define IC_CONF_PRPVF_CSC1		(1<<9)
#define IC_CONF_PRPVF_PRPVF_EN		(1<<8)
#define IC_CONF_PRPENC_ROT_EN		(1<<2)
#define IC_CONF_PRPENC_CSC1		(1<<1)
#define IC_CONF_PRPENC_EN		(1<<0)


static compose_control_word (char* rgb, unsigned long value,
			     int width, int shift)
{
	/* Cope with leader */
  while (shift > 8) {
    ++rgb;
    shift -= 8;
  }

  while (width > 0) {
    int avail = width;
    unsigned long mask;
    if (avail > 8 - shift)
      avail = 8 - shift;
    mask = ((1 << avail) - 1);
    printf ("writing 0x%x, mask 0x%x with shift %d\n",
	    value & mask, mask, shift);
    *rgb++ |= (value & mask) << shift;
    width -= avail;
    shift = 0;			/* Always zero after first byte */
  }
}

#if 0
static const char* i2c_status_decode (int sr)
{
  static char sz[80];
  *sz = 0;
  if (sr & I2C_I2SR_ICF)
    strcat (sz, " ICF");
  if (sr & I2C_I2SR_IAAS)
    strcat (sz, " IAAS");
  if (sr & I2C_I2SR_IBB)
    strcat (sz, " IBB");
  if (sr & I2C_I2SR_IAL)
    strcat (sz, " IAL");
  if (sr & I2C_I2SR_SRW)
    strcat (sz, " SRW");
  if (sr & I2C_I2SR_IIF)
    strcat (sz, " IIF");
  if (sr & I2C_I2SR_RXAK)
    strcat (sz, " RXAK");

  return sz;
}
#endif

static void i2c_stop (void)
{
  ENTRY;

  I2C_I2CR &= ~(I2C_I2CR_MSTA | I2C_I2CR_MTX);
  I2C_I2SR = 0;

  while (I2C_I2SR & I2C_I2SR_IBB) 	/* Wait for STOP condition */
    ;
}

static void i2c_setup (void)
{
  ENTRY;

  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_I2C_CLK);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_I2C_DAT);

  I2C_IFDR = I2C_IFDR_V;

  I2C_I2SR = 0;
  i2c_stop ();
  I2C_I2CR = I2C_I2CR_IEN;	/* Enable I2C */
}

static void i2c_setup_sensor_i2c (void)
{
  ENTRY;

  IOMUX_PIN_CONFIG_GPIO	 (PIN_SENSOR_PWR_EN);
  GPIO_PIN_CONFIG_OUTPUT (PIN_SENSOR_PWR_EN);
  GPIO_PIN_SET		 (PIN_SENSOR_PWR_EN);	/* Enable sensor power */

  IOMUX_PIN_CONFIG_GPIO  (PIN_SENSOR_BUF_EN);
  GPIO_PIN_CONFIG_OUTPUT (PIN_SENSOR_BUF_EN);
  GPIO_PIN_SET		 (PIN_SENSOR_BUF_EN);	/* Enable sensor signal bufr */

  usleep (1000);		/* delay 1ms while camera powers on */

  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_PIXCLK);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_MCLK);

	/* Post divider for CSI. */
  MASK_AND_SET (CCM_PDR0, (0x1ff << 23), (0x58 << 23));
}

static void ipu_setup_sensor (void)
{
  ENTRY;

  IOMUX_PIN_CONFIG_GPIO  (PIN_CMOS_STBY);
  GPIO_PIN_CONFIG_OUTPUT (PIN_CMOS_STBY);
  GPIO_PIN_SET		 (PIN_CMOS_STBY);	/* Put camera in standby */

  IOMUX_PIN_CONFIG_GPIO  (PIN_NCMOS_RESET);
  GPIO_PIN_CONFIG_OUTPUT (PIN_NCMOS_RESET);
  GPIO_PIN_SET		 (PIN_NCMOS_RESET);	/* Prep camera reset signal */

  IOMUX_PIN_CONFIG_GPIO  (PIN_CMOS_EXPOSURE);
  GPIO_PIN_CONFIG_OUTPUT (PIN_CMOS_EXPOSURE);
  GPIO_PIN_CLEAR	 (PIN_CMOS_EXPOSURE);

  IOMUX_PIN_CONFIG_GPIO  (PIN_ILLUMINATION_EN1);
  GPIO_PIN_CONFIG_OUTPUT (PIN_ILLUMINATION_EN1);
  GPIO_PIN_SET		 (PIN_ILLUMINATION_EN1);

  IOMUX_PIN_CONFIG_GPIO  (PIN_ILLUMINATION_EN2);
  GPIO_PIN_CONFIG_OUTPUT (PIN_ILLUMINATION_EN2);
  GPIO_PIN_SET		 (PIN_ILLUMINATION_EN2);

  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_D15);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_D14);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_D13);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_D12);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_D11);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_D10);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_D9);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_D8);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_D7);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_D6);

  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_VSYNC);
  IOMUX_PIN_CONFIG_FUNC  (MX31_PIN_CSI_HSYNC);

  GPIO_PIN_CLEAR	 (PIN_NCMOS_RESET);
  udelay (1);
  GPIO_PIN_SET		 (PIN_NCMOS_RESET);
  udelay (1);
  GPIO_PIN_CLEAR	 (PIN_CMOS_STBY); /* Release camera from standby */
}


static void ipu_setup (void)
{
  /* Configure common parameters */

  IPU_CONF	= 0;				/* Setting endian-ness only */
  IDMAC_CONF	= IDMAC_CONF_SINGLE_AHB_M_EN;

  /* SYSTEM Configure sensor interface */

  CSI_CONF	= 0
    | CSI_CONF_SENS_PRTCL_GATED
    | CSI_CONF_SENS_DATA_FORMAT_GENERIC
    | CSI_CONF_DATA_WIDTH_15BIT
    | CSI_CONF_EXT_VSYNC
    ;

  CSI_SENS_FRM_SIZE = 0
    | (FRAME_WIDTH  << CSI_FRM_SIZE_WIDTH_SH)
    | (FRAME_HEIGHT << CSI_FRM_SIZE_HEIGHT_SH);


  /* TASK Configure and initialize CSI IC IDMAC */

  CSI_ACT_FRM_SIZE = 0
    | (FRAME_WIDTH  << CSI_FRM_SIZE_WIDTH_SH)
    | (FRAME_HEIGHT << CSI_FRM_SIZE_HEIGHT_SH)
    ;

				/* IC Task options */
  IC_CONF = 0
    | IC_CONF_CSI_MEM_WR_EN	/* Allow direct write from CSI to memory */
    ;

	/* Initialize IDMAC register file */
  {
    char rgb[(132 + 7)/8];
    memset (rgb, 0, sizeof (rgb));

    compose_control_word (rgb, 0, 10, 0);	/* XV */
    compose_control_word (rgb, 0, 10, 10);	/* YV */
  }
  ipu_write_ima (1, 7*(0 + 1), 0,
		 0
		 | 0 << 0	/* XV */
		 | 0 << 10	/* YV */
		 | 0 << 20	/* XB */
		 );
  ipu_write_ima (1, 7*(0 + 1), 0,
		 0
		 | 0 << 0	/* XV */
		 | 0 << 10	/* YV */
		 | 0 << 20	/* XB */
		 );


		regData = 0;
		regData |= (params->XV << 0);
		regData |= (params->YV << 10);
		regData |= (params->XB << 20);
		writeIPUIntMem(IPU_IMA_CH_PAR_MEM, 2*channel, 0, regData);

		regData = 0;
		regData |= (params->YB << 0);
		regData |= (params->NSB << 14);
		regData |= (params->LNPB << 15);
		regData |= ((params->UBO & 0x7FF)  << 21);
		writeIPUIntMem(IPU_IMA_CH_PAR_MEM, 2*channel, 1, regData);

		regData = 0;
		regData |= (params->UBO  >> 11);
		regData |= ((params->VBO & 0x3FFFF) << 15);
		writeIPUIntMem(IPU_IMA_CH_PAR_MEM, 2*channel, 2, regData);

		regData = 0;
		regData |= (params->VBO >> 18);
		regData |= (params->FW <<  12);
		regData |= ((params->FH & 0xFF) << 24);
		writeIPUIntMem(IPU_IMA_CH_PAR_MEM, 2*channel, 3, regData);

		regData = 0;
		regData |= (params->FH >> 8);
		writeIPUIntMem(IPU_IMA_CH_PAR_MEM, 2*channel, 4, regData);

		regData = 0;
		regData |= (params->EBA0 << 0);
		writeIPUIntMem(IPU_IMA_CH_PAR_MEM, 2*channel+1, 0, regData);

		regData = 0;
		regData |= (params->EBA1 << 0);
		writeIPUIntMem(IPU_IMA_CH_PAR_MEM, 2*channel+1, 1, regData);

		regData = 0;
		regData |= (params->BPP << 0);
		regData |= (params->SL  << 3);
		regData |= (params->PFS << 17);
		writeIPUIntMem(IPU_IMA_CH_PAR_MEM, 2*channel+1, 2, regData);






  IDMAC_CHA_PRI |= 1<<7;	/* Channel 7 is high priority */


  /* Initialize sensors */

	/* Initialize sensor via i2c if needed. */

  /* Enabling tasks */

  IPU_CONF	= IPU_CONF_CSI_EN | IPU_CONF_IC_EN;
  IDMAC_CHA_EN |= 1<<7;	/* Enable channel 7 */

  /* Resuming tasks */

  /* Reconfiguring and resuming tasks */

  /* Disabling tasks */
}

/* i2c_wait_for_interrupt

   waits for an interrupt and returns the status.

*/

static int i2c_wait_for_interrupt (void)
{
  ENTRY;
  while (!(I2C_I2SR & I2C_I2SR_IIF))	/* Wait for interrupt */
    ;

  I2C_I2SR = 0;				/* Only IIF can be cleared */
  return I2C_I2SR;
}


/* i2c_start

   sends the slave the frame START and target address.  Return value
   is 0 on success.

   *** FIXME: the check for IAL isn't really necessary since this is
   *** really a slave issue, I think.

*/

static int i2c_start (char address, int reading)
{
  ENTRY;
  address &= 0x7f;

  I2C_I2CR |= I2C_I2CR_MSTA;	/* Acquire bus */
  do {
    int sr = I2C_I2SR;
    if (sr & I2C_I2SR_IBB)	/* Arbitration won */
      break;
    if (sr & I2C_I2SR_IAL)	/* Arbitration lost */
      return 1;
    usleep (10);
  } while (1);
  I2C_I2SR = 0;
  I2C_I2CR |= I2C_I2CR_MTX;	/* Prepare for transmit */
  I2C_I2DR = (address << 1) | (reading ? 1 : 0);

				/* Address must be ACK'd */
  return (i2c_wait_for_interrupt () & I2C_I2SR_RXAK) ? 1 : 0;
}


/* i2c_write

   writes the buffer to the given address.  It returns 0 on success.

*/

static int i2c_write (char address, char* rgb, int cb)
{
  int i;
  ENTRY;
  if (I2C_I2SR & I2C_I2SR_IBB) {
    printf ("error: i2c bus busy %d\n", address);
    i2c_stop ();
    return 1;
  }

  if (i2c_start (address, 0)) {
    i2c_stop ();
    return 1;
  }

  I2C_I2CR |= I2C_I2CR_MTX;
  for (i = 0; i < cb; ++i) {
    I2C_I2DR = *rgb++;
    if (i2c_wait_for_interrupt () & I2C_I2SR_RXAK) {
      i2c_stop ();
      return 1;
    }
  }

  i2c_stop ();
  return 0;
}


static int cmd_ipu (int argc, const char** argv)
{
  ENTRY;
  if (argc < 2)
    return ERROR_PARAM;

  if (strcmp (argv[1], "i") == 0) {
    i2c_setup_sensor_i2c ();
    i2c_setup ();
    ipu_setup_sensor ();
  }

  if (strcmp (argv[1], "i2c") == 0) {
    int address;
    i2c_setup_sensor_i2c ();
    i2c_setup ();
    for (address = 0; address < 128; ++address) {
      char rgb[2] = { 0, 0 };
      int result = i2c_write (address, rgb, 2);
      if (result == 0)
	printf ("I2C device found at address %d (0x%x)\n", address, address);
    }
  }

  return 0;
}

static __command struct command_d c_ipu = {
  .command = "ipu",
  .func = (command_func_t) cmd_ipu,
  COMMAND_DESCRIPTION ("ipu test")
  COMMAND_HELP(
"ipu\n"
"  IPU tests.\n"
  )
};
