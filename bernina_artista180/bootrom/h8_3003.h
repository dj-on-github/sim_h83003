/* On-chip registers of the H8/3003, modes 3/4.
 *
 * Names and addresses from appendix B.1 of the hardware manual. Only the
 * registers the boot ROM touches are declared; the rest are in the
 * simulator's tool/h8_regmap.dart if more are needed.
 */
#ifndef H8_3003_H
#define H8_3003_H

typedef unsigned char  u8;
typedef unsigned short u16;
typedef unsigned long  u32;

#define REG8(a)  (*(volatile u8 *)(a))
#define REG16(a) (*(volatile u16 *)(a))
#define REG32(a) (*(volatile u32 *)(a))

/* Bus controller */
#define ABWCR  REG8(0xFFFFECUL)   /* bus width */
#define ASTCR  REG8(0xFFFFEDUL)   /* access state */
#define WCR    REG8(0xFFFFEEUL)   /* wait control */
#define WCER   REG8(0xFFFFEFUL)   /* wait enable */
#define BRCR   REG8(0xFFFFF3UL)   /* bus release */

/* I/O ports */
/* A/D converter. The result registers are ten bits in a pair, but every
 * reading here is taken from the high byte alone. */
#define ADDRAH REG8(0xFFFFE0UL)
#define ADDRBH REG8(0xFFFFE2UL)
#define ADDRCH REG8(0xFFFFE4UL)
#define ADDRDH REG8(0xFFFFE6UL)
#define ADCSR  REG8(0xFFFFE8UL)
#define ADCSR_ADF   0x80   /* conversion finished */
#define ADCSR_START 0x28   /* ADST | SCAN */

#define P4DDR  REG8(0xFFFFC5UL)
#define P4DR   REG8(0xFFFFC7UL)
#define P4PCR  REG8(0xFFFFDAUL)   /* input pull-up control */
#define P5DDR  REG8(0xFFFFC8UL)
#define P6DDR  REG8(0xFFFFC9UL)
#define P5DR   REG8(0xFFFFCAUL)
#define P6DR   REG8(0xFFFFCBUL)
#define P8DDR  REG8(0xFFFFCDUL)
#define P8DR   REG8(0xFFFFCFUL)
#define P9DDR  REG8(0xFFFFD0UL)
#define P9DR   REG8(0xFFFFD2UL)
#define PBDDR  REG8(0xFFFFD4UL)
#define PBDR   REG8(0xFFFFD6UL)
#define PCDDR  REG8(0xFFFFD5UL)
#define PCDR   REG8(0xFFFFD7UL)

/* Integrated timer unit. Three of its five channels are used. */
#define TSTR   REG8(0xFFFF60UL)
#define TSNC   REG8(0xFFFF61UL)
#define TMDR   REG8(0xFFFF62UL)
#define TCR0   REG8(0xFFFF64UL)
#define TIOR0  REG8(0xFFFF65UL)
#define TIER0  REG8(0xFFFF66UL)
#define TCNT0  REG16(0xFFFF68UL)
#define GRA0   REG16(0xFFFF6AUL)
#define GRB0   REG16(0xFFFF6CUL)
#define TCR1   REG8(0xFFFF6EUL)
#define TIOR1  REG8(0xFFFF6FUL)
#define TIER1  REG8(0xFFFF70UL)
#define TCNT1  REG16(0xFFFF72UL)
#define GRA1   REG16(0xFFFF74UL)
#define GRB1   REG16(0xFFFF76UL)
#define TCR2   REG8(0xFFFF78UL)
#define TIOR2  REG8(0xFFFF79UL)
#define TIER2  REG8(0xFFFF7AUL)
#define TCNT2  REG16(0xFFFF7CUL)
#define GRA2   REG16(0xFFFF7EUL)
#define GRB2   REG16(0xFFFF80UL)
#define TFCR   REG8(0xFFFF63UL)
#define TOER   REG8(0xFFFF90UL)
#define TCR4   REG8(0xFFFF92UL)
#define TIOR4  REG8(0xFFFF93UL)
#define TIER4  REG8(0xFFFF94UL)
#define TCNT4  REG16(0xFFFF96UL)
#define GRA4   REG16(0xFFFF98UL)
#define GRB4   REG16(0xFFFF9AUL)

