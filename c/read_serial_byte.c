

typedef uint8_t uint8_gcc __attribute__((mode(QI)));
typedef uint16_t uint16_gcc __attribute__((mode(HI)));
typedef uint32_t uint32_gcc __attribute__((mode(SI)));

unsigned char read_serial_data_byte(void) // read_serial_data_byte()
{
unsigned char *RDR0 = (unsigned char *)0xFFFFB5;
unsigned char *RDR1 = (unsigned char *)0xFFFFBD;
unsigned char *SSR0 = (unsigned char *)0xFFFFB4;
unsigned char *SSR1 = (unsigned char *)0xFFFFBC;

     unsigned char x  = *(unsigned char *)0xFFFD1C;
     unsigned char c;

     if (!(x & 2)) {       // Serial port 1 selected in FFFD1C:2
        c = *RDR1;        // Read received byte from serial data reg
        *SSR1 &= ~(1 << 6); // Clear full flag in serial status reg
        return c;
     }
     else {                // Serial port 0 selected in FFFD1C:2
        c = *RDR0;
        *SSR0 &= ~(1 << 6);
        return c;
     }
}



