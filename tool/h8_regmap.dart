// On-chip register names for the H8/3003, keyed by the low byte of the
// address (the registers occupy H'FFFF1C-H'FFFFFF in modes 3/4).
//
// Transcribed from appendix B.1 "Register Addresses and Bit Names" of the
// Hitachi H8/3003 hardware manual. The SCI and port register names are
// suffixed with their channel number where the manual reuses one name for
// both channels.

const Map<int, String> h8Registers = {
  0x20: 'MAR0AR', 0x21: 'MAR0AE', 0x22: 'MAR0AH', 0x23: 'MAR0AL',
  0x24: 'ETCR0AH', 0x25: 'ETCR0AL', 0x26: 'IOAR0A', 0x27: 'DTCR0A',
  0x28: 'MAR0BR', 0x29: 'MAR0BE', 0x2A: 'MAR0BH', 0x2B: 'MAR0BL',
  0x2C: 'ETCR0BH', 0x2D: 'ETCR0BL', 0x2E: 'IOAR0B', 0x2F: 'DTCR0B',
  0x30: 'MAR1AR', 0x31: 'MAR1AE', 0x32: 'MAR1AH', 0x33: 'MAR1AL',
  0x34: 'ETCR1AH', 0x35: 'ETCR1AL', 0x36: 'IOAR1A', 0x37: 'DTCR1A',
  0x38: 'MAR1BR', 0x39: 'MAR1BE', 0x3A: 'MAR1BH', 0x3B: 'MAR1BL',
  0x3C: 'ETCR1BH', 0x3D: 'ETCR1BL', 0x3E: 'IOAR1B', 0x3F: 'DTCR1B',
  0x40: 'MAR2AR', 0x41: 'MAR2AE', 0x42: 'MAR2AH', 0x43: 'MAR2AL',
  0x44: 'ETCR2AH', 0x45: 'ETCR2AL', 0x46: 'IOAR2A', 0x47: 'DTCR2A',
  0x48: 'MAR2BR', 0x49: 'MAR2BE', 0x4A: 'MAR2BH', 0x4B: 'MAR2BL',
  0x4C: 'ETCR2BH', 0x4D: 'ETCR2BL', 0x4E: 'IOAR2B', 0x4F: 'DTCR2B',
  0x50: 'MAR3AR', 0x51: 'MAR3AE', 0x52: 'MAR3AH', 0x53: 'MAR3AL',
  0x54: 'ETCR3AH', 0x55: 'ETCR3AL', 0x56: 'IOAR3A', 0x57: 'DTCR3A',
  0x58: 'MAR3BR', 0x59: 'MAR3BE', 0x5A: 'MAR3BH', 0x5B: 'MAR3BL',
  0x5C: 'ETCR3BH', 0x5D: 'ETCR3BL', 0x5E: 'IOAR3B', 0x5F: 'DTCR3B',
  0x60: 'TSTR', 0x61: 'TSNC', 0x62: 'TMDR', 0x63: 'TFCR',
  0x64: 'TCR0', 0x65: 'TIOR0', 0x66: 'TIER0', 0x67: 'TSR0',
  0x68: 'TCNT0H', 0x69: 'TCNT0L', 0x6A: 'GRA0H', 0x6B: 'GRA0L',
  0x6C: 'GRB0H', 0x6D: 'GRB0L',
  0x6E: 'TCR1', 0x6F: 'TIOR1', 0x70: 'TIER1', 0x71: 'TSR1',
  0x72: 'TCNT1H', 0x73: 'TCNT1L', 0x74: 'GRA1H', 0x75: 'GRA1L',
  0x76: 'GRB1H', 0x77: 'GRB1L',
  0x78: 'TCR2', 0x79: 'TIOR2', 0x7A: 'TIER2', 0x7B: 'TSR2',
  0x7C: 'TCNT2H', 0x7D: 'TCNT2L', 0x7E: 'GRA2H', 0x7F: 'GRA2L',
  0x80: 'GRB2H', 0x81: 'GRB2L',
  0x82: 'TCR3', 0x83: 'TIOR3', 0x84: 'TIER3', 0x85: 'TSR3',
  0x86: 'TCNT3H', 0x87: 'TCNT3L', 0x88: 'GRA3H', 0x89: 'GRA3L',
  0x8A: 'GRB3H', 0x8B: 'GRB3L', 0x8C: 'BRA3H', 0x8D: 'BRA3L',
  0x8E: 'BRB3H', 0x8F: 'BRB3L',
  0x90: 'TOER', 0x91: 'TOCR',
  0x92: 'TCR4', 0x93: 'TIOR4', 0x94: 'TIER4', 0x95: 'TSR4',
  0x96: 'TCNT4H', 0x97: 'TCNT4L', 0x98: 'GRA4H', 0x99: 'GRA4L',
  0x9A: 'GRB4H', 0x9B: 'GRB4L', 0x9C: 'BRA4H', 0x9D: 'BRA4L',
  0x9E: 'BRB4H', 0x9F: 'BRB4L',
  0xA0: 'TPMR', 0xA1: 'TPCR', 0xA2: 'NDERB', 0xA3: 'NDERA',
  0xA4: 'NDRB', 0xA5: 'NDRA', 0xA8: 'WDT_TCSR', 0xA9: 'WDT_TCNT',
  0xAC: 'RFSHCR', 0xAD: 'RTMCSR', 0xAE: 'RTCNT', 0xAF: 'RTCOR',
  0xB0: 'SMR0', 0xB1: 'BRR0', 0xB2: 'SCR0', 0xB3: 'TDR0',
  0xB4: 'SSR0', 0xB5: 'RDR0',
  0xB8: 'SMR1', 0xB9: 'BRR1', 0xBA: 'SCR1', 0xBB: 'TDR1',
  0xBC: 'SSR1', 0xBD: 'RDR1',
  0xC5: 'P4DDR', 0xC7: 'P4DR', 0xC8: 'P5DDR', 0xC9: 'P6DDR',
  0xCA: 'P5DR', 0xCB: 'P6DR', 0xCD: 'P8DDR', 0xCE: 'P7DR',
  0xCF: 'P8DR', 0xD0: 'P9DDR', 0xD1: 'PADDR', 0xD2: 'P9DR',
  0xD3: 'PADR', 0xD4: 'PBDDR', 0xD5: 'PCDDR', 0xD6: 'PBDR',
  0xD7: 'PCDR', 0xDA: 'P4PCR', 0xDB: 'P5PCR',
  0xE0: 'ADDRAH', 0xE1: 'ADDRAL', 0xE2: 'ADDRBH', 0xE3: 'ADDRBL',
  0xE4: 'ADDRCH', 0xE5: 'ADDRCL', 0xE6: 'ADDRDH', 0xE7: 'ADDRDL',
  0xE8: 'ADCSR', 0xE9: 'ADCR',
  0xEC: 'ABWCR', 0xED: 'ASTCR', 0xEE: 'WCR', 0xEF: 'WCER',
  0xF1: 'MDCR', 0xF2: 'SYSCR', 0xF3: 'BRCR', 0xF4: 'ISCR',
  0xF5: 'IER', 0xF6: 'ISR', 0xF8: 'IPRA', 0xF9: 'IPRB',
};