/* Programmable timing pattern controller: the port A and port B pins it
 * drives are the stepper motor phases. */
#define TPMR   REG8(0xFFFFA0UL)
#define TPCR   REG8(0xFFFFA1UL)
#define NDERB  REG8(0xFFFFA2UL)
#define NDERA  REG8(0xFFFFA3UL)
#define NDRB   REG8(0xFFFFA4UL)
#define NDRA   REG8(0xFFFFA5UL)

#define PADDR  REG8(0xFFFFD1UL)
#define PADR   REG8(0xFFFFD3UL)

/* Serial channel 0 */
#define SMR0   REG8(0xFFFFB0UL)
#define BRR0   REG8(0xFFFFB1UL)
#define SCR0   REG8(0xFFFFB2UL)
#define TDR0   REG8(0xFFFFB3UL)
#define SSR0   REG8(0xFFFFB4UL)
#define RDR0   REG8(0xFFFFB5UL)

/* Serial channel 1 */
#define SMR1   REG8(0xFFFFB8UL)
#define BRR1   REG8(0xFFFFB9UL)
#define SCR1   REG8(0xFFFFBAUL)
#define TDR1   REG8(0xFFFFBBUL)
#define SSR1   REG8(0xFFFFBCUL)
#define RDR1   REG8(0xFFFFBDUL)

/* SSR bits */
#define SSR_TDRE 0x80   /* transmit data register empty */
#define SSR_RDRF 0x40   /* receive data register full */
#define SSR_ORER 0x20   /* overrun error */
#define SSR_FER  0x10   /* framing error */
#define SSR_PER  0x08   /* parity error */
#define SSR_RX_ERRORS (SSR_ORER | SSR_FER | SSR_PER)

/* SCR bits */
#define SCR_TE   0x20   /* transmit enable */
#define SCR_RE   0x10   /* receive enable */

/* Boot ROM state, in on-chip RAM.
 *
 * CHAN_SELECTION bit 1 chooses which serial channel every routine below
 * talks to: clear selects SCI1, set selects SCI0. Bit 0 is set on the
 * failure path and cleared on the success path.
 */
#define BOOT_VECTOR_SCRATCH 0xFFFD18UL  /* dispatch instruction built here */
#define CHAN_SELECTION      REG8(0xFFFD1CUL)

/* Bit 1 of CHAN_SELECTION: set picks SCI0, clear picks SCI1. */
#define CHAN_SCI0           0x02
#define CHAN_SEL_PORT0      0x02
#define BOOT_FAILED_FLAG    0x01
#define BOOT_STATE_1E       REG8(0xFFFD1EUL)  /* protocol state */
#define BOOT_STATE_1F       REG8(0xFFFD1FUL)
#define BOOT_ADDR_ACC       REG32(0xFFFD14UL) /* hex digits accumulate here */
#define BOOT_DATA_20        REG8(0xFFFD20UL)
#define BOOT_PTR_10         REG32(0xFFFD10UL)

/* The application, in flash. Its first longword is both the entry point and
 * the first slot of the interrupt handler table the trampolines read. */
/* Data transfer controller, channel 0. The boot ROM uses it to move flash
 * pages to and from the RAM buffer rather than copying them with the CPU. */
#define MAR0A               REG32(0xFFFF20UL)   /* source */
#define ETCR0A              REG16(0xFFFF24UL)   /* count */
#define DTCR0A              REG8(0xFFFF27UL)
#define MAR0B               REG32(0xFFFF28UL)   /* destination */
#define DTCR0B              REG8(0xFFFF2FUL)

#define DTE                 0x80                /* transfer enable, in both */

/* Address of the 256-byte page buffer the flash routines work through. */
#define FLASH_PAGE_BUF      REG32(0xFFFD10UL)

#define APP_TABLE           0x200000UL
#define APP_ENTRY           REG32(APP_TABLE)
#define APP_ENTRY_ALT       REG32(APP_TABLE + 4)

#endif /* H8_3003_H */