/// The peripheral module a register belongs to, for grouping in reports.
String h8Module(int lo) {
  if (lo >= 0x20 && lo <= 0x5F) return 'DMAC';
  if (lo >= 0x60 && lo <= 0x9F) return 'ITU (16-bit timers)';
  if (lo >= 0xA0 && lo <= 0xA5) return 'TPC (pattern controller)';
  if (lo >= 0xA8 && lo <= 0xA9) return 'Watchdog timer';
  if (lo >= 0xAC && lo <= 0xAF) return 'Refresh controller';
  if (lo >= 0xB0 && lo <= 0xB7) return 'SCI0 (serial)';
  if (lo >= 0xB8 && lo <= 0xBF) return 'SCI1 (serial)';
  if (lo >= 0xC0 && lo <= 0xDF) return 'I/O ports (GPIO)';
  if (lo >= 0xE0 && lo <= 0xE9) return 'A/D converter';
  if (lo >= 0xEC && lo <= 0xEF) return 'Bus controller';
  if (lo >= 0xF1 && lo <= 0xF9) return 'System / interrupt control';
  return 'On-chip (unassigned)';
}

/// Exception vector names, by vector number (tables 4-2 and 5-3).
const Map<int, String> h8Vectors = {
  0: 'RESET', 7: 'NMI',
  8: 'TRAPA0', 9: 'TRAPA1', 10: 'TRAPA2', 11: 'TRAPA3',
  12: 'IRQ0', 13: 'IRQ1', 14: 'IRQ2', 15: 'IRQ3',
  16: 'IRQ4', 17: 'IRQ5', 18: 'IRQ6', 19: 'IRQ7',
  20: 'WOVI_watchdog',
  24: 'IMIA0', 25: 'IMIB0', 26: 'OVI0',
  28: 'IMIA1', 29: 'IMIB1', 30: 'OVI1',
  32: 'IMIA2', 33: 'IMIB2', 34: 'OVI2',
  36: 'IMIA3', 37: 'IMIB3', 38: 'OVI3',
  40: 'IMIA4', 41: 'IMIB4', 42: 'OVI4',
  44: 'DEND0A', 45: 'DEND0B', 46: 'DEND1A', 47: 'DEND1B',
  48: 'DEND2A', 49: 'DEND2B', 50: 'DEND3A', 51: 'DEND3B',
  52: 'ERI0', 53: 'RXI0', 54: 'TXI0', 55: 'TEI0',
  56: 'ERI1', 57: 'RXI1', 58: 'TXI1', 59: 'TEI1',
  60: 'ADI',
};
