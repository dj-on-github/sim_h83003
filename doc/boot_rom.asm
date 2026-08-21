; Bernina180_20260816.bin — flat binary, 4063232 bytes loaded  H'000000-H'FFFFFF
; 1754 symbols
;

boot_reset:
  H'000400  7A 07 00 FF FD F4         MOV.L #H'00FFFDF4,ER7
  H'000406  5E 00 20 A6               JSR @sub_0020A6:24

sub_00040A:
  H'00040A  04 80                     ORC #H'80,CCR
  H'00040C  54 70                     RTS

sub_00040E:
  H'00040E  06 7F                     ANDC #H'7F,CCR
  H'000410  54 70                     RTS

sub_000412:
  H'000412  01 00 6B 20 00 20 00 00   MOV.L @H'200000:24,ER0
  H'00041A  59 00                     JMP @ER0

sub_00041C:
  H'00041C  01 00 6B 20 00 20 00 04   MOV.L @H'200004:24,ER0
  H'000424  59 00                     JMP @ER0

tramp_vec2:
  H'000426  01 00 6D F4               PUSH.L ER4
  H'00042A  6D F6                     PUSH.W R6
  H'00042C  19 44                     SUB.W R4,R4
  H'00042E  69 76                     MOV.W @ER7,R6
  H'000430  1D 64                     CMP.W R6,R4
  H'000432  47 10                     BEQ H'000444
  H'000434  19 CC                     SUB.W E4,E4
  H'000436  79 2C 01 44               CMP.W #H'0144,E4
  H'00043A  47 04                     BEQ H'000440
  H'00043C  0B 5C                     INC.W #1,E4
  H'00043E  40 F6                     BRA H'000436
  H'000440  0B 54                     INC.W #1,R4
  H'000442  40 EA                     BRA H'00042E
  H'000444  0B 87                     ADDS #2,ER7
  H'000446  01 00 6D 74               POP.L ER4
  H'00044A  54 70                     RTS

read_serial_data_byte:
  H'00044C  6D F5                     PUSH.W R5
  H'00044E  6A 2E 00 FF FD 1C         MOV.B @CHAN_SELECTION:24,R6L
  H'000454  73 1E                     BTST #1,R6L
  H'000456  46 0C                     BNE H'000464
  H'000458  2D BD                     MOV.B @RDR1:8,R5L
  H'00045A  7F BC 72 60               BCLR #6,@SSR1:8
  H'00045E  0C DE                     MOV.B R5L,R6L
  H'000460  18 66                     SUB.B R6H,R6H
  H'000462  40 0A                     BRA H'00046E
  H'000464  2D B5                     MOV.B @RDR0:8,R5L
  H'000466  7F B4 72 60               BCLR #6,@SSR0:8
  H'00046A  0C DE                     MOV.B R5L,R6L
  H'00046C  18 66                     SUB.B R6H,R6H
  H'00046E  6D 75                     POP.W R5
  H'000470  54 70                     RTS

send_serial_data_byte:
  H'000472  0C E6                     MOV.B R6L,R6H
  H'000474  6A 2E 00 FF FD 1C         MOV.B @CHAN_SELECTION:24,R6L
  H'00047A  73 1E                     BTST #1,R6L
  H'00047C  46 0E                     BNE H'00048C
  H'00047E  7E BC 73 70               BTST #7,@SSR1:8
  H'000482  47 FA                     BEQ H'00047E
  H'000484  36 BB                     MOV.B R6H,@TDR1:8
  H'000486  7F BC 72 70               BCLR #7,@SSR1:8
  H'00048A  40 0C                     BRA H'000498
  H'00048C  7E B4 73 70               BTST #7,@SSR0:8
  H'000490  47 FA                     BEQ H'00048C
  H'000492  36 B3                     MOV.B R6H,@TDR0:8
  H'000494  7F B4 72 70               BCLR #7,@SSR0:8
  H'000498  54 70                     RTS

sub_00049A:
  H'00049A  6A 2E 00 FF FD 1C         MOV.B @CHAN_SELECTION:24,R6L
  H'0004A0  73 1E                     BTST #1,R6L
  H'0004A2  46 24                     BNE H'0004C8
  H'0004A4  2E BC                     MOV.B @SSR1:8,R6L
  H'0004A6  EE 38                     AND.B #H'38,R6L
  H'0004A8  47 18                     BEQ H'0004C2
  H'0004AA  55 A0                     BSR read_serial_data_byte
  H'0004AC  2E BC                     MOV.B @SSR1:8,R6L
  H'0004AE  EE C7                     AND.B #H'C7,R6L
  H'0004B0  3E BC                     MOV.B R6L,@SSR1:8
  H'0004B2  FE 21                     MOV.B #H'21,R6L
  H'0004B4  55 BC                     BSR send_serial_data_byte
  H'0004B6  18 EE                     SUB.B R6L,R6L
  H'0004B8  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0004BE  19 66                     SUB.W R6,R6
  H'0004C0  40 2A                     BRA H'0004EC
  H'0004C2  79 06 00 01               MOV.W #H'0001,R6
  H'0004C6  40 24                     BRA H'0004EC
  H'0004C8  2E B4                     MOV.B @SSR0:8,R6L
  H'0004CA  EE 38                     AND.B #H'38,R6L
  H'0004CC  47 1A                     BEQ H'0004E8
  H'0004CE  5C 00 FF 7A               BSR read_serial_data_byte:16
  H'0004D2  2E B4                     MOV.B @SSR0:8,R6L
  H'0004D4  EE C7                     AND.B #H'C7,R6L
  H'0004D6  3E B4                     MOV.B R6L,@SSR0:8
  H'0004D8  FE 21                     MOV.B #H'21,R6L
  H'0004DA  55 96                     BSR send_serial_data_byte
  H'0004DC  18 EE                     SUB.B R6L,R6L
  H'0004DE  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0004E4  19 66                     SUB.W R6,R6
  H'0004E6  40 04                     BRA H'0004EC
  H'0004E8  79 06 00 01               MOV.W #H'0001,R6
  H'0004EC  54 70                     RTS

serial_clear_rx_errors:
  H'0004EE  6A 2E 00 FF FD 1C         MOV.B @CHAN_SELECTION:24,R6L
  H'0004F4  73 1E                     BTST #1,R6L
  H'0004F6  46 22                     BNE H'00051A
  H'0004F8  2E BC                     MOV.B @SSR1:8,R6L
  H'0004FA  EE 38                     AND.B #H'38,R6L
  H'0004FC  47 16                     BEQ H'000514
  H'0004FE  5C 00 FF 4A               BSR read_serial_data_byte:16
  H'000502  2E BC                     MOV.B @SSR1:8,R6L
  H'000504  EE C7                     AND.B #H'C7,R6L
  H'000506  3E BC                     MOV.B R6L,@SSR1:8
  H'000508  18 EE                     SUB.B R6L,R6L
  H'00050A  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'000510  19 66                     SUB.W R6,R6
  H'000512  40 26                     BRA H'00053A
  H'000514  79 06 00 01               MOV.W #H'0001,R6
  H'000518  40 20                     BRA H'00053A
  H'00051A  2E B4                     MOV.B @SSR0:8,R6L
  H'00051C  EE 38                     AND.B #H'38,R6L
  H'00051E  47 16                     BEQ H'000536
  H'000520  5C 00 FF 28               BSR read_serial_data_byte:16
  H'000524  2E B4                     MOV.B @SSR0:8,R6L
  H'000526  EE C7                     AND.B #H'C7,R6L
  H'000528  3E B4                     MOV.B R6L,@SSR0:8
  H'00052A  18 EE                     SUB.B R6L,R6L
  H'00052C  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'000532  19 66                     SUB.W R6,R6
  H'000534  40 04                     BRA H'00053A
  H'000536  79 06 00 01               MOV.W #H'0001,R6
  H'00053A  54 70                     RTS

get_rx_data_full_bit:
  H'00053C  6D F5                     PUSH.W R5
  H'00053E  6A 2E 00 FF FD 1C         MOV.B @CHAN_SELECTION:24,R6L
  H'000544  73 1E                     BTST #1,R6L
  H'000546  46 16                     BNE H'00055E
  H'000548  7E BC 73 60               BTST #6,@SSR1:8
  H'00054C  46 06                     BNE H'000554
  H'00054E  5C 00 FF 48               BSR sub_00049A:16
  H'000552  40 F4                     BRA H'000548
  H'000554  2D BD                     MOV.B @RDR1:8,R5L
  H'000556  7F BC 72 60               BCLR #6,@SSR1:8
  H'00055A  0C DE                     MOV.B R5L,R6L
  H'00055C  40 14                     BRA H'000572
  H'00055E  7E B4 73 60               BTST #6,@SSR0:8
  H'000562  46 06                     BNE H'00056A
  H'000564  5C 00 FF 32               BSR sub_00049A:16
  H'000568  40 F4                     BRA H'00055E
  H'00056A  2D B5                     MOV.B @RDR0:8,R5L
  H'00056C  7F B4 72 60               BCLR #6,@SSR0:8
  H'000570  0C DE                     MOV.B R5L,R6L
  H'000572  6D 75                     POP.W R5
  H'000574  54 70                     RTS

get_sci_rx_ready_bit:
  H'000576  6A 2E 00 FF FD 1C         MOV.B @CHAN_SELECTION:24,R6L
  H'00057C  73 1E                     BTST #1,R6L
  H'00057E  46 08                     BNE H'000588
  H'000580  2E BC                     MOV.B @SSR1:8,R6L
  H'000582  EE 40                     AND.B #H'40,R6L
  H'000584  18 66                     SUB.B R6H,R6H
  H'000586  40 06                     BRA H'00058E
  H'000588  2E B4                     MOV.B @SSR0:8,R6L
  H'00058A  EE 40                     AND.B #H'40,R6L
  H'00058C  18 66                     SUB.B R6H,R6H
  H'00058E  54 70                     RTS

get_sci_tx_ready_bit:
  H'000590  6A 2E 00 FF FD 1C         MOV.B @CHAN_SELECTION:24,R6L
  H'000596  73 1E                     BTST #1,R6L
  H'000598  46 08                     BNE H'0005A2
  H'00059A  2E BC                     MOV.B @SSR1:8,R6L
  H'00059C  EE 80                     AND.B #H'80,R6L
  H'00059E  18 66                     SUB.B R6H,R6H
  H'0005A0  40 06                     BRA H'0005A8
  H'0005A2  2E B4                     MOV.B @SSR0:8,R6L
  H'0005A4  EE 80                     AND.B #H'80,R6L
  H'0005A6  18 66                     SUB.B R6H,R6H
  H'0005A8  54 70                     RTS

sub_0005AA:
  H'0005AA  6D F5                     PUSH.W R5
  H'0005AC  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'0005B2  0A 0E                     INC.B R6L
  H'0005B4  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0005BA  5C 00 FE 8E               BSR read_serial_data_byte:16
  H'0005BE  0C ED                     MOV.B R6L,R5L
  H'0005C0  0C D5                     MOV.B R5L,R5H
  H'0005C2  AD 30                     CMP.B #H'30,R5L
  H'0005C4  45 08                     BCS H'0005CE
  H'0005C6  A5 39                     CMP.B #H'39,R5H
  H'0005C8  42 04                     BHI H'0005CE
  H'0005CA  8D D0                     ADD.B #H'D0,R5L
  H'0005CC  40 16                     BRA H'0005E4
  H'0005CE  A5 41                     CMP.B #H'41,R5H
  H'0005D0  45 08                     BCS H'0005DA
  H'0005D2  A5 46                     CMP.B #H'46,R5H
  H'0005D4  42 04                     BHI H'0005DA
  H'0005D6  8D C9                     ADD.B #H'C9,R5L
  H'0005D8  40 0A                     BRA H'0005E4
  H'0005DA  18 EE                     SUB.B R6L,R6L
  H'0005DC  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0005E2  F5 3F                     MOV.B #H'3F,R5H
  H'0005E4  0C 5E                     MOV.B R5H,R6L
  H'0005E6  5C 00 FE 88               BSR send_serial_data_byte:16
  H'0005EA  0C DE                     MOV.B R5L,R6L
  H'0005EC  6D 75                     POP.W R5
  H'0005EE  54 70                     RTS

sub_0005F0:
  H'0005F0  6D F4                     PUSH.W R4
  H'0005F2  6D F5                     PUSH.W R5
  H'0005F4  6D F6                     PUSH.W R6
  H'0005F6  6A 2D 00 FF FD 1E         MOV.B @H'FFFD1E:24,R5L
  H'0005FC  0A 0D                     INC.B R5L
  H'0005FE  6A AD 00 FF FD 1E         MOV.B R5L,@H'FFFD1E:24
  H'000604  5C 00 FE 44               BSR read_serial_data_byte:16
  H'000608  0C E5                     MOV.B R6L,R5H
  H'00060A  0C 5C                     MOV.B R5H,R4L
  H'00060C  A5 30                     CMP.B #H'30,R5H
  H'00060E  45 08                     BCS H'000618
  H'000610  AC 39                     CMP.B #H'39,R4L
  H'000612  42 04                     BHI H'000618
  H'000614  85 D0                     ADD.B #H'D0,R5H
  H'000616  40 18                     BRA H'000630
  H'000618  AC 41                     CMP.B #H'41,R4L
  H'00061A  45 08                     BCS H'000624
  H'00061C  AC 46                     CMP.B #H'46,R4L
  H'00061E  42 04                     BHI H'000624
  H'000620  85 C9                     ADD.B #H'C9,R5H
  H'000622  40 0C                     BRA H'000630
  H'000624  6E 7E 00 01               MOV.B @(H'0001:16,ER7),R6L
  H'000628  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'00062E  FC 3F                     MOV.B #H'3F,R4L
  H'000630  0C CE                     MOV.B R4L,R6L
  H'000632  5C 00 FE 3C               BSR send_serial_data_byte:16
  H'000636  0C 5E                     MOV.B R5H,R6L
  H'000638  0B 87                     ADDS #2,ER7
  H'00063A  6D 75                     POP.W R5
  H'00063C  6D 74                     POP.W R4
  H'00063E  54 70                     RTS

sub_000640:
  H'000640  6D F5                     PUSH.W R5
  H'000642  EE 0F                     AND.B #H'0F,R6L
  H'000644  0C ED                     MOV.B R6L,R5L
  H'000646  AD 09                     CMP.B #H'09,R5L
  H'000648  42 08                     BHI H'000652
  H'00064A  8E 30                     ADD.B #H'30,R6L
  H'00064C  5C 00 FE 22               BSR send_serial_data_byte:16
  H'000650  40 06                     BRA H'000658
  H'000652  8E 37                     ADD.B #H'37,R6L
  H'000654  5C 00 FE 1A               BSR send_serial_data_byte:16
  H'000658  6D 75                     POP.W R5
  H'00065A  54 70                     RTS

sub_00065C:
  H'00065C  01 00 6D F3               PUSH.L ER3
  H'000660  01 00 6D F5               PUSH.L ER5
  H'000664  01 00 6D F6               PUSH.L ER6
  H'000668  19 BB                     SUB.W E3,E3
  H'00066A  0D B6                     MOV.W E3,R6
  H'00066C  17 F6                     EXTS.L ER6
  H'00066E  01 00 69 75               MOV.L @ER7,ER5
  H'000672  0A D6                     ADD.L ER5,ER6
  H'000674  68 6B                     MOV.B @ER6,R3L
  H'000676  47 0A                     BEQ H'000682
  H'000678  0C BE                     MOV.B R3L,R6L
  H'00067A  5C 00 FD F4               BSR send_serial_data_byte:16
  H'00067E  0B 5B                     INC.W #1,E3
  H'000680  40 E8                     BRA H'00066A
  H'000682  0B 97                     ADDS #4,ER7
  H'000684  01 00 6D 75               POP.L ER5
  H'000688  01 00 6D 73               POP.L ER3
  H'00068C  54 70                     RTS

sub_00068E:
  H'00068E  01 00 6D F6               PUSH.L ER6
  H'000692  7A 06 00 00 23 41         MOV.L #H'00002341,ER6
  H'000698  55 C2                     BSR sub_00065C
  H'00069A  01 00 6D 76               POP.L ER6
  H'00069E  54 70                     RTS

sub_0006A0:
  H'0006A0  6D F5                     PUSH.W R5
  H'0006A2  0C ED                     MOV.B R6L,R5L
  H'0006A4  6A 2E 00 FF FD 1C         MOV.B @CHAN_SELECTION:24,R6L
  H'0006AA  73 1E                     BTST #1,R6L
  H'0006AC  46 26                     BNE H'0006D4
  H'0006AE  79 06 00 0A               MOV.W #H'000A,R6
  H'0006B2  5C 00 FD 70               BSR tramp_vec2:16
  H'0006B6  18 EE                     SUB.B R6L,R6L
  H'0006B8  3E BA                     MOV.B R6L,@SCR1:8
  H'0006BA  3E B8                     MOV.B R6L,@SMR1:8
  H'0006BC  3D B9                     MOV.B R5L,@BRR1:8
  H'0006BE  79 06 00 01               MOV.W #H'0001,R6
  H'0006C2  5C 00 FD 60               BSR tramp_vec2:16
  H'0006C6  FE 30                     MOV.B #H'30,R6L
  H'0006C8  3E BA                     MOV.B R6L,@SCR1:8
  H'0006CA  79 06 00 0A               MOV.W #H'000A,R6
  H'0006CE  5C 00 FD 54               BSR tramp_vec2:16
  H'0006D2  40 24                     BRA H'0006F8
  H'0006D4  79 06 00 0A               MOV.W #H'000A,R6
  H'0006D8  5C 00 FD 4A               BSR tramp_vec2:16
  H'0006DC  18 EE                     SUB.B R6L,R6L
  H'0006DE  3E B2                     MOV.B R6L,@SCR0:8
  H'0006E0  3E B0                     MOV.B R6L,@SMR0:8
  H'0006E2  3D B1                     MOV.B R5L,@BRR0:8
  H'0006E4  79 06 00 01               MOV.W #H'0001,R6
  H'0006E8  5C 00 FD 3A               BSR tramp_vec2:16
  H'0006EC  FE 30                     MOV.B #H'30,R6L
  H'0006EE  3E B2                     MOV.B R6L,@SCR0:8
  H'0006F0  79 06 00 0A               MOV.W #H'000A,R6
  H'0006F4  5C 00 FD 2E               BSR tramp_vec2:16
  H'0006F8  6D 75                     POP.W R5
  H'0006FA  54 70                     RTS

sub_0006FC:
  H'0006FC  6D F6                     PUSH.W R6
  H'0006FE  18 EE                     SUB.B R6L,R6L
  H'000700  3E BA                     MOV.B R6L,@SCR1:8
  H'000702  3E B8                     MOV.B R6L,@SMR1:8
  H'000704  FE 11                     MOV.B #H'11,R6L
  H'000706  3E B9                     MOV.B R6L,@BRR1:8
  H'000708  79 06 00 01               MOV.W #H'0001,R6
  H'00070C  5C 00 FD 16               BSR tramp_vec2:16
  H'000710  FE 30                     MOV.B #H'30,R6L
  H'000712  3E BA                     MOV.B R6L,@SCR1:8
  H'000714  6D 76                     POP.W R6
  H'000716  54 70                     RTS

sub_000718:
  H'000718  6D F6                     PUSH.W R6
  H'00071A  18 EE                     SUB.B R6L,R6L
  H'00071C  3E B2                     MOV.B R6L,@SCR0:8
  H'00071E  3E B0                     MOV.B R6L,@SMR0:8
  H'000720  FE 11                     MOV.B #H'11,R6L
  H'000722  3E B1                     MOV.B R6L,@BRR0:8
  H'000724  79 06 00 01               MOV.W #H'0001,R6
  H'000728  5C 00 FC FA               BSR tramp_vec2:16
  H'00072C  FE 30                     MOV.B #H'30,R6L
  H'00072E  3E B2                     MOV.B R6L,@SCR0:8
  H'000730  6D 76                     POP.W R6
  H'000732  54 70                     RTS
  H'000734  6D F6                     PUSH.W R6
  H'000736  18 EE                     SUB.B R6L,R6L
  H'000738  3E B2                     MOV.B R6L,@SCR0:8
  H'00073A  6D 76                     POP.W R6
  H'00073C  54 70                     RTS

sub_00073E:
  H'00073E  01 00 6D F6               PUSH.L ER6
  H'000742  7E 27 73 70               BTST #7,@DTCR0A:8
  H'000746  46 FA                     BNE H'000742
  H'000748  01 00 69 76               MOV.L @ER7,ER6
  H'00074C  01 00 6B 86 FF 20         MOV.L ER6,@MAR0AR:16
  H'000752  01 00 6F 76 00 08         MOV.L @(VEC_vec2:16,ER7),ER6
  H'000758  01 00 6B 86 FF 28         MOV.L ER6,@MAR0BR:16
  H'00075E  6F 76 00 0C               MOV.W @(VEC_vec3:16,ER7),R6
  H'000762  6B 86 FF 24               MOV.W R6,@ETCR0AH:16
  H'000766  FE 10                     MOV.B #H'10,R6L
  H'000768  3E 2F                     MOV.B R6L,@DTCR0B:8
  H'00076A  FE 16                     MOV.B #H'16,R6L
  H'00076C  3E 27                     MOV.B R6L,@DTCR0A:8
  H'00076E  7F 2F 72 70               BCLR #7,@DTCR0B:8
  H'000772  7F 2F 70 70               BSET #7,@DTCR0B:8
  H'000776  7F 27 72 70               BCLR #7,@DTCR0A:8
  H'00077A  7F 27 70 70               BSET #7,@DTCR0A:8
  H'00077E  0B 97                     ADDS #4,ER7
  H'000780  54 70                     RTS

sub_000782:
  H'000782  6D F2                     PUSH.W R2
  H'000784  6D F3                     PUSH.W R3
  H'000786  01 00 6D F4               PUSH.L ER4
  H'00078A  6D F5                     PUSH.W R5
  H'00078C  7A 66 00 FF 00 00         AND.L #H'00FF0000,ER6
  H'000792  0F E4                     MOV.L ER6,ER4
  H'000794  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'00079A  68 62                     MOV.B @ER6,R2H
  H'00079C  0F C6                     MOV.L ER4,ER6
  H'00079E  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'0007A4  68 6A                     MOV.B @ER6,R2L
  H'0007A6  68 43                     MOV.B @ER4,R3H
  H'0007A8  0F C6                     MOV.L ER4,ER6
  H'0007AA  0B 06                     ADDS #1,ER6
  H'0007AC  68 6B                     MOV.B @ER6,R3L
  H'0007AE  0F C6                     MOV.L ER4,ER6
  H'0007B0  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'0007B6  FD AA                     MOV.B #H'AA,R5L
  H'0007B8  68 ED                     MOV.B R5L,@ER6
  H'0007BA  0F C6                     MOV.L ER4,ER6
  H'0007BC  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'0007C2  FD 55                     MOV.B #H'55,R5L
  H'0007C4  68 ED                     MOV.B R5L,@ER6
  H'0007C6  0F C6                     MOV.L ER4,ER6
  H'0007C8  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'0007CE  FD 90                     MOV.B #H'90,R5L
  H'0007D0  68 ED                     MOV.B R5L,@ER6
  H'0007D2  79 06 00 0B               MOV.W #H'000B,R6
  H'0007D6  5C 00 FC 4C               BSR tramp_vec2:16
  H'0007DA  18 55                     SUB.B R5H,R5H
  H'0007DC  68 4E                     MOV.B @ER4,R6L
  H'0007DE  1C 3E                     CMP.B R3H,R6L
  H'0007E0  46 0E                     BNE H'0007F0
  H'0007E2  0F C6                     MOV.L ER4,ER6
  H'0007E4  0B 06                     ADDS #1,ER6
  H'0007E6  68 6D                     MOV.B @ER6,R5L
  H'0007E8  1C BD                     CMP.B R3L,R5L
  H'0007EA  46 04                     BNE H'0007F0
  H'0007EC  F5 0A                     MOV.B #H'0A,R5H
  H'0007EE  40 60                     BRA H'000850
  H'0007F0  68 4E                     MOV.B @ER4,R6L
  H'0007F2  AE 33                     CMP.B #H'33,R6L
  H'0007F4  46 02                     BNE H'0007F8
  H'0007F6  40 58                     BRA H'000850
  H'0007F8  AE 01                     CMP.B #H'01,R6L
  H'0007FA  47 40                     BEQ H'00083C
  H'0007FC  AE 04                     CMP.B #H'04,R6L
  H'0007FE  47 28                     BEQ H'000828
  H'000800  AE 1F                     CMP.B #H'1F,R6L
  H'000802  46 4C                     BNE H'000850
  H'000804  0F C6                     MOV.L ER4,ER6
  H'000806  0B 06                     ADDS #1,ER6
  H'000808  68 6D                     MOV.B @ER6,R5L
  H'00080A  AD A4                     CMP.B #H'A4,R5L
  H'00080C  46 04                     BNE H'000812
  H'00080E  F5 01                     MOV.B #H'01,R5H
  H'000810  40 16                     BRA H'000828
  H'000812  AD 13                     CMP.B #H'13,R5L
  H'000814  46 04                     BNE H'00081A
  H'000816  F5 02                     MOV.B #H'02,R5H
  H'000818  40 0E                     BRA H'000828
  H'00081A  AD DA                     CMP.B #H'DA,R5L
  H'00081C  46 04                     BNE H'000822
  H'00081E  F5 03                     MOV.B #H'03,R5H
  H'000820  40 06                     BRA H'000828
  H'000822  AD D5                     CMP.B #H'D5,R5L
  H'000824  46 02                     BNE H'000828
  H'000826  F5 04                     MOV.B #H'04,R5H
  H'000828  0F C6                     MOV.L ER4,ER6
  H'00082A  0B 06                     ADDS #1,ER6
  H'00082C  68 6D                     MOV.B @ER6,R5L
  H'00082E  AD AD                     CMP.B #H'AD,R5L
  H'000830  46 04                     BNE H'000836
  H'000832  F5 06                     MOV.B #H'06,R5H
  H'000834  40 06                     BRA H'00083C
  H'000836  AD D5                     CMP.B #H'D5,R5L
  H'000838  46 02                     BNE H'00083C
  H'00083A  F5 07                     MOV.B #H'07,R5H
  H'00083C  0F C6                     MOV.L ER4,ER6
  H'00083E  0B 06                     ADDS #1,ER6
  H'000840  68 6D                     MOV.B @ER6,R5L
  H'000842  AD AD                     CMP.B #H'AD,R5L
  H'000844  46 04                     BNE H'00084A
  H'000846  F5 08                     MOV.B #H'08,R5H
  H'000848  40 06                     BRA H'000850
  H'00084A  AD D5                     CMP.B #H'D5,R5L
  H'00084C  46 02                     BNE H'000850
  H'00084E  F5 09                     MOV.B #H'09,R5H
  H'000850  0F C6                     MOV.L ER4,ER6
  H'000852  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000858  FD AA                     MOV.B #H'AA,R5L
  H'00085A  68 ED                     MOV.B R5L,@ER6
  H'00085C  0F C6                     MOV.L ER4,ER6
  H'00085E  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'000864  FD 55                     MOV.B #H'55,R5L
  H'000866  68 ED                     MOV.B R5L,@ER6
  H'000868  0F C6                     MOV.L ER4,ER6
  H'00086A  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000870  FD F0                     MOV.B #H'F0,R5L
  H'000872  68 ED                     MOV.B R5L,@ER6
  H'000874  A5 0A                     CMP.B #H'0A,R5H
  H'000876  46 14                     BNE H'00088C
  H'000878  0F C6                     MOV.L ER4,ER6
  H'00087A  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000880  68 E2                     MOV.B R2H,@ER6
  H'000882  0F C6                     MOV.L ER4,ER6
  H'000884  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'00088A  68 EA                     MOV.B R2L,@ER6
  H'00088C  79 06 00 0B               MOV.W #H'000B,R6
  H'000890  5C 00 FB 92               BSR tramp_vec2:16
  H'000894  0C 5E                     MOV.B R5H,R6L
  H'000896  6D 75                     POP.W R5
  H'000898  01 00 6D 74               POP.L ER4
  H'00089C  6D 73                     POP.W R3
  H'00089E  6D 72                     POP.W R2
  H'0008A0  54 70                     RTS

sub_0008A2:
  H'0008A2  01 00 6D F5               PUSH.L ER5
  H'0008A6  01 00 6D F6               PUSH.L ER6
  H'0008AA  79 05 01 00               MOV.W #H'0100,R5
  H'0008AE  6D F5                     PUSH.W R5
  H'0008B0  01 00 6B 25 00 FF FD 10   MOV.L @H'FFFD10:24,ER5
  H'0008B8  01 00 6D F5               PUSH.L ER5
  H'0008BC  01 00 6F 76 00 06         MOV.L @(H'0006:16,ER7),ER6
  H'0008C2  7A 66 00 FF FF 00         AND.L #H'00FFFF00,ER6
  H'0008C8  5C 00 FE 72               BSR sub_00073E:16
  H'0008CC  0B 97                     ADDS #4,ER7
  H'0008CE  0B 87                     ADDS #2,ER7
  H'0008D0  0B 97                     ADDS #4,ER7
  H'0008D2  01 00 6D 75               POP.L ER5
  H'0008D6  54 70                     RTS

sub_0008D8:
  H'0008D8  01 00 6D F4               PUSH.L ER4
  H'0008DC  6D F5                     PUSH.W R5
  H'0008DE  1B 97                     SUBS #4,ER7
  H'0008E0  0F E4                     MOV.L ER6,ER4
  H'0008E2  01 00 69 F4               MOV.L ER4,@ER7
  H'0008E6  19 55                     SUB.W R5,R5
  H'0008E8  7C 40 73 70               BTST #7,@ER4
  H'0008EC  46 14                     BNE H'000902
  H'0008EE  0D 56                     MOV.W R5,R6
  H'0008F0  0B 55                     INC.W #1,R5
  H'0008F2  79 26 3A 98               CMP.W #H'3A98,R6
  H'0008F6  4C 0A                     BGE H'000902
  H'0008F8  79 06 00 01               MOV.W #H'0001,R6
  H'0008FC  5C 00 FB 26               BSR tramp_vec2:16
  H'000900  40 E6                     BRA H'0008E8
  H'000902  79 25 3A 98               CMP.W #H'3A98,R5
  H'000906  4C 06                     BGE H'00090E
  H'000908  79 06 00 01               MOV.W #H'0001,R6
  H'00090C  40 02                     BRA H'000910
  H'00090E  19 66                     SUB.W R6,R6
  H'000910  0B 97                     ADDS #4,ER7
  H'000912  6D 75                     POP.W R5
  H'000914  01 00 6D 74               POP.L ER4
  H'000918  54 70                     RTS

sub_00091A:
  H'00091A  01 00 6D F3               PUSH.L ER3
  H'00091E  01 00 6D F4               PUSH.L ER4
  H'000922  6D F5                     PUSH.W R5
  H'000924  7A 66 00 FF 00 00         AND.L #H'00FF0000,ER6
  H'00092A  0F E3                     MOV.L ER6,ER3
  H'00092C  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000932  FD AA                     MOV.B #H'AA,R5L
  H'000934  68 ED                     MOV.B R5L,@ER6
  H'000936  0F B6                     MOV.L ER3,ER6
  H'000938  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'00093E  FD 55                     MOV.B #H'55,R5L
  H'000940  68 ED                     MOV.B R5L,@ER6
  H'000942  0F B6                     MOV.L ER3,ER6
  H'000944  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'00094A  FD 80                     MOV.B #H'80,R5L
  H'00094C  68 ED                     MOV.B R5L,@ER6
  H'00094E  0F B6                     MOV.L ER3,ER6
  H'000950  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000956  FD AA                     MOV.B #H'AA,R5L
  H'000958  68 ED                     MOV.B R5L,@ER6
  H'00095A  0F B6                     MOV.L ER3,ER6
  H'00095C  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'000962  FD 55                     MOV.B #H'55,R5L
  H'000964  68 ED                     MOV.B R5L,@ER6
  H'000966  1A C4                     SUB.L ER4,ER4
  H'000968  7A 24 00 01 00 00         CMP.L #H'00010000,ER4
  H'00096E  44 10                     BCC H'000980
  H'000970  0F C6                     MOV.L ER4,ER6
  H'000972  0A B6                     ADD.L ER3,ER6
  H'000974  FD 30                     MOV.B #H'30,R5L
  H'000976  68 ED                     MOV.B R5L,@ER6
  H'000978  7A 14 00 01 00 00         ADD.L #H'00010000,ER4
  H'00097E  40 E8                     BRA H'000968
  H'000980  79 06 00 01               MOV.W #H'0001,R6
  H'000984  5C 00 FA 9E               BSR tramp_vec2:16
  H'000988  0F B6                     MOV.L ER3,ER6
  H'00098A  5C 00 FF 4A               BSR sub_0008D8:16
  H'00098E  6D 75                     POP.W R5
  H'000990  01 00 6D 74               POP.L ER4
  H'000994  01 00 6D 73               POP.L ER3
  H'000998  54 70                     RTS

sub_00099A:
  H'00099A  01 00 6D F4               PUSH.L ER4
  H'00099E  6D F5                     PUSH.W R5
  H'0009A0  7A 66 00 FF 00 00         AND.L #H'00FF0000,ER6
  H'0009A6  0F E4                     MOV.L ER6,ER4
  H'0009A8  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'0009AE  FD AA                     MOV.B #H'AA,R5L
  H'0009B0  68 ED                     MOV.B R5L,@ER6
  H'0009B2  0F C6                     MOV.L ER4,ER6
  H'0009B4  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'0009BA  FD 55                     MOV.B #H'55,R5L
  H'0009BC  68 ED                     MOV.B R5L,@ER6
  H'0009BE  0F C6                     MOV.L ER4,ER6
  H'0009C0  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'0009C6  FD A0                     MOV.B #H'A0,R5L
  H'0009C8  68 ED                     MOV.B R5L,@ER6
  H'0009CA  6D 75                     POP.W R5
  H'0009CC  01 00 6D 74               POP.L ER4
  H'0009D0  54 70                     RTS

sub_0009D2:
  H'0009D2  01 00 6D F3               PUSH.L ER3
  H'0009D6  6D F4                     PUSH.W R4
  H'0009D8  6D F5                     PUSH.W R5
  H'0009DA  1B 97                     SUBS #4,ER7
  H'0009DC  0F E3                     MOV.L ER6,ER3
  H'0009DE  01 00 69 F3               MOV.L ER3,@ER7
  H'0009E2  19 44                     SUB.W R4,R4
  H'0009E4  68 3E                     MOV.B @ER3,R6L
  H'0009E6  EE 40                     AND.B #H'40,R6L
  H'0009E8  68 3D                     MOV.B @ER3,R5L
  H'0009EA  ED 40                     AND.B #H'40,R5L
  H'0009EC  1C DE                     CMP.B R5L,R6L
  H'0009EE  47 14                     BEQ H'000A04
  H'0009F0  0D 46                     MOV.W R4,R6
  H'0009F2  0B 54                     INC.W #1,R4
  H'0009F4  79 26 2A F8               CMP.W #H'2AF8,R6
  H'0009F8  4C 0A                     BGE H'000A04
  H'0009FA  79 06 00 01               MOV.W #H'0001,R6
  H'0009FE  5C 00 FA 24               BSR tramp_vec2:16
  H'000A02  40 E0                     BRA H'0009E4
  H'000A04  79 24 2A F8               CMP.W #H'2AF8,R4
  H'000A08  4C 06                     BGE H'000A10
  H'000A0A  79 06 00 01               MOV.W #H'0001,R6
  H'000A0E  40 02                     BRA H'000A12
  H'000A10  19 66                     SUB.W R6,R6
  H'000A12  0B 97                     ADDS #4,ER7
  H'000A14  6D 75                     POP.W R5
  H'000A16  6D 74                     POP.W R4
  H'000A18  01 00 6D 73               POP.L ER3
  H'000A1C  54 70                     RTS

sub_000A1E:
  H'000A1E  01 00 6D F3               PUSH.L ER3
  H'000A22  6D F4                     PUSH.W R4
  H'000A24  6D F5                     PUSH.W R5
  H'000A26  1B 97                     SUBS #4,ER7
  H'000A28  0F E3                     MOV.L ER6,ER3
  H'000A2A  01 00 69 F3               MOV.L ER3,@ER7
  H'000A2E  19 44                     SUB.W R4,R4
  H'000A30  68 3E                     MOV.B @ER3,R6L
  H'000A32  EE 40                     AND.B #H'40,R6L
  H'000A34  68 3D                     MOV.B @ER3,R5L
  H'000A36  ED 40                     AND.B #H'40,R5L
  H'000A38  1C DE                     CMP.B R5L,R6L
  H'000A3A  47 14                     BEQ H'000A50
  H'000A3C  0D 46                     MOV.W R4,R6
  H'000A3E  0B 54                     INC.W #1,R4
  H'000A40  79 26 00 11               CMP.W #H'0011,R6
  H'000A44  4C 0A                     BGE H'000A50
  H'000A46  79 06 00 01               MOV.W #H'0001,R6
  H'000A4A  5C 00 F9 D8               BSR tramp_vec2:16
  H'000A4E  40 E0                     BRA H'000A30
  H'000A50  79 24 00 11               CMP.W #H'0011,R4
  H'000A54  4C 06                     BGE H'000A5C
  H'000A56  79 06 00 01               MOV.W #H'0001,R6
  H'000A5A  40 02                     BRA H'000A5E
  H'000A5C  19 66                     SUB.W R6,R6
  H'000A5E  0B 97                     ADDS #4,ER7
  H'000A60  6D 75                     POP.W R5
  H'000A62  6D 74                     POP.W R4
  H'000A64  01 00 6D 73               POP.L ER3
  H'000A68  54 70                     RTS
  H'000A6A  01 00 6D F5               PUSH.L ER5
  H'000A6E  01 00 6D F6               PUSH.L ER6
  H'000A72  79 05 01 00               MOV.W #H'0100,R5
  H'000A76  6D F5                     PUSH.W R5
  H'000A78  01 00 6B 25 00 FF FD 10   MOV.L @H'FFFD10:24,ER5
  H'000A80  01 00 6D F5               PUSH.L ER5
  H'000A84  01 00 6F 76 00 06         MOV.L @(H'0006:16,ER7),ER6
  H'000A8A  7A 66 00 FF FF 00         AND.L #H'00FFFF00,ER6
  H'000A90  5C 00 FC AA               BSR sub_00073E:16
  H'000A94  0B 97                     ADDS #4,ER7
  H'000A96  0B 87                     ADDS #2,ER7
  H'000A98  0B 97                     ADDS #4,ER7
  H'000A9A  01 00 6D 75               POP.L ER5
  H'000A9E  54 70                     RTS

sub_000AA0:
  H'000AA0  01 00 6D F5               PUSH.L ER5
  H'000AA4  0F E5                     MOV.L ER6,ER5
  H'000AA6  5C 00 FE F0               BSR sub_00099A:16
  H'000AAA  79 06 01 00               MOV.W #H'0100,R6
  H'000AAE  6D F6                     PUSH.W R6
  H'000AB0  0F D6                     MOV.L ER5,ER6
  H'000AB2  7A 66 00 FF FF 00         AND.L #H'00FFFF00,ER6
  H'000AB8  01 00 6D F6               PUSH.L ER6
  H'000ABC  01 00 6B 26 00 FF FD 10   MOV.L @H'FFFD10:24,ER6
  H'000AC4  5C 00 FC 76               BSR sub_00073E:16
  H'000AC8  0B 97                     ADDS #4,ER7
  H'000ACA  0B 87                     ADDS #2,ER7
  H'000ACC  0F D6                     MOV.L ER5,ER6
  H'000ACE  5C 00 FF 4C               BSR sub_000A1E:16
  H'000AD2  01 00 6D 75               POP.L ER5
  H'000AD6  54 70                     RTS

sub_000AD8:
  H'000AD8  55 C6                     BSR sub_000AA0
  H'000ADA  54 70                     RTS

tramp_vec3:
  H'000ADC  01 00 6D F0               PUSH.L ER0
  H'000AE0  01 00 6D F1               PUSH.L ER1
  H'000AE4  01 00 6D F2               PUSH.L ER2
  H'000AE8  01 00 6D F4               PUSH.L ER4
  H'000AEC  01 00 6D F5               PUSH.L ER5
  H'000AF0  01 00 6F 74 00 18         MOV.L @(VEC_vec6:16,ER7),ER4
  H'000AF6  01 00 6F 72 00 1C         MOV.L @(VEC_NMI:16,ER7),ER2
  H'000AFC  18 88                     SUB.B R0L,R0L
  H'000AFE  0F E1                     MOV.L ER6,ER1
  H'000B00  0F C6                     MOV.L ER4,ER6
  H'000B02  5C 00 FD 9C               BSR sub_0008A2:16
  H'000B06  0F C6                     MOV.L ER4,ER6
  H'000B08  7A 66 00 00 00 FF         AND.L #H'000000FF,ER6
  H'000B0E  0A A6                     ADD.L ER2,ER6
  H'000B10  7A 26 00 00 01 00         CMP.L #H'00000100,ER6
  H'000B16  43 0E                     BLS H'000B26
  H'000B18  0D 46                     MOV.W R4,R6
  H'000B1A  79 66 00 FF               AND.W #H'00FF,R6
  H'000B1E  79 08 01 00               MOV.W #H'0100,E0
  H'000B22  19 68                     SUB.W R6,E0
  H'000B24  40 04                     BRA H'000B2A
  H'000B26  0D 28                     MOV.W R2,E0
  H'000B28  F8 01                     MOV.B #H'01,R0L
  H'000B2A  6D F8                     PUSH.W E0
  H'000B2C  0F C6                     MOV.L ER4,ER6
  H'000B2E  7A 66 00 00 00 FF         AND.L #H'000000FF,ER6
  H'000B34  01 00 6B 25 00 FF FD 10   MOV.L @H'FFFD10:24,ER5
  H'000B3C  0A D6                     ADD.L ER5,ER6
  H'000B3E  01 00 6D F6               PUSH.L ER6
  H'000B42  0F 96                     MOV.L ER1,ER6
  H'000B44  5C 00 FB F6               BSR sub_00073E:16
  H'000B48  0B 97                     ADDS #4,ER7
  H'000B4A  0B 87                     ADDS #2,ER7
  H'000B4C  0F C6                     MOV.L ER4,ER6
  H'000B4E  55 88                     BSR sub_000AD8
  H'000B50  0D 86                     MOV.W E0,R6
  H'000B52  17 76                     EXTU.L ER6
  H'000B54  0A E1                     ADD.L ER6,ER1
  H'000B56  0A E4                     ADD.L ER6,ER4
  H'000B58  1A E2                     SUB.L ER6,ER2
  H'000B5A  0C 88                     MOV.B R0L,R0L
  H'000B5C  47 A2                     BEQ H'000B00
  H'000B5E  01 00 6D 75               POP.L ER5
  H'000B62  01 00 6D 74               POP.L ER4
  H'000B66  01 00 6D 72               POP.L ER2
  H'000B6A  01 00 6D 71               POP.L ER1
  H'000B6E  01 00 6D 70               POP.L ER0
  H'000B72  54 70                     RTS

sub_000B74:
  H'000B74  01 00 6D F4               PUSH.L ER4
  H'000B78  6D F5                     PUSH.W R5
  H'000B7A  0F E4                     MOV.L ER6,ER4
  H'000B7C  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000B82  FD AA                     MOV.B #H'AA,R5L
  H'000B84  68 ED                     MOV.B R5L,@ER6
  H'000B86  0F C6                     MOV.L ER4,ER6
  H'000B88  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'000B8E  FD 55                     MOV.B #H'55,R5L
  H'000B90  68 ED                     MOV.B R5L,@ER6
  H'000B92  0F C6                     MOV.L ER4,ER6
  H'000B94  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000B9A  FD 80                     MOV.B #H'80,R5L
  H'000B9C  68 ED                     MOV.B R5L,@ER6
  H'000B9E  0F C6                     MOV.L ER4,ER6
  H'000BA0  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'000BA6  FD 55                     MOV.B #H'55,R5L
  H'000BA8  68 ED                     MOV.B R5L,@ER6
  H'000BAA  0F C6                     MOV.L ER4,ER6
  H'000BAC  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000BB2  FD 10                     MOV.B #H'10,R5L
  H'000BB4  68 ED                     MOV.B R5L,@ER6
  H'000BB6  0F C6                     MOV.L ER4,ER6
  H'000BB8  5C 00 FE 16               BSR sub_0009D2:16
  H'000BBC  6D 75                     POP.W R5
  H'000BBE  01 00 6D 74               POP.L ER4
  H'000BC2  54 70                     RTS

sub_000BC4:
  H'000BC4  01 00 6D F2               PUSH.L ER2
  H'000BC8  01 00 6D F3               PUSH.L ER3
  H'000BCC  01 00 6D F5               PUSH.L ER5
  H'000BD0  0F E3                     MOV.L ER6,ER3
  H'000BD2  FE 4F                     MOV.B #H'4F,R6L
  H'000BD4  5C 00 F8 9A               BSR send_serial_data_byte:16
  H'000BD8  79 02 00 01               MOV.W #H'0001,R2
  H'000BDC  79 22 00 01               CMP.W #H'0001,R2
  H'000BE0  58 60 00 92               BNE H'000C76:16
  H'000BE4  FE 45                     MOV.B #H'45,R6L
  H'000BE6  5C 00 F8 88               BSR send_serial_data_byte:16
  H'000BEA  5C 00 F9 4E               BSR get_rx_data_full_bit:16
  H'000BEE  AE 59                     CMP.B #H'59,R6L
  H'000BF0  46 72                     BNE H'000C64
  H'000BF2  FE 59                     MOV.B #H'59,R6L
  H'000BF4  5C 00 F8 7A               BSR send_serial_data_byte:16
  H'000BF8  19 AA                     SUB.W E2,E2
  H'000BFA  79 2A 01 00               CMP.W #H'0100,E2
  H'000BFE  47 20                     BEQ H'000C20
  H'000C00  0D A6                     MOV.W E2,R6
  H'000C02  17 F6                     EXTS.L ER6
  H'000C04  01 00 6B 25 00 FF FD 10   MOV.L @H'FFFD10:24,ER5
  H'000C0C  0A D6                     ADD.L ER5,ER6
  H'000C0E  01 00 6D F6               PUSH.L ER6
  H'000C12  5C 00 F9 26               BSR get_rx_data_full_bit:16
  H'000C16  01 00 6D 75               POP.L ER5
  H'000C1A  68 DE                     MOV.B R6L,@ER5
  H'000C1C  0B 5A                     INC.W #1,E2
  H'000C1E  40 DA                     BRA H'000BFA
  H'000C20  79 22 00 01               CMP.W #H'0001,R2
  H'000C24  46 08                     BNE H'000C2E
  H'000C26  FE 4F                     MOV.B #H'4F,R6L
  H'000C28  5C 00 F8 46               BSR send_serial_data_byte:16
  H'000C2C  40 06                     BRA H'000C34
  H'000C2E  FE 56                     MOV.B #H'56,R6L
  H'000C30  5C 00 F8 3E               BSR send_serial_data_byte:16
  H'000C34  0F B6                     MOV.L ER3,ER6
  H'000C36  5C 00 FD E4               BSR sub_000A1E:16
  H'000C3A  0D 62                     MOV.W R6,R2
  H'000C3C  0F B6                     MOV.L ER3,ER6
  H'000C3E  5C 00 FD 58               BSR sub_00099A:16
  H'000C42  79 06 01 00               MOV.W #H'0100,R6
  H'000C46  6D F6                     PUSH.W R6
  H'000C48  01 00 6D F3               PUSH.L ER3
  H'000C4C  01 00 6B 26 00 FF FD 10   MOV.L @H'FFFD10:24,ER6
  H'000C54  5C 00 FA E6               BSR sub_00073E:16
  H'000C58  0B 97                     ADDS #4,ER7
  H'000C5A  0B 87                     ADDS #2,ER7
  H'000C5C  7A 13 00 00 01 00         ADD.L #H'00000100,ER3
  H'000C62  40 0E                     BRA H'000C72
  H'000C64  FE 4E                     MOV.B #H'4E,R6L
  H'000C66  5C 00 F8 08               BSR send_serial_data_byte:16
  H'000C6A  0F B6                     MOV.L ER3,ER6
  H'000C6C  5C 00 FD AE               BSR sub_000A1E:16
  H'000C70  19 22                     SUB.W R2,R2
  H'000C72  58 00 FF 66               BRA H'000BDC:16
  H'000C76  01 00 6D 75               POP.L ER5
  H'000C7A  01 00 6D 73               POP.L ER3
  H'000C7E  01 00 6D 72               POP.L ER2
  H'000C82  54 70                     RTS
  H'000C84  01 00 6D F1               PUSH.L ER1
  H'000C88  01 00 6D F2               PUSH.L ER2
  H'000C8C  01 00 6D F3               PUSH.L ER3
  H'000C90  6D F4                     PUSH.W R4
  H'000C92  6D F5                     PUSH.W R5
  H'000C94  0F E2                     MOV.L ER6,ER2
  H'000C96  7A 66 0F FF 00 00         AND.L #H'0FFF0000,ER6
  H'000C9C  0F E3                     MOV.L ER6,ER3
  H'000C9E  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000CA4  FD AA                     MOV.B #H'AA,R5L
  H'000CA6  68 ED                     MOV.B R5L,@ER6
  H'000CA8  0F B6                     MOV.L ER3,ER6
  H'000CAA  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'000CB0  FD 55                     MOV.B #H'55,R5L
  H'000CB2  68 ED                     MOV.B R5L,@ER6
  H'000CB4  0F B6                     MOV.L ER3,ER6
  H'000CB6  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000CBC  FD A0                     MOV.B #H'A0,R5L
  H'000CBE  68 ED                     MOV.B R5L,@ER6
  H'000CC0  6E 7E 00 15               MOV.B @(H'0015:16,ER7),R6L
  H'000CC4  68 AE                     MOV.B R6L,@ER2
  H'000CC6  0F A1                     MOV.L ER2,ER1
  H'000CC8  19 44                     SUB.W R4,R4
  H'000CCA  68 2E                     MOV.B @ER2,R6L
  H'000CCC  EE 40                     AND.B #H'40,R6L
  H'000CCE  68 1D                     MOV.B @ER1,R5L
  H'000CD0  ED 40                     AND.B #H'40,R5L
  H'000CD2  1C DE                     CMP.B R5L,R6L
  H'000CD4  47 0C                     BEQ H'000CE2
  H'000CD6  0D 46                     MOV.W R4,R6
  H'000CD8  0B 54                     INC.W #1,R4
  H'000CDA  79 26 03 E8               CMP.W #H'03E8,R6
  H'000CDE  4C 02                     BGE H'000CE2
  H'000CE0  40 E8                     BRA H'000CCA
  H'000CE2  79 24 03 E8               CMP.W #H'03E8,R4
  H'000CE6  4C 06                     BGE H'000CEE
  H'000CE8  79 06 00 01               MOV.W #H'0001,R6
  H'000CEC  40 02                     BRA H'000CF0
  H'000CEE  19 66                     SUB.W R6,R6
  H'000CF0  6D 75                     POP.W R5
  H'000CF2  6D 74                     POP.W R4
  H'000CF4  01 00 6D 73               POP.L ER3
  H'000CF8  01 00 6D 72               POP.L ER2
  H'000CFC  01 00 6D 71               POP.L ER1
  H'000D00  54 70                     RTS

sub_000D02:
  H'000D02  01 00 6D F1               PUSH.L ER1
  H'000D06  01 00 6D F3               PUSH.L ER3
  H'000D0A  01 00 6D F4               PUSH.L ER4
  H'000D0E  6D F5                     PUSH.W R5
  H'000D10  1B 97                     SUBS #4,ER7
  H'000D12  0F E3                     MOV.L ER6,ER3
  H'000D14  7A 66 0F FF 00 00         AND.L #H'0FFF0000,ER6
  H'000D1A  0F E4                     MOV.L ER6,ER4
  H'000D1C  FE 4F                     MOV.B #H'4F,R6L
  H'000D1E  5C 00 F7 50               BSR send_serial_data_byte:16
  H'000D22  01 00 69 F3               MOV.L ER3,@ER7
  H'000D26  79 01 00 01               MOV.W #H'0001,R1
  H'000D2A  79 21 00 01               CMP.W #H'0001,R1
  H'000D2E  58 60 00 92               BNE H'000DC4:16
  H'000D32  FE 45                     MOV.B #H'45,R6L
  H'000D34  5C 00 F7 3A               BSR send_serial_data_byte:16
  H'000D38  5C 00 F8 00               BSR get_rx_data_full_bit:16
  H'000D3C  AE 59                     CMP.B #H'59,R6L
  H'000D3E  46 78                     BNE H'000DB8
  H'000D40  0F B6                     MOV.L ER3,ER6
  H'000D42  7A 66 00 00 FF FF         AND.L #H'0000FFFF,ER6
  H'000D48  46 06                     BNE H'000D50
  H'000D4A  0F B6                     MOV.L ER3,ER6
  H'000D4C  5C 00 FB CA               BSR sub_00091A:16
  H'000D50  FE 59                     MOV.B #H'59,R6L
  H'000D52  5C 00 F7 1C               BSR send_serial_data_byte:16
  H'000D56  0F B4                     MOV.L ER3,ER4
  H'000D58  7A 64 00 FF 00 00         AND.L #H'00FF0000,ER4
  H'000D5E  19 99                     SUB.W E1,E1
  H'000D60  79 29 01 00               CMP.W #H'0100,E1
  H'000D64  47 36                     BEQ H'000D9C
  H'000D66  5C 00 F7 D2               BSR get_rx_data_full_bit:16
  H'000D6A  0C E5                     MOV.B R6L,R5H
  H'000D6C  0F C6                     MOV.L ER4,ER6
  H'000D6E  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000D74  FD AA                     MOV.B #H'AA,R5L
  H'000D76  68 ED                     MOV.B R5L,@ER6
  H'000D78  0F C6                     MOV.L ER4,ER6
  H'000D7A  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'000D80  FD 55                     MOV.B #H'55,R5L
  H'000D82  68 ED                     MOV.B R5L,@ER6
  H'000D84  0F C6                     MOV.L ER4,ER6
  H'000D86  7A 16 00 00 55 55         ADD.L #H'00005555,ER6
  H'000D8C  FD A0                     MOV.B #H'A0,R5L
  H'000D8E  68 ED                     MOV.B R5L,@ER6
  H'000D90  0D 96                     MOV.W E1,R6
  H'000D92  17 F6                     EXTS.L ER6
  H'000D94  0A B6                     ADD.L ER3,ER6
  H'000D96  68 E5                     MOV.B R5H,@ER6
  H'000D98  0B 59                     INC.W #1,E1
  H'000D9A  40 C4                     BRA H'000D60
  H'000D9C  79 21 00 01               CMP.W #H'0001,R1
  H'000DA0  46 08                     BNE H'000DAA
  H'000DA2  FE 4F                     MOV.B #H'4F,R6L
  H'000DA4  5C 00 F6 CA               BSR send_serial_data_byte:16
  H'000DA8  40 06                     BRA H'000DB0
  H'000DAA  FE 56                     MOV.B #H'56,R6L
  H'000DAC  5C 00 F6 C2               BSR send_serial_data_byte:16
  H'000DB0  7A 13 00 00 01 00         ADD.L #H'00000100,ER3
  H'000DB6  40 08                     BRA H'000DC0
  H'000DB8  FE 4E                     MOV.B #H'4E,R6L
  H'000DBA  5C 00 F6 B4               BSR send_serial_data_byte:16
  H'000DBE  19 11                     SUB.W R1,R1
  H'000DC0  58 00 FF 66               BRA H'000D2A:16
  H'000DC4  0B 97                     ADDS #4,ER7
  H'000DC6  6D 75                     POP.W R5
  H'000DC8  01 00 6D 74               POP.L ER4
  H'000DCC  01 00 6D 73               POP.L ER3
  H'000DD0  01 00 6D 71               POP.L ER1
  H'000DD4  54 70                     RTS

sub_000DD6:
  H'000DD6  6D F4                     PUSH.W R4
  H'000DD8  01 00 6D F5               PUSH.L ER5
  H'000DDC  01 00 6D F6               PUSH.L ER6
  H'000DE0  1B 87                     SUBS #2,ER7
  H'000DE2  FE 4F                     MOV.B #H'4F,R6L
  H'000DE4  5C 00 F6 8A               BSR send_serial_data_byte:16
  H'000DE8  FE 45                     MOV.B #H'45,R6L
  H'000DEA  5C 00 F6 84               BSR send_serial_data_byte:16
  H'000DEE  79 06 00 01               MOV.W #H'0001,R6
  H'000DF2  69 F6                     MOV.W R6,@ER7
  H'000DF4  19 44                     SUB.W R4,R4
  H'000DF6  79 24 01 00               CMP.W #H'0100,R4
  H'000DFA  47 1E                     BEQ H'000E1A
  H'000DFC  0D 46                     MOV.W R4,R6
  H'000DFE  17 F6                     EXTS.L ER6
  H'000E00  01 00 6F 75 00 02         MOV.L @(H'0002:16,ER7),ER5
  H'000E06  0A D6                     ADD.L ER5,ER6
  H'000E08  01 00 6D F6               PUSH.L ER6
  H'000E0C  5C 00 F7 2C               BSR get_rx_data_full_bit:16
  H'000E10  01 00 6D 75               POP.L ER5
  H'000E14  68 DE                     MOV.B R6L,@ER5
  H'000E16  0B 54                     INC.W #1,R4
  H'000E18  40 DC                     BRA H'000DF6
  H'000E1A  FE 4F                     MOV.B #H'4F,R6L
  H'000E1C  5C 00 F6 52               BSR send_serial_data_byte:16
  H'000E20  0B 97                     ADDS #4,ER7
  H'000E22  0B 87                     ADDS #2,ER7
  H'000E24  01 00 6D 75               POP.L ER5
  H'000E28  6D 74                     POP.W R4
  H'000E2A  54 70                     RTS

sub_000E2C:
  H'000E2C  01 00 6D F2               PUSH.L ER2
  H'000E30  01 00 6D F3               PUSH.L ER3
  H'000E34  01 00 6D F5               PUSH.L ER5
  H'000E38  0F E2                     MOV.L ER6,ER2
  H'000E3A  FE 4F                     MOV.B #H'4F,R6L
  H'000E3C  5C 00 F6 32               BSR send_serial_data_byte:16
  H'000E40  FE 45                     MOV.B #H'45,R6L
  H'000E42  5C 00 F6 2C               BSR send_serial_data_byte:16
  H'000E46  79 03 00 01               MOV.W #H'0001,R3
  H'000E4A  19 BB                     SUB.W E3,E3
  H'000E4C  79 2B 01 00               CMP.W #H'0100,E3
  H'000E50  47 20                     BEQ H'000E72
  H'000E52  0D B6                     MOV.W E3,R6
  H'000E54  17 F6                     EXTS.L ER6
  H'000E56  01 00 6B 25 00 FF FD 10   MOV.L @H'FFFD10:24,ER5
  H'000E5E  0A D6                     ADD.L ER5,ER6
  H'000E60  01 00 6D F6               PUSH.L ER6
  H'000E64  5C 00 F6 D4               BSR get_rx_data_full_bit:16
  H'000E68  01 00 6D 75               POP.L ER5
  H'000E6C  68 DE                     MOV.B R6L,@ER5
  H'000E6E  0B 5B                     INC.W #1,E3
  H'000E70  40 DA                     BRA H'000E4C
  H'000E72  0F A6                     MOV.L ER2,ER6
  H'000E74  5C 00 FB 22               BSR sub_00099A:16
  H'000E78  79 06 01 00               MOV.W #H'0100,R6
  H'000E7C  6D F6                     PUSH.W R6
  H'000E7E  01 00 6D F2               PUSH.L ER2
  H'000E82  01 00 6B 26 00 FF FD 10   MOV.L @H'FFFD10:24,ER6
  H'000E8A  5C 00 F8 B0               BSR sub_00073E:16
  H'000E8E  0B 97                     ADDS #4,ER7
  H'000E90  0B 87                     ADDS #2,ER7
  H'000E92  0F A6                     MOV.L ER2,ER6
  H'000E94  5C 00 FB 86               BSR sub_000A1E:16
  H'000E98  0D 63                     MOV.W R6,R3
  H'000E9A  79 23 00 01               CMP.W #H'0001,R3
  H'000E9E  46 08                     BNE H'000EA8
  H'000EA0  FE 4F                     MOV.B #H'4F,R6L
  H'000EA2  5C 00 F5 CC               BSR send_serial_data_byte:16
  H'000EA6  40 06                     BRA H'000EAE
  H'000EA8  FE 56                     MOV.B #H'56,R6L
  H'000EAA  5C 00 F5 C4               BSR send_serial_data_byte:16
  H'000EAE  01 00 6D 75               POP.L ER5
  H'000EB2  01 00 6D 73               POP.L ER3
  H'000EB6  01 00 6D 72               POP.L ER2
  H'000EBA  54 70                     RTS

sub_000EBC:
  H'000EBC  01 00 6D F1               PUSH.L ER1
  H'000EC0  01 00 6D F2               PUSH.L ER2
  H'000EC4  01 00 6D F4               PUSH.L ER4
  H'000EC8  6D F5                     PUSH.W R5
  H'000ECA  0F E4                     MOV.L ER6,ER4
  H'000ECC  5C 00 FC A4               BSR sub_000B74:16
  H'000ED0  18 66                     SUB.B R6H,R6H
  H'000ED2  0D 62                     MOV.W R6,R2
  H'000ED4  79 22 00 01               CMP.W #H'0001,R2
  H'000ED8  46 08                     BNE H'000EE2
  H'000EDA  FE 4F                     MOV.B #H'4F,R6L
  H'000EDC  5C 00 F5 92               BSR send_serial_data_byte:16
  H'000EE0  40 06                     BRA H'000EE8
  H'000EE2  FE 56                     MOV.B #H'56,R6L
  H'000EE4  5C 00 F5 8A               BSR send_serial_data_byte:16
  H'000EE8  0D 22                     MOV.W R2,R2
  H'000EEA  58 70 00 80               BEQ H'000F6E:16
  H'000EEE  FE 45                     MOV.B #H'45,R6L
  H'000EF0  5C 00 F5 7E               BSR send_serial_data_byte:16
  H'000EF4  5C 00 F6 44               BSR get_rx_data_full_bit:16
  H'000EF8  AE 59                     CMP.B #H'59,R6L
  H'000EFA  46 66                     BNE H'000F62
  H'000EFC  FE 59                     MOV.B #H'59,R6L
  H'000EFE  5C 00 F5 70               BSR send_serial_data_byte:16
  H'000F02  19 AA                     SUB.W E2,E2
  H'000F04  79 2A 01 00               CMP.W #H'0100,E2
  H'000F08  47 3C                     BEQ H'000F46
  H'000F0A  5C 00 F6 2E               BSR get_rx_data_full_bit:16
  H'000F0E  0C E5                     MOV.B R6L,R5H
  H'000F10  0F C1                     MOV.L ER4,ER1
  H'000F12  7A 61 00 FF 00 00         AND.L #H'00FF0000,ER1
  H'000F18  7A 11 00 00 55 55         ADD.L #H'00005555,ER1
  H'000F1E  FE AA                     MOV.B #H'AA,R6L
  H'000F20  68 9E                     MOV.B R6L,@ER1
  H'000F22  0F C6                     MOV.L ER4,ER6
  H'000F24  7A 66 00 FF 00 00         AND.L #H'00FF0000,ER6
  H'000F2A  7A 16 00 00 2A AA         ADD.L #H'00002AAA,ER6
  H'000F30  FD 55                     MOV.B #H'55,R5L
  H'000F32  68 ED                     MOV.B R5L,@ER6
  H'000F34  FE A0                     MOV.B #H'A0,R6L
  H'000F36  68 9E                     MOV.B R6L,@ER1
  H'000F38  68 C5                     MOV.B R5H,@ER4
  H'000F3A  0F C6                     MOV.L ER4,ER6
  H'000F3C  5C 00 FA DE               BSR sub_000A1E:16
  H'000F40  0D 62                     MOV.W R6,R2
  H'000F42  0B 5A                     INC.W #1,E2
  H'000F44  40 BE                     BRA H'000F04
  H'000F46  79 22 00 01               CMP.W #H'0001,R2
  H'000F4A  46 08                     BNE H'000F54
  H'000F4C  FE 4F                     MOV.B #H'4F,R6L
  H'000F4E  5C 00 F5 20               BSR send_serial_data_byte:16
  H'000F52  40 06                     BRA H'000F5A
  H'000F54  FE 56                     MOV.B #H'56,R6L
  H'000F56  5C 00 F5 18               BSR send_serial_data_byte:16
  H'000F5A  7A 14 00 00 01 00         ADD.L #H'00000100,ER4
  H'000F60  40 08                     BRA H'000F6A
  H'000F62  FE 4E                     MOV.B #H'4E,R6L
  H'000F64  5C 00 F5 0A               BSR send_serial_data_byte:16
  H'000F68  19 22                     SUB.W R2,R2
  H'000F6A  58 00 FF 7A               BRA H'000EE8:16
  H'000F6E  6D 75                     POP.W R5
  H'000F70  01 00 6D 74               POP.L ER4
  H'000F74  01 00 6D 72               POP.L ER2
  H'000F78  01 00 6D 71               POP.L ER1
  H'000F7C  54 70                     RTS

sub_000F7E:
  H'000F7E  01 00 6D F3               PUSH.L ER3
  H'000F82  01 00 6D F4               PUSH.L ER4
  H'000F86  01 00 6D F5               PUSH.L ER5
  H'000F8A  01 00 6F 73 00 10         MOV.L @(VEC_vec4:16,ER7),ER3
  H'000F90  1A D5                     SUB.L ER5,ER5
  H'000F92  0F E4                     MOV.L ER6,ER4
  H'000F94  0F B6                     MOV.L ER3,ER6
  H'000F96  1B 73                     DEC.L #1,ER3
  H'000F98  0F E6                     MOV.L ER6,ER6
  H'000F9A  47 0A                     BEQ H'000FA6
  H'000F9C  1A E6                     SUB.L ER6,ER6
  H'000F9E  68 4E                     MOV.B @ER4,R6L
  H'000FA0  0A E5                     ADD.L ER6,ER5
  H'000FA2  0B 74                     INC.L #1,ER4
  H'000FA4  40 EE                     BRA H'000F94
  H'000FA6  0F D6                     MOV.L ER5,ER6
  H'000FA8  01 00 6D 75               POP.L ER5
  H'000FAC  01 00 6D 74               POP.L ER4
  H'000FB0  01 00 6D 73               POP.L ER3
  H'000FB4  54 70                     RTS

sub_000FB6:
  H'000FB6  01 00 6D F3               PUSH.L ER3
  H'000FBA  6D F4                     PUSH.W R4
  H'000FBC  6D F5                     PUSH.W R5
  H'000FBE  6D FE                     PUSH.W E6
  H'000FC0  5C 00 F4 88               BSR read_serial_data_byte:16
  H'000FC4  0C EC                     MOV.B R6L,R4L
  H'000FC6  18 CC                     SUB.B R4L,R4L
  H'000FC8  1A B3                     SUB.L ER3,ER3
  H'000FCA  7A 23 00 00 01 F4         CMP.L #H'000001F4,ER3
  H'000FD0  58 70 00 A4               BEQ H'001078:16
  H'000FD4  79 06 00 01               MOV.W #H'0001,R6
  H'000FD8  5C 00 F4 4A               BSR tramp_vec2:16
  H'000FDC  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'000FE2  7D 60 70 10               BSET #1,@ER6
  H'000FE6  5C 00 F5 8C               BSR get_sci_rx_ready_bit:16
  H'000FEA  0D 66                     MOV.W R6,R6
  H'000FEC  47 2A                     BEQ H'001018
  H'000FEE  1A E6                     SUB.L ER6,ER6
  H'000FF0  0C 4E                     MOV.B R4H,R6L
  H'000FF2  78 60 6A 2E 00 00 23 78   MOV.B @(H'002378:24,ER6),R6L
  H'000FFA  6D F6                     PUSH.W R6
  H'000FFC  5C 00 F4 4C               BSR read_serial_data_byte:16
  H'001000  6D 75                     POP.W R5
  H'001002  1D 65                     CMP.W R6,R5
  H'001004  46 0E                     BNE H'001014
  H'001006  0A 04                     INC.B R4H
  H'001008  A4 02                     CMP.B #H'02,R4H
  H'00100A  46 06                     BNE H'001012
  H'00100C  79 06 00 01               MOV.W #H'0001,R6
  H'001010  40 72                     BRA H'001084
  H'001012  40 02                     BRA H'001016
  H'001014  18 44                     SUB.B R4H,R4H
  H'001016  40 04                     BRA H'00101C
  H'001018  5C 00 F4 D2               BSR serial_clear_rx_errors:16
  H'00101C  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'001022  7D 60 72 10               BCLR #1,@ER6
  H'001026  5C 00 F5 4C               BSR get_sci_rx_ready_bit:16
  H'00102A  0D 66                     MOV.W R6,R6
  H'00102C  47 40                     BEQ H'00106E
  H'00102E  1A E6                     SUB.L ER6,ER6
  H'001030  0C CE                     MOV.B R4L,R6L
  H'001032  78 60 6A 2E 00 00 23 78   MOV.B @(H'002378:24,ER6),R6L
  H'00103A  6D F6                     PUSH.W R6
  H'00103C  5C 00 F4 0C               BSR read_serial_data_byte:16
  H'001040  6D 75                     POP.W R5
  H'001042  1D 65                     CMP.W R6,R5
  H'001044  46 1E                     BNE H'001064
  H'001046  1A E6                     SUB.L ER6,ER6
  H'001048  0C CE                     MOV.B R4L,R6L
  H'00104A  78 60 6A 2E 00 00 23 78   MOV.B @(H'002378:24,ER6),R6L
  H'001052  5C 00 F4 1C               BSR send_serial_data_byte:16
  H'001056  0A 0C                     INC.B R4L
  H'001058  AC 02                     CMP.B #H'02,R4L
  H'00105A  46 06                     BNE H'001062
  H'00105C  79 06 00 01               MOV.W #H'0001,R6
  H'001060  40 22                     BRA H'001084
  H'001062  40 08                     BRA H'00106C
  H'001064  FE 51                     MOV.B #H'51,R6L
  H'001066  5C 00 F4 08               BSR send_serial_data_byte:16
  H'00106A  18 CC                     SUB.B R4L,R4L
  H'00106C  40 04                     BRA H'001072
  H'00106E  5C 00 F4 7C               BSR serial_clear_rx_errors:16
  H'001072  0B 73                     INC.L #1,ER3
  H'001074  58 00 FF 52               BRA H'000FCA:16
  H'001078  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'00107E  7D 60 72 10               BCLR #1,@ER6
  H'001082  19 66                     SUB.W R6,R6
  H'001084  6D 7E                     POP.W E6
  H'001086  6D 75                     POP.W R5
  H'001088  6D 74                     POP.W R4
  H'00108A  01 00 6D 73               POP.L ER3
  H'00108E  54 70                     RTS

tramp_vec1:
  H'001090  6D F4                     PUSH.W R4
  H'001092  01 00 6D F5               PUSH.L ER5
  H'001096  01 00 6D F6               PUSH.L ER6
  H'00109A  5C 00 F4 F2               BSR get_sci_tx_ready_bit:16
  H'00109E  0D 66                     MOV.W R6,R6
  H'0010A0  46 04                     BNE H'0010A6
  H'0010A2  58 00 0F 80               BRA H'002026:16
  H'0010A6  5C 00 F3 F0               BSR sub_00049A:16
  H'0010AA  79 26 00 01               CMP.W #H'0001,R6
  H'0010AE  58 60 0F 74               BNE H'002026:16
  H'0010B2  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'0010B8  AE F6                     CMP.B #H'F6,R6L
  H'0010BA  58 20 0F 60               BHI H'00201E:16
  H'0010BE  17 56                     EXTU.W R6
  H'0010C0  17 76                     EXTU.L ER6
  H'0010C2  0A E6                     ADD.L ER6,ER6
  H'0010C4  0A E6                     ADD.L ER6,ER6
  H'0010C6  01 00 78 60 6B 26 00 00 10 D2MOV.L @(H'0010D2:24,ER6),ER6
  H'0010D0  59 60                     JMP @ER6
  H'0010D2  00 00                     NOP
  H'0010D4  14 AE                     OR.B R2L,R6L
  H'0010D6  00 00                     NOP
  H'0010D8  16 58                     AND.B R5H,R0L
  H'0010DA  00 00                     NOP
  H'0010DC  16 58                     AND.B R5H,R0L
  H'0010DE  00 00                     NOP
  H'0010E0  16 58                     AND.B R5H,R0L
  H'0010E2  00 00                     NOP
  H'0010E4  16 58                     AND.B R5H,R0L
  H'0010E6  00 00                     NOP
  H'0010E8  16 58                     AND.B R5H,R0L
  H'0010EA  00 00                     NOP
  H'0010EC  16 58                     AND.B R5H,R0L
  H'0010EE  00 00                     NOP
  H'0010F0  16 8E                     AND.B R0L,R6L
  H'0010F2  00 00                     NOP
  H'0010F4  16 D6                     AND.B R5L,R6H
  H'0010F6  00 00                     NOP
  H'0010F8  17 42                     .WORD sub_001742
  H'0010FA  00 00                     NOP
  H'0010FC  17 42                     .WORD sub_001742
  H'0010FE  00 00                     NOP
  H'001100  17 42                     .WORD sub_001742
  H'001102  00 00                     NOP
  H'001104  17 42                     .WORD sub_001742
  H'001106  00 00                     NOP
  H'001108  17 42                     .WORD sub_001742
  H'00110A  00 00                     NOP
  H'00110C  17 42                     .WORD sub_001742
  H'00110E  00 00                     NOP
  H'001110  17 78                     .WORD sub_001778
  H'001112  00 00                     NOP
  H'001114  17 78                     .WORD sub_001778
  H'001116  00 00                     NOP
  H'001118  17 A2                     .WORD sub_0017A2
  H'00111A  00 00                     NOP
  H'00111C  18 00                     SUB.B R0H,R0H
  H'00111E  00 00                     NOP
  H'001120  18 12                     SUB.B R1H,R2H
  H'001122  00 00                     NOP
  H'001124  18 24                     SUB.B R2H,R4H
  H'001126  00 00                     NOP
  H'001128  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00112A  00 00                     NOP
  H'00112C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00112E  00 00                     NOP
  H'001130  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001132  00 00                     NOP
  H'001134  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001136  00 00                     NOP
  H'001138  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00113A  00 00                     NOP
  H'00113C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00113E  00 00                     NOP
  H'001140  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001142  00 00                     NOP
  H'001144  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001146  00 00                     NOP
  H'001148  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00114A  00 00                     NOP
  H'00114C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00114E  00 00                     NOP
  H'001150  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001152  00 00                     NOP
  H'001154  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001156  00 00                     NOP
  H'001158  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00115A  00 00                     NOP
  H'00115C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00115E  00 00                     NOP
  H'001160  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001162  00 00                     NOP
  H'001164  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001166  00 00                     NOP
  H'001168  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00116A  00 00                     NOP
  H'00116C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00116E  00 00                     NOP
  H'001170  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001172  00 00                     NOP
  H'001174  18 36                     SUB.B R3H,R6H
  H'001176  00 00                     NOP
  H'001178  18 36                     SUB.B R3H,R6H
  H'00117A  00 00                     NOP
  H'00117C  18 60                     SUB.B R6H,R0H
  H'00117E  00 00                     NOP
  H'001180  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001182  00 00                     NOP
  H'001184  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001186  00 00                     NOP
  H'001188  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00118A  00 00                     NOP
  H'00118C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00118E  00 00                     NOP
  H'001190  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001192  00 00                     NOP
  H'001194  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001196  00 00                     NOP
  H'001198  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00119A  00 00                     NOP
  H'00119C  18 9C                     SUB.B R1L,R4L
  H'00119E  00 00                     NOP
  H'0011A0  18 BA                     SUB.B R3L,R2L
  H'0011A2  00 00                     NOP
  H'0011A4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011A6  00 00                     NOP
  H'0011A8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011AA  00 00                     NOP
  H'0011AC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011AE  00 00                     NOP
  H'0011B0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011B2  00 00                     NOP
  H'0011B4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011B6  00 00                     NOP
  H'0011B8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011BA  00 00                     NOP
  H'0011BC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011BE  00 00                     NOP
  H'0011C0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011C2  00 00                     NOP
  H'0011C4  18 D0                     SUB.B R5L,R0H
  H'0011C6  00 00                     NOP
  H'0011C8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011CA  00 00                     NOP
  H'0011CC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011CE  00 00                     NOP
  H'0011D0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011D2  00 00                     NOP
  H'0011D4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011D6  00 00                     NOP
  H'0011D8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011DA  00 00                     NOP
  H'0011DC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011DE  00 00                     NOP
  H'0011E0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011E2  00 00                     NOP
  H'0011E4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011E6  00 00                     NOP
  H'0011E8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011EA  00 00                     NOP
  H'0011EC  18 E4                     SUB.B R6L,R4H
  H'0011EE  00 00                     NOP
  H'0011F0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011F2  00 00                     NOP
  H'0011F4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011F6  00 00                     NOP
  H'0011F8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011FA  00 00                     NOP
  H'0011FC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0011FE  00 00                     NOP
  H'001200  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001202  00 00                     NOP
  H'001204  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001206  00 00                     NOP
  H'001208  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00120A  00 00                     NOP
  H'00120C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00120E  00 00                     NOP
  H'001210  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001212  00 00                     NOP
  H'001214  18 F6                     SUB.B R7L,R6H
  H'001216  00 00                     NOP
  H'001218  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00121A  00 00                     NOP
  H'00121C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00121E  00 00                     NOP
  H'001220  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001222  00 00                     NOP
  H'001224  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001226  00 00                     NOP
  H'001228  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00122A  00 00                     NOP
  H'00122C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00122E  00 00                     NOP
  H'001230  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001232  00 00                     NOP
  H'001234  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001236  00 00                     NOP
  H'001238  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00123A  00 00                     NOP
  H'00123C  19 20                     SUB.W R2,R0
  H'00123E  00 00                     NOP
  H'001240  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001242  00 00                     NOP
  H'001244  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001246  00 00                     NOP
  H'001248  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00124A  00 00                     NOP
  H'00124C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00124E  00 00                     NOP
  H'001250  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001252  00 00                     NOP
  H'001254  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001256  00 00                     NOP
  H'001258  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00125A  00 00                     NOP
  H'00125C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00125E  00 00                     NOP
  H'001260  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001262  00 00                     NOP
  H'001264  19 62                     SUB.W R6,R2
  H'001266  00 00                     NOP
  H'001268  19 62                     SUB.W R6,R2
  H'00126A  00 00                     NOP
  H'00126C  19 98                     SUB.W E1,E0
  H'00126E  00 00                     NOP
  H'001270  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001272  00 00                     NOP
  H'001274  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001276  00 00                     NOP
  H'001278  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00127A  00 00                     NOP
  H'00127C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00127E  00 00                     NOP
  H'001280  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001282  00 00                     NOP
  H'001284  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001286  00 00                     NOP
  H'001288  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00128A  00 00                     NOP
  H'00128C  1A 1C                     .WORD sub_001A1C
  H'00128E  00 00                     NOP
  H'001290  1A 1C                     .WORD sub_001A1C
  H'001292  00 00                     NOP
  H'001294  1A 1C                     .WORD sub_001A1C
  H'001296  00 00                     NOP
  H'001298  1A 1C                     .WORD sub_001A1C
  H'00129A  00 00                     NOP
  H'00129C  1A 52                     .WORD sub_001A52
  H'00129E  00 00                     NOP
  H'0012A0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012A2  00 00                     NOP
  H'0012A4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012A6  00 00                     NOP
  H'0012A8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012AA  00 00                     NOP
  H'0012AC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012AE  00 00                     NOP
  H'0012B0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012B2  00 00                     NOP
  H'0012B4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012B6  00 00                     NOP
  H'0012B8  1A AE                     .WORD sub_001AAE
  H'0012BA  00 00                     NOP
  H'0012BC  1A AE                     .WORD sub_001AAE
  H'0012BE  00 00                     NOP
  H'0012C0  1A AE                     .WORD sub_001AAE
  H'0012C2  00 00                     NOP
  H'0012C4  1A AE                     .WORD sub_001AAE
  H'0012C6  00 00                     NOP
  H'0012C8  1A AE                     .WORD sub_001AAE
  H'0012CA  00 00                     NOP
  H'0012CC  1A AE                     .WORD sub_001AAE
  H'0012CE  00 00                     NOP
  H'0012D0  1A E4                     SUB.L ER6,ER4
  H'0012D2  00 00                     NOP
  H'0012D4  1A E4                     SUB.L ER6,ER4
  H'0012D6  00 00                     NOP
  H'0012D8  1B 0E                     .WORD sub_001B0E
  H'0012DA  00 00                     NOP
  H'0012DC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012DE  00 00                     NOP
  H'0012E0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012E2  00 00                     NOP
  H'0012E4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012E6  00 00                     NOP
  H'0012E8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012EA  00 00                     NOP
  H'0012EC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012EE  00 00                     NOP
  H'0012F0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012F2  00 00                     NOP
  H'0012F4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012F6  00 00                     NOP
  H'0012F8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012FA  00 00                     NOP
  H'0012FC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0012FE  00 00                     NOP
  H'001300  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001302  00 00                     NOP
  H'001304  1B 84                     SUBS #2,ER4
  H'001306  00 00                     NOP
  H'001308  1B 84                     SUBS #2,ER4
  H'00130A  00 00                     NOP
  H'00130C  1B 84                     SUBS #2,ER4
  H'00130E  00 00                     NOP
  H'001310  1B 84                     SUBS #2,ER4
  H'001312  00 00                     NOP
  H'001314  1B 84                     SUBS #2,ER4
  H'001316  00 00                     NOP
  H'001318  1B 84                     SUBS #2,ER4
  H'00131A  00 00                     NOP
  H'00131C  1B BA                     .WORD sub_001BBA
  H'00131E  00 00                     NOP
  H'001320  1B FA                     .WORD sub_001BFA
  H'001322  00 00                     NOP
  H'001324  1B FA                     .WORD sub_001BFA
  H'001326  00 00                     NOP
  H'001328  1C 24                     CMP.B R2H,R4H
  H'00132A  00 00                     NOP
  H'00132C  1C 8A                     CMP.B R0L,R2L
  H'00132E  00 00                     NOP
  H'001330  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001332  00 00                     NOP
  H'001334  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001336  00 00                     NOP
  H'001338  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00133A  00 00                     NOP
  H'00133C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00133E  00 00                     NOP
  H'001340  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001342  00 00                     NOP
  H'001344  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001346  00 00                     NOP
  H'001348  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00134A  00 00                     NOP
  H'00134C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00134E  00 00                     NOP
  H'001350  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001352  00 00                     NOP
  H'001354  1F 8C                     .WORD sub_001F8C
  H'001356  00 00                     NOP
  H'001358  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00135A  00 00                     NOP
  H'00135C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00135E  00 00                     NOP
  H'001360  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001362  00 00                     NOP
  H'001364  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001366  00 00                     NOP
  H'001368  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00136A  00 00                     NOP
  H'00136C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00136E  00 00                     NOP
  H'001370  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001372  00 00                     NOP
  H'001374  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001376  00 00                     NOP
  H'001378  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00137A  00 00                     NOP
  H'00137C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00137E  00 00                     NOP
  H'001380  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001382  00 00                     NOP
  H'001384  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001386  00 00                     NOP
  H'001388  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00138A  00 00                     NOP
  H'00138C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00138E  00 00                     NOP
  H'001390  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001392  00 00                     NOP
  H'001394  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001396  00 00                     NOP
  H'001398  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00139A  00 00                     NOP
  H'00139C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00139E  00 00                     NOP
  H'0013A0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013A2  00 00                     NOP
  H'0013A4  1C AC                     CMP.B R2L,R4L
  H'0013A6  00 00                     NOP
  H'0013A8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013AA  00 00                     NOP
  H'0013AC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013AE  00 00                     NOP
  H'0013B0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013B2  00 00                     NOP
  H'0013B4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013B6  00 00                     NOP
  H'0013B8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013BA  00 00                     NOP
  H'0013BC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013BE  00 00                     NOP
  H'0013C0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013C2  00 00                     NOP
  H'0013C4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013C6  00 00                     NOP
  H'0013C8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013CA  00 00                     NOP
  H'0013CC  1C E4                     CMP.B R6L,R4H
  H'0013CE  00 00                     NOP
  H'0013D0  1D 12                     CMP.W R1,R2
  H'0013D2  00 00                     NOP
  H'0013D4  1D 40                     CMP.W R4,R0
  H'0013D6  00 00                     NOP
  H'0013D8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013DA  00 00                     NOP
  H'0013DC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013DE  00 00                     NOP
  H'0013E0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013E2  00 00                     NOP
  H'0013E4  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013E6  00 00                     NOP
  H'0013E8  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013EA  00 00                     NOP
  H'0013EC  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013EE  00 00                     NOP
  H'0013F0  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'0013F2  00 00                     NOP
  H'0013F4  1D 80                     CMP.W E0,R0
  H'0013F6  00 00                     NOP
  H'0013F8  1D 80                     CMP.W E0,R0
  H'0013FA  00 00                     NOP
  H'0013FC  1D 80                     CMP.W E0,R0
  H'0013FE  00 00                     NOP
  H'001400  1D 80                     CMP.W E0,R0
  H'001402  00 00                     NOP
  H'001404  1D 80                     CMP.W E0,R0
  H'001406  00 00                     NOP
  H'001408  1D 80                     CMP.W E0,R0
  H'00140A  00 00                     NOP
  H'00140C  1D B6                     CMP.W E3,R6
  H'00140E  00 00                     NOP
  H'001410  1D E0                     CMP.W E6,R0
  H'001412  00 00                     NOP
  H'001414  1D E0                     CMP.W E6,R0
  H'001416  00 00                     NOP
  H'001418  1D E0                     CMP.W E6,R0
  H'00141A  00 00                     NOP
  H'00141C  1D E0                     CMP.W E6,R0
  H'00141E  00 00                     NOP
  H'001420  1D E0                     CMP.W E6,R0
  H'001422  00 00                     NOP
  H'001424  1D E0                     CMP.W E6,R0
  H'001426  00 00                     NOP
  H'001428  1E 16                     SUBX R1H,R6H
  H'00142A  00 00                     NOP
  H'00142C  1E 7A                     SUBX R7H,R2L
  H'00142E  00 00                     NOP
  H'001430  1E A2                     SUBX R2L,R2H
  H'001432  00 00                     NOP
  H'001434  1E CA                     SUBX R4L,R2L
  H'001436  00 00                     NOP
  H'001438  1E F2                     SUBX R7L,R2H
  H'00143A  00 00                     NOP
  H'00143C  1F 12                     .WORD H'1F12
  H'00143E  00 00                     NOP
  H'001440  1F 36                     .WORD H'1F36
  H'001442  00 00                     NOP
  H'001444  1F 4E                     .WORD H'1F4E
  H'001446  00 00                     NOP
  H'001448  1F 76                     .WORD sub_001F76
  H'00144A  00 00                     NOP
  H'00144C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00144E  00 00                     NOP
  H'001450  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001452  00 00                     NOP
  H'001454  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001456  00 00                     NOP
  H'001458  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00145A  00 00                     NOP
  H'00145C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00145E  00 00                     NOP
  H'001460  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001462  00 00                     NOP
  H'001464  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001466  00 00                     NOP
  H'001468  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00146A  00 00                     NOP
  H'00146C  1F 9C                     .WORD sub_001F9C
  H'00146E  00 00                     NOP
  H'001470  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001472  00 00                     NOP
  H'001474  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001476  00 00                     NOP
  H'001478  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00147A  00 00                     NOP
  H'00147C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00147E  00 00                     NOP
  H'001480  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001482  00 00                     NOP
  H'001484  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001486  00 00                     NOP
  H'001488  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00148A  00 00                     NOP
  H'00148C  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'00148E  00 00                     NOP
  H'001490  20 1E                     MOV.B @H'FFFF1E:8,R0H
  H'001492  00 00                     NOP
  H'001494  1F AE                     .WORD sub_001FAE
  H'001496  00 00                     NOP
  H'001498  1F AE                     .WORD sub_001FAE
  H'00149A  00 00                     NOP
  H'00149C  1F AE                     .WORD sub_001FAE
  H'00149E  00 00                     NOP
  H'0014A0  1F AE                     .WORD sub_001FAE
  H'0014A2  00 00                     NOP
  H'0014A4  1F AE                     .WORD sub_001FAE
  H'0014A6  00 00                     NOP
  H'0014A8  1F AE                     .WORD sub_001FAE
  H'0014AA  00 00                     NOP
  H'0014AC  1F E2                     CMP.L ER6,ER2

sub_0014AE:
  H'0014AE  5C 00 F0 C4               BSR get_sci_rx_ready_bit:16
  H'0014B2  0D 66                     MOV.W R6,R6
  H'0014B4  58 70 01 9C               BEQ H'001654:16
  H'0014B8  5C 00 EF 90               BSR read_serial_data_byte:16
  H'0014BC  0C E4                     MOV.B R6L,R4H
  H'0014BE  1A D5                     SUB.L ER5,ER5
  H'0014C0  FD 26                     MOV.B #H'26,R5L
  H'0014C2  7A 06 00 00 14 E5         MOV.L #H'000014E5,ER6
  H'0014C8  6C 6C                     MOV.B @ER6+,R4L
  H'0014CA  1C 4C                     CMP.B R4H,R4L
  H'0014CC  47 08                     BEQ H'0014D6
  H'0014CE  1B F5                     DEC.L #2,ER5
  H'0014D0  46 F6                     BNE H'0014C8
  H'0014D2  58 00 01 66               BRA H'00163C:16
  H'0014D6  0A D5                     ADD.L ER5,ER5
  H'0014D8  01 00 78 50 6B 25 00 00 14 F4MOV.L @(H'0014F4:24,ER5),ER5
  H'0014E2  59 50                     JMP @ER5
  H'0014E4  00 47                     .WORD H'0047
  H'0014E6  48 49                     BVC H'001531
  H'0014E8  4A 4B                     BPL H'001535
  H'0014EA  4C 4D                     BGE H'001539
  H'0014EC  4E 50                     BGT H'00153E
  H'0014EE  52 53                     MULXU.W R5,ER3
  H'0014F0  54 56                     .WORD H'5456
  H'0014F2  57 58                     .WORD H'5758
  H'0014F4  59 5A                     .WORD H'595A
  H'0014F6  72 77                     BCLR #7,R7H
  H'0014F8  00 00                     NOP
  H'0014FA  15 6A                     XOR.B R6H,R2L
  H'0014FC  00 00                     NOP
  H'0014FE  15 58                     XOR.B R5H,R0L
  H'001500  00 00                     NOP
  H'001502  15 D8                     XOR.B R5L,R0L
  H'001504  00 00                     NOP
  H'001506  15 E2                     XOR.B R6L,R2H
  H'001508  00 00                     NOP
  H'00150A  15 C0                     XOR.B R4L,R0H
  H'00150C  00 00                     NOP
  H'00150E  15 7E                     XOR.B R7H,R6L
  H'001510  00 00                     NOP
  H'001512  15 AA                     XOR.B R2L,R2L
  H'001514  00 00                     NOP
  H'001516  15 EC                     XOR.B R6L,R4L
  H'001518  00 00                     NOP
  H'00151A  16 28                     AND.B R2H,R0L
  H'00151C  00 00                     NOP
  H'00151E  15 44                     XOR.B R4H,R4H
  H'001520  00 00                     NOP
  H'001522  15 92                     XOR.B R1L,R2H
  H'001524  00 00                     NOP
  H'001526  15 B6                     XOR.B R3L,R6H
  H'001528  00 00                     NOP
  H'00152A  15 F6                     XOR.B R7L,R6H
  H'00152C  00 00                     NOP
  H'00152E  16 0A                     AND.B R0H,R2L
  H'001530  00 00                     NOP
  H'001532  15 CA                     XOR.B R4L,R2L
  H'001534  00 00                     NOP
  H'001536  15 9E                     XOR.B R1L,R6L
  H'001538  00 00                     NOP
  H'00153A  15 CE                     XOR.B R4L,R6L
  H'00153C  00 00                     NOP
  H'00153E  16 00                     AND.B R0H,R0H
  H'001540  00 00                     NOP
  H'001542  16 1E                     AND.B R1H,R6L
  H'001544  FE 01                     MOV.B #H'01,R6L
  H'001546  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'00154C  FE 20                     MOV.B #H'20,R6L
  H'00154E  6A AE 00 FF FD 1F         MOV.B R6L,@H'FFFD1F:24
  H'001554  58 00 00 E6               BRA H'00163E:16
  H'001558  FE 01                     MOV.B #H'01,R6L
  H'00155A  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001560  6A AE 00 FF FD 1F         MOV.B R6L,@H'FFFD1F:24
  H'001566  58 00 00 D4               BRA H'00163E:16
  H'00156A  FE 09                     MOV.B #H'09,R6L
  H'00156C  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001572  FE 01                     MOV.B #H'01,R6L
  H'001574  6A AE 00 FF FD 1F         MOV.B R6L,@H'FFFD1F:24
  H'00157A  58 00 00 C0               BRA H'00163E:16
  H'00157E  FE 09                     MOV.B #H'09,R6L
  H'001580  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001586  FE FF                     MOV.B #H'FF,R6L
  H'001588  6A AE 00 FF FD 1F         MOV.B R6L,@H'FFFD1F:24
  H'00158E  58 00 00 AC               BRA H'00163E:16
  H'001592  FE 5A                     MOV.B #H'5A,R6L
  H'001594  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'00159A  58 00 00 A0               BRA H'00163E:16
  H'00159E  FE 28                     MOV.B #H'28,R6L
  H'0015A0  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0015A6  58 00 00 94               BRA H'00163E:16
  H'0015AA  FE 32                     MOV.B #H'32,R6L
  H'0015AC  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0015B2  58 00 00 88               BRA H'00163E:16
  H'0015B6  FE F0                     MOV.B #H'F0,R6L
  H'0015B8  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0015BE  40 7E                     BRA H'00163E
  H'0015C0  FE 3C                     MOV.B #H'3C,R6L
  H'0015C2  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0015C8  40 74                     BRA H'00163E
  H'0015CA  F4 4F                     MOV.B #H'4F,R4H
  H'0015CC  40 70                     BRA H'00163E
  H'0015CE  FE 50                     MOV.B #H'50,R6L
  H'0015D0  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0015D6  40 66                     BRA H'00163E
  H'0015D8  FE 79                     MOV.B #H'79,R6L
  H'0015DA  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0015E0  40 5C                     BRA H'00163E
  H'0015E2  FE B4                     MOV.B #H'B4,R6L
  H'0015E4  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0015EA  40 52                     BRA H'00163E
  H'0015EC  FE BE                     MOV.B #H'BE,R6L
  H'0015EE  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0015F4  40 48                     BRA H'00163E
  H'0015F6  FE 8C                     MOV.B #H'8C,R6L
  H'0015F8  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0015FE  40 3E                     BRA H'00163E
  H'001600  FE E6                     MOV.B #H'E6,R6L
  H'001602  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001608  40 34                     BRA H'00163E
  H'00160A  FE C8                     MOV.B #H'C8,R6L
  H'00160C  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001612  1A E6                     SUB.L ER6,ER6
  H'001614  01 00 6B A6 00 FF FD 14   MOV.L ER6,@H'FFFD14:24
  H'00161C  40 20                     BRA H'00163E
  H'00161E  FE A0                     MOV.B #H'A0,R6L
  H'001620  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001626  40 16                     BRA H'00163E
  H'001628  FE 53                     MOV.B #H'53,R6L
  H'00162A  5C 00 EE 44               BSR send_serial_data_byte:16
  H'00162E  79 06 00 0A               MOV.W #H'000A,R6
  H'001632  5C 00 ED F0               BSR tramp_vec2:16
  H'001636  5E 00 04 12               JSR @sub_000412:24
  H'00163A  40 02                     BRA H'00163E
  H'00163C  F4 51                     MOV.B #H'51,R4H
  H'00163E  0C 4E                     MOV.B R4H,R6L
  H'001640  5C 00 EE 2E               BSR send_serial_data_byte:16
  H'001644  1A E6                     SUB.L ER6,ER6
  H'001646  01 00 6B A6 00 FF FD 14   MOV.L ER6,@H'FFFD14:24
  H'00164E  6A AE 00 FF FD 20         MOV.B R6L,@H'FFFD20:24
  H'001654  58 00 09 CE               BRA H'002026:16

sub_001658:
  H'001658  5C 00 EF 1A               BSR get_sci_rx_ready_bit:16
  H'00165C  0D 66                     MOV.W R6,R6
  H'00165E  47 2A                     BEQ H'00168A
  H'001660  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001668  10 36                     SHLL.L ER6
  H'00166A  10 36                     SHLL.L ER6
  H'00166C  10 36                     SHLL.L ER6
  H'00166E  10 36                     SHLL.L ER6
  H'001670  01 00 6D F6               PUSH.L ER6
  H'001674  5C 00 EF 32               BSR sub_0005AA:16
  H'001678  18 66                     SUB.B R6H,R6H
  H'00167A  17 76                     EXTU.L ER6
  H'00167C  01 00 6D 75               POP.L ER5
  H'001680  0A E5                     ADD.L ER6,ER5
  H'001682  01 00 6B A5 00 FF FD 14   MOV.L ER5,@H'FFFD14:24
  H'00168A  58 00 09 98               BRA H'002026:16

sub_00168E:
  H'00168E  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001696  7A 26 00 20 40 00         CMP.L #H'00204000,ER6
  H'00169C  45 12                     BCS H'0016B0
  H'00169E  7A 26 00 20 80 00         CMP.L #H'00208000,ER6
  H'0016A4  44 0A                     BCC H'0016B0
  H'0016A6  FE FF                     MOV.B #H'FF,R6L
  H'0016A8  6A AE 00 FF FD 20         MOV.B R6L,@H'FFFD20:24
  H'0016AE  40 08                     BRA H'0016B8
  H'0016B0  68 6D                     MOV.B @ER6,R5L
  H'0016B2  6A AD 00 FF FD 20         MOV.B R5L,@H'FFFD20:24
  H'0016B8  6A 2E 00 FF FD 20         MOV.B @H'FFFD20:24,R6L
  H'0016BE  11 0E                     SHLR.B R6L
  H'0016C0  11 0E                     SHLR.B R6L
  H'0016C2  11 0E                     SHLR.B R6L
  H'0016C4  11 0E                     SHLR.B R6L
  H'0016C6  5C 00 EF 76               BSR sub_000640:16
  H'0016CA  FE 08                     MOV.B #H'08,R6L
  H'0016CC  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0016D2  58 00 09 50               BRA H'002026:16

sub_0016D6:
  H'0016D6  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'0016DE  7A 26 00 20 40 00         CMP.L #H'00204000,ER6
  H'0016E4  45 12                     BCS H'0016F8
  H'0016E6  7A 26 00 20 80 00         CMP.L #H'00208000,ER6
  H'0016EC  44 0A                     BCC H'0016F8
  H'0016EE  FE FF                     MOV.B #H'FF,R6L
  H'0016F0  6A AE 00 FF FD 20         MOV.B R6L,@H'FFFD20:24
  H'0016F6  40 08                     BRA H'001700
  H'0016F8  68 6D                     MOV.B @ER6,R5L
  H'0016FA  6A AD 00 FF FD 20         MOV.B R5L,@H'FFFD20:24
  H'001700  6A 2E 00 FF FD 20         MOV.B @H'FFFD20:24,R6L
  H'001706  5C 00 EF 36               BSR sub_000640:16
  H'00170A  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001712  0B 76                     INC.L #1,ER6
  H'001714  01 00 6B A6 00 FF FD 14   MOV.L ER6,@H'FFFD14:24
  H'00171C  6A 2E 00 FF FD 1F         MOV.B @H'FFFD1F:24,R6L
  H'001722  1A 0E                     DEC.B R6L
  H'001724  6A AE 00 FF FD 1F         MOV.B R6L,@H'FFFD1F:24
  H'00172A  47 0A                     BEQ H'001736
  H'00172C  FE 07                     MOV.B #H'07,R6L
  H'00172E  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001734  40 08                     BRA H'00173E
  H'001736  FE 12                     MOV.B #H'12,R6L
  H'001738  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'00173E  58 00 08 E4               BRA H'002026:16

sub_001742:
  H'001742  5C 00 EE 30               BSR get_sci_rx_ready_bit:16
  H'001746  0D 66                     MOV.W R6,R6
  H'001748  47 2A                     BEQ H'001774
  H'00174A  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001752  10 36                     SHLL.L ER6
  H'001754  10 36                     SHLL.L ER6
  H'001756  10 36                     SHLL.L ER6
  H'001758  10 36                     SHLL.L ER6
  H'00175A  01 00 6D F6               PUSH.L ER6
  H'00175E  5C 00 EE 48               BSR sub_0005AA:16
  H'001762  18 66                     SUB.B R6H,R6H
  H'001764  17 76                     EXTU.L ER6
  H'001766  01 00 6D 75               POP.L ER5
  H'00176A  0A E5                     ADD.L ER6,ER5
  H'00176C  01 00 6B A5 00 FF FD 14   MOV.L ER5,@H'FFFD14:24
  H'001774  58 00 08 AE               BRA H'002026:16

sub_001778:
  H'001778  5C 00 ED FA               BSR get_sci_rx_ready_bit:16
  H'00177C  0D 66                     MOV.W R6,R6
  H'00177E  47 1E                     BEQ H'00179E
  H'001780  6A 2E 00 FF FD 20         MOV.B @H'FFFD20:24,R6L
  H'001786  10 0E                     SHLL.B R6L
  H'001788  10 0E                     SHLL.B R6L
  H'00178A  10 0E                     SHLL.B R6L
  H'00178C  10 0E                     SHLL.B R6L
  H'00178E  6D F6                     PUSH.W R6
  H'001790  5C 00 EE 16               BSR sub_0005AA:16
  H'001794  6D 75                     POP.W R5
  H'001796  08 ED                     ADD.B R6L,R5L
  H'001798  6A AD 00 FF FD 20         MOV.B R5L,@H'FFFD20:24
  H'00179E  58 00 08 84               BRA H'002026:16

sub_0017A2:
  H'0017A2  FE 13                     MOV.B #H'13,R6L
  H'0017A4  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0017AA  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'0017B2  6A 2D 00 FF FD 20         MOV.B @H'FFFD20:24,R5L
  H'0017B8  68 ED                     MOV.B R5L,@ER6
  H'0017BA  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'0017C2  68 65                     MOV.B @ER6,R5H
  H'0017C4  1C D5                     CMP.B R5L,R5H
  H'0017C6  46 08                     BNE H'0017D0
  H'0017C8  FE 12                     MOV.B #H'12,R6L
  H'0017CA  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0017D0  6A 2E 00 FF FD 1F         MOV.B @H'FFFD1F:24,R6L
  H'0017D6  AE FF                     CMP.B #H'FF,R6L
  H'0017D8  46 22                     BNE H'0017FC
  H'0017DA  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'0017E2  0B 76                     INC.L #1,ER6
  H'0017E4  01 00 6B A6 00 FF FD 14   MOV.L ER6,@H'FFFD14:24
  H'0017EC  18 EE                     SUB.B R6L,R6L
  H'0017EE  6A AE 00 FF FD 20         MOV.B R6L,@H'FFFD20:24
  H'0017F4  FE 0F                     MOV.B #H'0F,R6L
  H'0017F6  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0017FC  58 00 08 26               BRA H'002026:16

sub_001800:
  H'001800  FE 4F                     MOV.B #H'4F,R6L
  H'001802  5C 00 EC 6C               BSR send_serial_data_byte:16
  H'001806  18 EE                     SUB.B R6L,R6L
  H'001808  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'00180E  58 00 08 14               BRA H'002026:16

sub_001812:
  H'001812  FE 4E                     MOV.B #H'4E,R6L
  H'001814  5C 00 EC 5A               BSR send_serial_data_byte:16
  H'001818  18 EE                     SUB.B R6L,R6L
  H'00181A  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001820  58 00 08 02               BRA H'002026:16

sub_001824:
  H'001824  FE 56                     MOV.B #H'56,R6L
  H'001826  5C 00 EC 48               BSR send_serial_data_byte:16
  H'00182A  18 EE                     SUB.B R6L,R6L
  H'00182C  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001832  58 00 07 F0               BRA H'002026:16

sub_001836:
  H'001836  5C 00 ED 3C               BSR get_sci_rx_ready_bit:16
  H'00183A  0D 66                     MOV.W R6,R6
  H'00183C  47 1E                     BEQ H'00185C
  H'00183E  6A 2E 00 FF FD 20         MOV.B @H'FFFD20:24,R6L
  H'001844  10 0E                     SHLL.B R6L
  H'001846  10 0E                     SHLL.B R6L
  H'001848  10 0E                     SHLL.B R6L
  H'00184A  10 0E                     SHLL.B R6L
  H'00184C  6D F6                     PUSH.W R6
  H'00184E  5C 00 ED 58               BSR sub_0005AA:16
  H'001852  6D 75                     POP.W R5
  H'001854  08 ED                     ADD.B R6L,R5L
  H'001856  6A AD 00 FF FD 20         MOV.B R5L,@H'FFFD20:24
  H'00185C  58 00 07 C6               BRA H'002026:16

sub_001860:
  H'001860  6A 2E 00 FF FD 20         MOV.B @H'FFFD20:24,R6L
  H'001866  5C 00 EE 36               BSR sub_0006A0:16
  H'00186A  79 06 01 F4               MOV.W #H'01F4,R6
  H'00186E  5C 00 EB B4               BSR tramp_vec2:16
  H'001872  5C 00 EE 18               BSR sub_00068E:16
  H'001876  5C 00 F7 3C               BSR sub_000FB6:16
  H'00187A  0D 66                     MOV.W R6,R6
  H'00187C  46 12                     BNE H'001890
  H'00187E  FE 11                     MOV.B #H'11,R6L
  H'001880  5C 00 EE 1C               BSR sub_0006A0:16
  H'001884  5C 00 EE 06               BSR sub_00068E:16
  H'001888  5C 00 F7 2A               BSR sub_000FB6:16
  H'00188C  0D 66                     MOV.W R6,R6
  H'00188E  47 F4                     BEQ H'001884
  H'001890  18 EE                     SUB.B R6L,R6L
  H'001892  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001898  58 00 07 8A               BRA H'002026:16

sub_00189C:
  H'00189C  6A 2E 00 00 23 40         MOV.B @H'002340:24,R6L
  H'0018A2  11 0E                     SHLR.B R6L
  H'0018A4  11 0E                     SHLR.B R6L
  H'0018A6  11 0E                     SHLR.B R6L
  H'0018A8  11 0E                     SHLR.B R6L
  H'0018AA  5C 00 ED 92               BSR sub_000640:16
  H'0018AE  FE 33                     MOV.B #H'33,R6L
  H'0018B0  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0018B6  58 00 07 6C               BRA H'002026:16

sub_0018BA:
  H'0018BA  6A 2E 00 00 23 40         MOV.B @H'002340:24,R6L
  H'0018C0  5C 00 ED 7C               BSR sub_000640:16
  H'0018C4  18 EE                     SUB.B R6L,R6L
  H'0018C6  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0018CC  58 00 07 56               BRA H'002026:16

sub_0018D0:
  H'0018D0  5E 00 04 0A               JSR @sub_00040A:24
  H'0018D4  79 06 00 0A               MOV.W #H'000A,R6
  H'0018D8  5C 00 EB 4A               BSR tramp_vec2:16
  H'0018DC  5E 00 04 00               JSR @boot_reset:24
  H'0018E0  58 00 07 42               BRA H'002026:16

sub_0018E4:
  H'0018E4  FE 4F                     MOV.B #H'4F,R6L
  H'0018E6  5C 00 EB 88               BSR send_serial_data_byte:16
  H'0018EA  18 EE                     SUB.B R6L,R6L
  H'0018EC  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0018F2  58 00 07 30               BRA H'002026:16

sub_0018F6:
  H'0018F6  7A 06 00 00 23 45         MOV.L #H'00002345,ER6
  H'0018FC  5C 00 ED 5C               BSR sub_00065C:16
  H'001900  7A 06 00 00 23 5C         MOV.L #H'0000235C,ER6
  H'001906  5C 00 ED 52               BSR sub_00065C:16
  H'00190A  7A 06 00 00 23 6F         MOV.L #H'0000236F,ER6
  H'001910  5C 00 ED 48               BSR sub_00065C:16
  H'001914  18 EE                     SUB.B R6L,R6L
  H'001916  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'00191C  58 00 07 06               BRA H'002026:16

sub_001920:
  H'001920  5C 00 EC 52               BSR get_sci_rx_ready_bit:16
  H'001924  0D 66                     MOV.W R6,R6
  H'001926  47 36                     BEQ H'00195E
  H'001928  5C 00 EB 20               BSR read_serial_data_byte:16
  H'00192C  0C E4                     MOV.B R6L,R4H
  H'00192E  A4 42                     CMP.B #H'42,R4H
  H'001930  46 10                     BNE H'001942
  H'001932  FE 42                     MOV.B #H'42,R6L
  H'001934  5C 00 EB 3A               BSR send_serial_data_byte:16
  H'001938  FE 64                     MOV.B #H'64,R6L
  H'00193A  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001940  40 1C                     BRA H'00195E
  H'001942  A4 53                     CMP.B #H'53,R4H
  H'001944  46 10                     BNE H'001956
  H'001946  FE 53                     MOV.B #H'53,R6L
  H'001948  5C 00 EB 26               BSR send_serial_data_byte:16
  H'00194C  FE 6E                     MOV.B #H'6E,R6L
  H'00194E  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001954  40 08                     BRA H'00195E
  H'001956  FE 13                     MOV.B #H'13,R6L
  H'001958  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'00195E  58 00 06 C4               BRA H'002026:16

sub_001962:
  H'001962  5C 00 EC 10               BSR get_sci_rx_ready_bit:16
  H'001966  0D 66                     MOV.W R6,R6
  H'001968  47 2A                     BEQ H'001994
  H'00196A  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001972  10 36                     SHLL.L ER6
  H'001974  10 36                     SHLL.L ER6
  H'001976  10 36                     SHLL.L ER6
  H'001978  10 36                     SHLL.L ER6
  H'00197A  01 00 6D F6               PUSH.L ER6
  H'00197E  5C 00 EC 28               BSR sub_0005AA:16
  H'001982  18 66                     SUB.B R6H,R6H
  H'001984  17 76                     EXTU.L ER6
  H'001986  01 00 6D 75               POP.L ER5
  H'00198A  0A E5                     ADD.L ER6,ER5
  H'00198C  01 00 6B A5 00 FF FD 14   MOV.L ER5,@H'FFFD14:24
  H'001994  58 00 06 8E               BRA H'002026:16

sub_001998:
  H'001998  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'0019A0  0D 6E                     MOV.W R6,E6
  H'0019A2  19 66                     SUB.W R6,R6
  H'0019A4  01 00 6B A6 00 FF FD 14   MOV.L ER6,@H'FFFD14:24
  H'0019AC  5C 00 ED D2               BSR sub_000782:16
  H'0019B0  AE 00                     CMP.B #H'00,R6L
  H'0019B2  46 0A                     BNE H'0019BE
  H'0019B4  FE 14                     MOV.B #H'14,R6L
  H'0019B6  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0019BC  40 5A                     BRA H'001A18
  H'0019BE  AE 01                     CMP.B #H'01,R6L
  H'0019C0  46 16                     BNE H'0019D8
  H'0019C2  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'0019CA  5C 00 F1 F6               BSR sub_000BC4:16
  H'0019CE  18 EE                     SUB.B R6L,R6L
  H'0019D0  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0019D6  40 40                     BRA H'001A18
  H'0019D8  AE 06                     CMP.B #H'06,R6L
  H'0019DA  45 1A                     BCS H'0019F6
  H'0019DC  AE 09                     CMP.B #H'09,R6L
  H'0019DE  42 16                     BHI H'0019F6
  H'0019E0  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'0019E8  5C 00 F3 16               BSR sub_000D02:16
  H'0019EC  18 EE                     SUB.B R6L,R6L
  H'0019EE  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'0019F4  40 22                     BRA H'001A18
  H'0019F6  AE 02                     CMP.B #H'02,R6L
  H'0019F8  46 16                     BNE H'001A10
  H'0019FA  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001A02  5C 00 F4 B6               BSR sub_000EBC:16
  H'001A06  18 EE                     SUB.B R6L,R6L
  H'001A08  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001A0E  40 08                     BRA H'001A18
  H'001A10  FE 14                     MOV.B #H'14,R6L
  H'001A12  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001A18  58 00 06 0A               BRA H'002026:16

sub_001A1C:
  H'001A1C  5C 00 EB 56               BSR get_sci_rx_ready_bit:16
  H'001A20  0D 66                     MOV.W R6,R6
  H'001A22  47 2A                     BEQ H'001A4E
  H'001A24  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001A2C  10 36                     SHLL.L ER6
  H'001A2E  10 36                     SHLL.L ER6
  H'001A30  10 36                     SHLL.L ER6
  H'001A32  10 36                     SHLL.L ER6
  H'001A34  01 00 6D F6               PUSH.L ER6
  H'001A38  5C 00 EB 6E               BSR sub_0005AA:16
  H'001A3C  18 66                     SUB.B R6H,R6H
  H'001A3E  17 76                     EXTU.L ER6
  H'001A40  01 00 6D 75               POP.L ER5
  H'001A44  0A E5                     ADD.L ER6,ER5
  H'001A46  01 00 6B A5 00 FF FD 14   MOV.L ER5,@H'FFFD14:24
  H'001A4E  58 00 05 D4               BRA H'002026:16

sub_001A52:
  H'001A52  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001A5A  FD 08                     MOV.B #H'08,R5L
  H'001A5C  10 36                     SHLL.L ER6
  H'001A5E  1A 0D                     DEC.B R5L
  H'001A60  46 FA                     BNE H'001A5C
  H'001A62  01 00 6B A6 00 FF FD 14   MOV.L ER6,@H'FFFD14:24
  H'001A6A  5C 00 ED 14               BSR sub_000782:16
  H'001A6E  AE 01                     CMP.B #H'01,R6L
  H'001A70  46 16                     BNE H'001A88
  H'001A72  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001A7A  5C 00 F3 AE               BSR sub_000E2C:16
  H'001A7E  18 EE                     SUB.B R6L,R6L
  H'001A80  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001A86  40 22                     BRA H'001AAA
  H'001A88  AE 0A                     CMP.B #H'0A,R6L
  H'001A8A  46 16                     BNE H'001AA2
  H'001A8C  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001A94  5C 00 F3 3E               BSR sub_000DD6:16
  H'001A98  18 EE                     SUB.B R6L,R6L
  H'001A9A  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001AA0  40 08                     BRA H'001AAA
  H'001AA2  FE 14                     MOV.B #H'14,R6L
  H'001AA4  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001AAA  58 00 05 78               BRA H'002026:16

sub_001AAE:
  H'001AAE  5C 00 EA C4               BSR get_sci_rx_ready_bit:16
  H'001AB2  0D 66                     MOV.W R6,R6
  H'001AB4  47 2A                     BEQ H'001AE0
  H'001AB6  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001ABE  10 36                     SHLL.L ER6
  H'001AC0  10 36                     SHLL.L ER6
  H'001AC2  10 36                     SHLL.L ER6
  H'001AC4  10 36                     SHLL.L ER6
  H'001AC6  01 00 6D F6               PUSH.L ER6
  H'001ACA  5C 00 EA DC               BSR sub_0005AA:16
  H'001ACE  18 66                     SUB.B R6H,R6H
  H'001AD0  17 76                     EXTU.L ER6
  H'001AD2  01 00 6D 75               POP.L ER5
  H'001AD6  0A E5                     ADD.L ER6,ER5
  H'001AD8  01 00 6B A5 00 FF FD 14   MOV.L ER5,@H'FFFD14:24
  H'001AE0  58 00 05 42               BRA H'002026:16

sub_001AE4:
  H'001AE4  5C 00 EA 8E               BSR get_sci_rx_ready_bit:16
  H'001AE8  0D 66                     MOV.W R6,R6
  H'001AEA  47 1E                     BEQ H'001B0A
  H'001AEC  6A 2E 00 FF FD 20         MOV.B @H'FFFD20:24,R6L
  H'001AF2  10 0E                     SHLL.B R6L
  H'001AF4  10 0E                     SHLL.B R6L
  H'001AF6  10 0E                     SHLL.B R6L
  H'001AF8  10 0E                     SHLL.B R6L
  H'001AFA  6D F6                     PUSH.W R6
  H'001AFC  5C 00 EA AA               BSR sub_0005AA:16
  H'001B00  6D 75                     POP.W R5
  H'001B02  08 ED                     ADD.B R6L,R5L
  H'001B04  6A AD 00 FF FD 20         MOV.B R5L,@H'FFFD20:24
  H'001B0A  58 00 05 18               BRA H'002026:16

sub_001B0E:
  H'001B0E  5E 00 04 0A               JSR @sub_00040A:24
  H'001B12  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001B1A  5C 00 EC 64               BSR sub_000782:16
  H'001B1E  AE 01                     CMP.B #H'01,R6L
  H'001B20  46 52                     BNE H'001B74
  H'001B22  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001B2A  5C 00 ED 74               BSR sub_0008A2:16
  H'001B2E  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001B36  7A 66 00 00 00 FF         AND.L #H'000000FF,ER6
  H'001B3C  01 00 6B 25 00 FF FD 10   MOV.L @H'FFFD10:24,ER5
  H'001B44  0A D6                     ADD.L ER5,ER6
  H'001B46  6A 2D 00 FF FD 20         MOV.B @H'FFFD20:24,R5L
  H'001B4C  68 ED                     MOV.B R5L,@ER6
  H'001B4E  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001B56  5C 00 EF 46               BSR sub_000AA0:16
  H'001B5A  79 26 00 01               CMP.W #H'0001,R6
  H'001B5E  46 0A                     BNE H'001B6A
  H'001B60  FE 12                     MOV.B #H'12,R6L
  H'001B62  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001B68  40 08                     BRA H'001B72
  H'001B6A  FE 14                     MOV.B #H'14,R6L
  H'001B6C  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001B72  40 08                     BRA H'001B7C
  H'001B74  FE 14                     MOV.B #H'14,R6L
  H'001B76  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001B7C  5E 00 04 0E               JSR @sub_00040E:24
  H'001B80  58 00 04 A2               BRA H'002026:16

sub_001B84:
  H'001B84  5C 00 E9 EE               BSR get_sci_rx_ready_bit:16
  H'001B88  0D 66                     MOV.W R6,R6
  H'001B8A  47 2A                     BEQ H'001BB6
  H'001B8C  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001B94  10 36                     SHLL.L ER6
  H'001B96  10 36                     SHLL.L ER6
  H'001B98  10 36                     SHLL.L ER6
  H'001B9A  10 36                     SHLL.L ER6
  H'001B9C  01 00 6D F6               PUSH.L ER6
  H'001BA0  5C 00 EA 06               BSR sub_0005AA:16
  H'001BA4  18 66                     SUB.B R6H,R6H
  H'001BA6  17 76                     EXTU.L ER6
  H'001BA8  01 00 6D 75               POP.L ER5
  H'001BAC  0A E5                     ADD.L ER6,ER5
  H'001BAE  01 00 6B A5 00 FF FD 14   MOV.L ER5,@H'FFFD14:24
  H'001BB6  58 00 04 6C               BRA H'002026:16

sub_001BBA:
  H'001BBA  5E 00 04 0A               JSR @sub_00040A:24
  H'001BBE  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001BC6  5C 00 EB B8               BSR sub_000782:16
  H'001BCA  AE 01                     CMP.B #H'01,R6L
  H'001BCC  46 1C                     BNE H'001BEA
  H'001BCE  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001BD6  5C 00 EC C8               BSR sub_0008A2:16
  H'001BDA  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'001BE0  0A 0E                     INC.B R6L
  H'001BE2  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001BE8  40 08                     BRA H'001BF2
  H'001BEA  FE 14                     MOV.B #H'14,R6L
  H'001BEC  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001BF2  5E 00 04 0E               JSR @sub_00040E:24
  H'001BF6  58 00 04 2C               BRA H'002026:16

sub_001BFA:
  H'001BFA  5C 00 E9 78               BSR get_sci_rx_ready_bit:16
  H'001BFE  0D 66                     MOV.W R6,R6
  H'001C00  47 1E                     BEQ H'001C20
  H'001C02  79 06 00 96               MOV.W #H'0096,R6
  H'001C06  5C 00 E9 E6               BSR sub_0005F0:16
  H'001C0A  6A 2D 00 FF FD 20         MOV.B @H'FFFD20:24,R5L
  H'001C10  10 0D                     SHLL.B R5L
  H'001C12  10 0D                     SHLL.B R5L
  H'001C14  10 0D                     SHLL.B R5L
  H'001C16  10 0D                     SHLL.B R5L
  H'001C18  08 DE                     ADD.B R5L,R6L
  H'001C1A  6A AE 00 FF FD 20         MOV.B R6L,@H'FFFD20:24
  H'001C20  58 00 04 02               BRA H'002026:16

sub_001C24:
  H'001C24  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001C2C  7A 66 00 00 00 FF         AND.L #H'000000FF,ER6
  H'001C32  01 00 6B 25 00 FF FD 10   MOV.L @H'FFFD10:24,ER5
  H'001C3A  0A D6                     ADD.L ER5,ER6
  H'001C3C  6A 2D 00 FF FD 20         MOV.B @H'FFFD20:24,R5L
  H'001C42  68 ED                     MOV.B R5L,@ER6
  H'001C44  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001C4C  0B 76                     INC.L #1,ER6
  H'001C4E  01 00 6B A6 00 FF FD 14   MOV.L ER6,@H'FFFD14:24
  H'001C56  7A 66 00 00 00 FF         AND.L #H'000000FF,ER6
  H'001C5C  46 20                     BNE H'001C7E
  H'001C5E  5E 00 04 0A               JSR @sub_00040A:24
  H'001C62  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001C6A  1B 06                     SUBS #1,ER6
  H'001C6C  5C 00 EE 68               BSR sub_000AD8:16
  H'001C70  5E 00 04 0E               JSR @sub_00040E:24
  H'001C74  FE 92                     MOV.B #H'92,R6L
  H'001C76  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001C7C  40 08                     BRA H'001C86
  H'001C7E  FE 93                     MOV.B #H'93,R6L
  H'001C80  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001C86  58 00 03 9C               BRA H'002026:16

sub_001C8A:
  H'001C8A  5E 00 04 0A               JSR @sub_00040A:24
  H'001C8E  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001C96  1B 06                     SUBS #1,ER6
  H'001C98  5C 00 EE 3C               BSR sub_000AD8:16
  H'001C9C  5E 00 04 0E               JSR @sub_00040E:24
  H'001CA0  18 EE                     SUB.B R6L,R6L
  H'001CA2  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001CA8  58 00 03 7A               BRA H'002026:16

sub_001CAC:
  H'001CAC  5C 00 E8 C6               BSR get_sci_rx_ready_bit:16
  H'001CB0  0D 66                     MOV.W R6,R6
  H'001CB2  47 2C                     BEQ H'001CE0
  H'001CB4  5C 00 E7 94               BSR read_serial_data_byte:16
  H'001CB8  79 26 00 51               CMP.W #H'0051,R6
  H'001CBC  46 1A                     BNE H'001CD8
  H'001CBE  FE 51                     MOV.B #H'51,R6L
  H'001CC0  5C 00 E7 AE               BSR send_serial_data_byte:16
  H'001CC4  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'001CCA  7D 60 70 20               BSET #2,@ER6
  H'001CCE  18 EE                     SUB.B R6L,R6L
  H'001CD0  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001CD6  40 08                     BRA H'001CE0
  H'001CD8  FE 13                     MOV.B #H'13,R6L
  H'001CDA  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001CE0  58 00 03 42               BRA H'002026:16

sub_001CE4:
  H'001CE4  5C 00 E8 8E               BSR get_sci_rx_ready_bit:16
  H'001CE8  0D 66                     MOV.W R6,R6
  H'001CEA  47 22                     BEQ H'001D0E
  H'001CEC  5C 00 E7 5C               BSR read_serial_data_byte:16
  H'001CF0  79 26 00 72               CMP.W #H'0072,R6
  H'001CF4  46 10                     BNE H'001D06
  H'001CF6  FE 72                     MOV.B #H'72,R6L
  H'001CF8  5C 00 E7 76               BSR send_serial_data_byte:16
  H'001CFC  FE BF                     MOV.B #H'BF,R6L
  H'001CFE  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001D04  40 08                     BRA H'001D0E
  H'001D06  FE 13                     MOV.B #H'13,R6L
  H'001D08  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001D0E  58 00 03 14               BRA H'002026:16

sub_001D12:
  H'001D12  5C 00 E8 60               BSR get_sci_rx_ready_bit:16
  H'001D16  0D 66                     MOV.W R6,R6
  H'001D18  47 22                     BEQ H'001D3C
  H'001D1A  5C 00 E7 2E               BSR read_serial_data_byte:16
  H'001D1E  79 26 00 4D               CMP.W #H'004D,R6
  H'001D22  46 10                     BNE H'001D34
  H'001D24  FE 4D                     MOV.B #H'4D,R6L
  H'001D26  5C 00 E7 48               BSR send_serial_data_byte:16
  H'001D2A  FE C0                     MOV.B #H'C0,R6L
  H'001D2C  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001D32  40 08                     BRA H'001D3C
  H'001D34  FE 13                     MOV.B #H'13,R6L
  H'001D36  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001D3C  58 00 02 E6               BRA H'002026:16

sub_001D40:
  H'001D40  5C 00 E8 32               BSR get_sci_rx_ready_bit:16
  H'001D44  0D 66                     MOV.W R6,R6
  H'001D46  47 34                     BEQ H'001D7C
  H'001D48  5C 00 E7 00               BSR read_serial_data_byte:16
  H'001D4C  79 26 00 45               CMP.W #H'0045,R6
  H'001D50  46 22                     BNE H'001D74
  H'001D52  FE 45                     MOV.B #H'45,R6L
  H'001D54  5C 00 E7 1A               BSR send_serial_data_byte:16
  H'001D58  79 06 00 64               MOV.W #H'0064,R6
  H'001D5C  5C 00 E6 C6               BSR tramp_vec2:16
  H'001D60  18 EE                     SUB.B R6L,R6L
  H'001D62  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001D68  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'001D6E  7D 60 72 10               BCLR #1,@ER6
  H'001D72  40 08                     BRA H'001D7C
  H'001D74  FE 13                     MOV.B #H'13,R6L
  H'001D76  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001D7C  58 00 02 A6               BRA H'002026:16

sub_001D80:
  H'001D80  5C 00 E7 F2               BSR get_sci_rx_ready_bit:16
  H'001D84  0D 66                     MOV.W R6,R6
  H'001D86  47 2A                     BEQ H'001DB2
  H'001D88  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001D90  10 36                     SHLL.L ER6
  H'001D92  10 36                     SHLL.L ER6
  H'001D94  10 36                     SHLL.L ER6
  H'001D96  10 36                     SHLL.L ER6
  H'001D98  01 00 6D F6               PUSH.L ER6
  H'001D9C  5C 00 E8 0A               BSR sub_0005AA:16
  H'001DA0  18 66                     SUB.B R6H,R6H
  H'001DA2  17 76                     EXTU.L ER6
  H'001DA4  01 00 6D 75               POP.L ER5
  H'001DA8  0A E5                     ADD.L ER6,ER5
  H'001DAA  01 00 6B A5 00 FF FD 14   MOV.L ER5,@H'FFFD14:24
  H'001DB2  58 00 02 70               BRA H'002026:16

sub_001DB6:
  H'001DB6  01 00 6B 26 00 FF FD 10   MOV.L @H'FFFD10:24,ER6
  H'001DBE  01 00 6B 25 00 FF FD 14   MOV.L @H'FFFD14:24,ER5
  H'001DC6  01 00 69 E5               MOV.L ER5,@ER6
  H'001DCA  1A E6                     SUB.L ER6,ER6
  H'001DCC  01 00 6B A6 00 FF FD 14   MOV.L ER6,@H'FFFD14:24
  H'001DD4  FE CF                     MOV.B #H'CF,R6L
  H'001DD6  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001DDC  58 00 02 46               BRA H'002026:16

sub_001DE0:
  H'001DE0  5C 00 E7 92               BSR get_sci_rx_ready_bit:16
  H'001DE4  0D 66                     MOV.W R6,R6
  H'001DE6  47 2A                     BEQ H'001E12
  H'001DE8  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001DF0  10 36                     SHLL.L ER6
  H'001DF2  10 36                     SHLL.L ER6
  H'001DF4  10 36                     SHLL.L ER6
  H'001DF6  10 36                     SHLL.L ER6
  H'001DF8  01 00 6D F6               PUSH.L ER6
  H'001DFC  5C 00 E7 AA               BSR sub_0005AA:16
  H'001E00  18 66                     SUB.B R6H,R6H
  H'001E02  17 76                     EXTU.L ER6
  H'001E04  01 00 6D 75               POP.L ER5
  H'001E08  0A E5                     ADD.L ER6,ER5
  H'001E0A  01 00 6B A5 00 FF FD 14   MOV.L ER5,@H'FFFD14:24
  H'001E12  58 00 02 10               BRA H'002026:16

sub_001E16:
  H'001E16  01 00 6B 26 00 FF FD 10   MOV.L @H'FFFD10:24,ER6
  H'001E1E  01 00 69 65               MOV.L @ER6,ER5
  H'001E22  7A 25 00 20 40 00         CMP.L #H'00204000,ER5
  H'001E28  45 18                     BCS H'001E42
  H'001E2A  7A 25 00 20 80 00         CMP.L #H'00208000,ER5
  H'001E30  44 10                     BCC H'001E42
  H'001E32  7A 06 AF AF AF AF         MOV.L #H'AFAFAFAF,ER6
  H'001E38  01 00 6B A6 00 FF FD 14   MOV.L ER6,@H'FFFD14:24
  H'001E40  40 26                     BRA H'001E68
  H'001E42  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001E4A  01 00 6D F6               PUSH.L ER6
  H'001E4E  01 00 6B 26 00 FF FD 10   MOV.L @H'FFFD10:24,ER6
  H'001E56  01 00 69 66               MOV.L @ER6,ER6
  H'001E5A  5C 00 F1 20               BSR sub_000F7E:16
  H'001E5E  0B 97                     ADDS #4,ER7
  H'001E60  01 00 6B A6 00 FF FD 14   MOV.L ER6,@H'FFFD14:24
  H'001E68  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'001E6E  0A 0E                     INC.B R6L
  H'001E70  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001E76  58 00 01 AC               BRA H'002026:16

sub_001E7A:
  H'001E7A  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001E82  FD 1C                     MOV.B #H'1C,R5L
  H'001E84  11 36                     SHLR.L ER6
  H'001E86  1A 0D                     DEC.B R5L
  H'001E88  46 FA                     BNE H'001E84
  H'001E8A  0F E5                     MOV.L ER6,ER5
  H'001E8C  5C 00 E7 B0               BSR sub_000640:16
  H'001E90  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'001E96  0A 0E                     INC.B R6L
  H'001E98  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001E9E  58 00 01 84               BRA H'002026:16

sub_001EA2:
  H'001EA2  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001EAA  FD 18                     MOV.B #H'18,R5L
  H'001EAC  11 36                     SHLR.L ER6
  H'001EAE  1A 0D                     DEC.B R5L
  H'001EB0  46 FA                     BNE H'001EAC
  H'001EB2  0F E5                     MOV.L ER6,ER5
  H'001EB4  5C 00 E7 88               BSR sub_000640:16
  H'001EB8  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'001EBE  0A 0E                     INC.B R6L
  H'001EC0  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001EC6  58 00 01 5C               BRA H'002026:16

sub_001ECA:
  H'001ECA  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001ED2  FD 14                     MOV.B #H'14,R5L
  H'001ED4  11 36                     SHLR.L ER6
  H'001ED6  1A 0D                     DEC.B R5L
  H'001ED8  46 FA                     BNE H'001ED4
  H'001EDA  0F E5                     MOV.L ER6,ER5
  H'001EDC  5C 00 E7 60               BSR sub_000640:16
  H'001EE0  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'001EE6  0A 0E                     INC.B R6L
  H'001EE8  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001EEE  58 00 01 34               BRA H'002026:16

sub_001EF2:
  H'001EF2  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001EFA  0D E6                     MOV.W E6,R6
  H'001EFC  19 EE                     SUB.W E6,E6
  H'001EFE  0F E5                     MOV.L ER6,ER5
  H'001F00  5C 00 E7 3C               BSR sub_000640:16
  H'001F04  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'001F0A  0A 0E                     INC.B R6L
  H'001F0C  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001F12  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001F1A  FD 0C                     MOV.B #H'0C,R5L
  H'001F1C  11 36                     SHLR.L ER6
  H'001F1E  1A 0D                     DEC.B R5L
  H'001F20  46 FA                     BNE H'001F1C
  H'001F22  0F E5                     MOV.L ER6,ER5
  H'001F24  5C 00 E7 18               BSR sub_000640:16
  H'001F28  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'001F2E  0A 0E                     INC.B R6L
  H'001F30  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001F36  6A 2E 00 FF FD 16         MOV.B @H'FFFD16:24,R6L
  H'001F3C  5C 00 E7 00               BSR sub_000640:16
  H'001F40  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'001F46  0A 0E                     INC.B R6L
  H'001F48  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001F4E  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001F56  11 36                     SHLR.L ER6
  H'001F58  11 36                     SHLR.L ER6
  H'001F5A  11 36                     SHLR.L ER6
  H'001F5C  11 36                     SHLR.L ER6
  H'001F5E  0F E5                     MOV.L ER6,ER5
  H'001F60  5C 00 E6 DC               BSR sub_000640:16
  H'001F64  6A 2E 00 FF FD 1E         MOV.B @H'FFFD1E:24,R6L
  H'001F6A  0A 0E                     INC.B R6L
  H'001F6C  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001F72  58 00 00 B0               BRA H'002026:16

sub_001F76:
  H'001F76  6A 2E 00 FF FD 17         MOV.B @H'FFFD17:24,R6L
  H'001F7C  5C 00 E6 C0               BSR sub_000640:16
  H'001F80  FE 12                     MOV.B #H'12,R6L
  H'001F82  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001F88  58 00 00 9A               BRA H'002026:16

sub_001F8C:
  H'001F8C  18 EE                     SUB.B R6L,R6L
  H'001F8E  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001F94  5E 00 04 1C               JSR @sub_00041C:24
  H'001F98  58 00 00 8A               BRA H'002026:16

sub_001F9C:
  H'001F9C  18 EE                     SUB.B R6L,R6L
  H'001F9E  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'001FA4  5E 00 04 0A               JSR @sub_00040A:24
  H'001FA8  5C 00 F0 E4               BSR tramp_vec1:16
  H'001FAC  40 FA                     BRA H'001FA8

sub_001FAE:
  H'001FAE  5C 00 E5 C4               BSR get_sci_rx_ready_bit:16
  H'001FB2  0D 66                     MOV.W R6,R6
  H'001FB4  47 2A                     BEQ H'001FE0
  H'001FB6  01 00 6B 26 00 FF FD 14   MOV.L @H'FFFD14:24,ER6
  H'001FBE  10 36                     SHLL.L ER6
  H'001FC0  10 36                     SHLL.L ER6
  H'001FC2  10 36                     SHLL.L ER6
  H'001FC4  10 36                     SHLL.L ER6
  H'001FC6  01 00 6D F6               PUSH.L ER6
  H'001FCA  5C 00 E5 DC               BSR sub_0005AA:16
  H'001FCE  18 66                     SUB.B R6H,R6H
  H'001FD0  17 76                     EXTU.L ER6
  H'001FD2  01 00 6D 75               POP.L ER5
  H'001FD6  0A E5                     ADD.L ER6,ER5
  H'001FD8  01 00 6B A5 00 FF FD 14   MOV.L ER5,@H'FFFD14:24
  H'001FE0  40 44                     BRA H'002026

sub_001FE2:
  H'001FE2  18 EE                     SUB.B R6L,R6L
  H'001FE4  6A AE 00 FF FD 20         MOV.B R6L,@H'FFFD20:24
  H'001FEA  1A E6                     SUB.L ER6,ER6
  H'001FEC  6A 2E 00 FF FD 20         MOV.B @H'FFFD20:24,R6L
  H'001FF2  01 00 6B 25 00 FF FD 14   MOV.L @H'FFFD14:24,ER5
  H'001FFA  0A D6                     ADD.L ER5,ER6
  H'001FFC  68 6E                     MOV.B @ER6,R6L
  H'001FFE  5C 00 E4 70               BSR send_serial_data_byte:16
  H'002002  6A 2E 00 FF FD 20         MOV.B @H'FFFD20:24,R6L
  H'002008  0A 0E                     INC.B R6L
  H'00200A  6A AE 00 FF FD 20         MOV.B R6L,@H'FFFD20:24
  H'002010  0C EE                     MOV.B R6L,R6L
  H'002012  46 D6                     BNE H'001FEA
  H'002014  FE 12                     MOV.B #H'12,R6L
  H'002016  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'00201C  40 08                     BRA H'002026
  H'00201E  FE 13                     MOV.B #H'13,R6L
  H'002020  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'002026  01 00 6D 76               POP.L ER6
  H'00202A  01 00 6D 75               POP.L ER5
  H'00202E  6D 74                     POP.W R4
  H'002030  54 70                     RTS

sub_002032:
  H'002032  6A 2E 00 20 00 00         MOV.B @H'200000:24,R6L
  H'002038  46 06                     BNE H'002040
  H'00203A  79 06 00 01               MOV.W #H'0001,R6
  H'00203E  40 02                     BRA H'002042
  H'002040  19 66                     SUB.W R6,R6
  H'002042  54 70                     RTS

sub_002044:
  H'002044  6D F6                     PUSH.W R6
  H'002046  FE FF                     MOV.B #H'FF,R6L
  H'002048  3E EC                     MOV.B R6L,@ABWCR:8
  H'00204A  18 EE                     SUB.B R6L,R6L
  H'00204C  3E F3                     MOV.B R6L,@BRCR:8
  H'00204E  FE FF                     MOV.B #H'FF,R6L
  H'002050  3E ED                     MOV.B R6L,@ASTCR:8
  H'002052  18 EE                     SUB.B R6L,R6L
  H'002054  3E EF                     MOV.B R6L,@WCER:8
  H'002056  FE F1                     MOV.B #H'F1,R6L
  H'002058  3E EE                     MOV.B R6L,@WCR:8
  H'00205A  FE 1E                     MOV.B #H'1E,R6L
  H'00205C  3E CD                     MOV.B R6L,@P8DDR:8
  H'00205E  6D 76                     POP.W R6
  H'002060  54 70                     RTS

sub_002062:
  H'002062  01 00 6D F6               PUSH.L ER6
  H'002066  7A 06 00 FF FE 10         MOV.L #H'00FFFE10,ER6
  H'00206C  01 00 6B A6 00 FF FD 10   MOV.L ER6,@H'FFFD10:24
  H'002074  01 00 6D 76               POP.L ER6
  H'002078  54 70                     RTS

sub_00207A:
  H'00207A  01 00 6D F6               PUSH.L ER6
  H'00207E  55 E2                     BSR sub_002062
  H'002080  55 C2                     BSR sub_002044
  H'002082  5C 00 E6 76               BSR sub_0006FC:16
  H'002086  5C 00 E6 8E               BSR sub_000718:16
  H'00208A  18 EE                     SUB.B R6L,R6L
  H'00208C  6A AE 00 FF FD 1E         MOV.B R6L,@H'FFFD1E:24
  H'002092  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'002098  7D 60 72 10               BCLR #1,@ER6
  H'00209C  7D 60 72 20               BCLR #2,@ER6
  H'0020A0  01 00 6D 76               POP.L ER6
  H'0020A4  54 70                     RTS

sub_0020A6:
  H'0020A6  55 D2                     BSR sub_00207A
  H'0020A8  5C 00 E5 E2               BSR sub_00068E:16
  H'0020AC  5C 00 EF 06               BSR sub_000FB6:16
  H'0020B0  79 26 00 01               CMP.W #H'0001,R6
  H'0020B4  47 08                     BEQ H'0020BE
  H'0020B6  5C 00 FF 78               BSR sub_002032:16
  H'0020BA  0D 66                     MOV.W R6,R6
  H'0020BC  46 1E                     BNE H'0020DC
  H'0020BE  FE 4D                     MOV.B #H'4D,R6L
  H'0020C0  5C 00 E3 AE               BSR send_serial_data_byte:16
  H'0020C4  79 06 00 0A               MOV.W #H'000A,R6
  H'0020C8  5C 00 E3 5A               BSR tramp_vec2:16
  H'0020CC  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'0020D2  7D 60 70 00               BSET #0,@ER6
  H'0020D6  5C 00 EF B6               BSR tramp_vec1:16
  H'0020DA  40 FA                     BRA H'0020D6
  H'0020DC  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'0020E2  7D 60 72 00               BCLR #0,@ER6
  H'0020E6  FE 4E                     MOV.B #H'4E,R6L
  H'0020E8  5C 00 E3 86               BSR send_serial_data_byte:16
  H'0020EC  79 06 00 0A               MOV.W #H'000A,R6
  H'0020F0  5C 00 E3 32               BSR tramp_vec2:16
  H'0020F4  5E 00 04 12               JSR @sub_000412:24
  H'0020F8  54 70                     RTS

sub_0020FA:
  H'0020FA  6D F6                     PUSH.W R6
  H'0020FC  18 EE                     SUB.B R6L,R6L
  H'0020FE  6A AE 00 FF FE 13         MOV.B R6L,@H'FFFE13:24
  H'002104  6A AE 00 FF FE 12         MOV.B R6L,@H'FFFE12:24
  H'00210A  6D 76                     POP.W R6
  H'00210C  54 70                     RTS

sub_00210E:
  H'00210E  6D F6                     PUSH.W R6
  H'002110  18 EE                     SUB.B R6L,R6L
  H'002112  6A AE 00 FF FE 11         MOV.B R6L,@H'FFFE11:24
  H'002118  6A AE 00 FF FE 10         MOV.B R6L,@H'FFFE10:24
  H'00211E  6D 76                     POP.W R6
  H'002120  54 70                     RTS

tramp_vec4:
  H'002122  6D F2                     PUSH.W R2
  H'002124  01 00 6D F3               PUSH.L ER3
  H'002128  6D F5                     PUSH.W R5
  H'00212A  6D FE                     PUSH.W E6
  H'00212C  1B 87                     SUBS #2,ER7
  H'00212E  55 CA                     BSR sub_0020FA
  H'002130  55 DC                     BSR sub_00210E
  H'002132  7A 06 FF FF FD 1C         MOV.L #H'FFFFFD1C,ER6
  H'002138  7D 60 72 20               BCLR #2,@ER6
  H'00213C  79 06 00 01               MOV.W #H'0001,R6
  H'002140  69 F6                     MOV.W R6,@ER7
  H'002142  19 33                     SUB.W R3,R3
  H'002144  19 BB                     SUB.W E3,E3
  H'002146  18 55                     SUB.B R5H,R5H
  H'002148  7E BC 73 70               BTST #7,@SSR1:8
  H'00214C  47 FA                     BEQ H'002148
  H'00214E  FE 4F                     MOV.B #H'4F,R6L
  H'002150  3E BB                     MOV.B R6L,@TDR1:8
  H'002152  7F BC 72 70               BCLR #7,@SSR1:8
  H'002156  79 23 00 04               CMP.W #H'0004,R3
  H'00215A  46 08                     BNE H'002164
  H'00215C  79 2B 00 04               CMP.W #H'0004,E3
  H'002160  58 70 01 88               BEQ H'0022EC:16
  H'002164  7E BC 73 70               BTST #7,@SSR1:8
  H'002168  47 48                     BEQ H'0021B2
  H'00216A  6A 2E 00 FF FE 12         MOV.B @H'FFFE12:24,R6L
  H'002170  6A 2D 00 FF FE 13         MOV.B @H'FFFE13:24,R5L
  H'002176  1C DE                     CMP.B R5L,R6L
  H'002178  47 38                     BEQ H'0021B2
  H'00217A  0C DE                     MOV.B R5L,R6L
  H'00217C  0A 0D                     INC.B R5L
  H'00217E  6A AD 00 FF FE 13         MOV.B R5L,@H'FFFE13:24
  H'002184  18 66                     SUB.B R6H,R6H
  H'002186  17 76                     EXTU.L ER6
  H'002188  78 60 6A 2D 00 FF FE 14   MOV.B @(H'FFFE14:24,ER6),R5L
  H'002190  3D BB                     MOV.B R5L,@TDR1:8
  H'002192  1C 5D                     CMP.B R5H,R5L
  H'002194  46 04                     BNE H'00219A
  H'002196  0B 53                     INC.W #1,R3
  H'002198  40 02                     BRA H'00219C
  H'00219A  19 33                     SUB.W R3,R3
  H'00219C  7F BC 72 70               BCLR #7,@SSR1:8
  H'0021A0  6A 2E 00 FF FE 13         MOV.B @H'FFFE13:24,R6L
  H'0021A6  AE 6E                     CMP.B #H'6E,R6L
  H'0021A8  43 08                     BLS H'0021B2
  H'0021AA  18 EE                     SUB.B R6L,R6L
  H'0021AC  6A AE 00 FF FE 13         MOV.B R6L,@H'FFFE13:24
  H'0021B2  7E B4 73 70               BTST #7,@SSR0:8
  H'0021B6  47 48                     BEQ H'002200
  H'0021B8  6A 2E 00 FF FE 10         MOV.B @H'FFFE10:24,R6L
  H'0021BE  6A 2D 00 FF FE 11         MOV.B @H'FFFE11:24,R5L
  H'0021C4  1C DE                     CMP.B R5L,R6L
  H'0021C6  47 38                     BEQ H'002200
  H'0021C8  0C DE                     MOV.B R5L,R6L
  H'0021CA  0A 0D                     INC.B R5L
  H'0021CC  6A AD 00 FF FE 11         MOV.B R5L,@H'FFFE11:24
  H'0021D2  18 66                     SUB.B R6H,R6H
  H'0021D4  17 76                     EXTU.L ER6
  H'0021D6  78 60 6A 2D 00 FF FE 87   MOV.B @(H'FFFE87:24,ER6),R5L
  H'0021DE  3D B3                     MOV.B R5L,@TDR0:8
  H'0021E0  1C 5D                     CMP.B R5H,R5L
  H'0021E2  46 04                     BNE H'0021E8
  H'0021E4  0B 5B                     INC.W #1,E3
  H'0021E6  40 02                     BRA H'0021EA
  H'0021E8  19 BB                     SUB.W E3,E3
  H'0021EA  7F B4 72 70               BCLR #7,@SSR0:8
  H'0021EE  6A 2E 00 FF FE 11         MOV.B @H'FFFE11:24,R6L
  H'0021F4  AE 6E                     CMP.B #H'6E,R6L
  H'0021F6  43 08                     BLS H'002200
  H'0021F8  18 EE                     SUB.B R6L,R6L
  H'0021FA  6A AE 00 FF FE 11         MOV.B R6L,@H'FFFE11:24
  H'002200  2E B4                     MOV.B @SSR0:8,R6L
  H'002202  EE 38                     AND.B #H'38,R6L
  H'002204  47 10                     BEQ H'002216
  H'002206  2A B5                     MOV.B @RDR0:8,R2L
  H'002208  2E B4                     MOV.B @SSR0:8,R6L
  H'00220A  EE C7                     AND.B #H'C7,R6L
  H'00220C  3E B4                     MOV.B R6L,@SSR0:8
  H'00220E  FE 21                     MOV.B #H'21,R6L
  H'002210  3E BB                     MOV.B R6L,@TDR1:8
  H'002212  7F BC 72 70               BCLR #7,@SSR1:8
  H'002216  7E B4 73 60               BTST #6,@SSR0:8
  H'00221A  47 58                     BEQ H'002274
  H'00221C  6A 2E 00 FF FE 12         MOV.B @H'FFFE12:24,R6L
  H'002222  0A 0E                     INC.B R6L
  H'002224  6A AE 00 FF FE 12         MOV.B R6L,@H'FFFE12:24
  H'00222A  1A 0E                     DEC.B R6L
  H'00222C  18 66                     SUB.B R6H,R6H
  H'00222E  17 76                     EXTU.L ER6
  H'002230  2D B5                     MOV.B @RDR0:8,R5L
  H'002232  78 60 6A AD 00 FF FE 14   MOV.B R5L,@(H'FFFE14:24,ER6)
  H'00223A  6A 2E 00 FF FE 12         MOV.B @H'FFFE12:24,R6L
  H'002240  AE 6E                     CMP.B #H'6E,R6L
  H'002242  43 08                     BLS H'00224C
  H'002244  18 EE                     SUB.B R6L,R6L
  H'002246  6A AE 00 FF FE 12         MOV.B R6L,@H'FFFE12:24
  H'00224C  7F B4 72 60               BCLR #6,@SSR0:8
  H'002250  1D E3                     CMP.W E6,R3
  H'002252  46 04                     BNE H'002258
  H'002254  F5 54                     MOV.B #H'54,R5H
  H'002256  40 1C                     BRA H'002274
  H'002258  79 23 00 01               CMP.W #H'0001,R3
  H'00225C  46 04                     BNE H'002262
  H'00225E  F5 72                     MOV.B #H'72,R5H
  H'002260  40 12                     BRA H'002274
  H'002262  79 23 00 02               CMP.W #H'0002,R3
  H'002266  46 04                     BNE H'00226C
  H'002268  F5 4D                     MOV.B #H'4D,R5H
  H'00226A  40 08                     BRA H'002274
  H'00226C  79 23 00 03               CMP.W #H'0003,R3
  H'002270  46 02                     BNE H'002274
  H'002272  F5 45                     MOV.B #H'45,R5H
  H'002274  2E BC                     MOV.B @SSR1:8,R6L
  H'002276  EE 38                     AND.B #H'38,R6L
  H'002278  47 10                     BEQ H'00228A
  H'00227A  2A BD                     MOV.B @RDR1:8,R2L
  H'00227C  2E BC                     MOV.B @SSR1:8,R6L
  H'00227E  EE C7                     AND.B #H'C7,R6L
  H'002280  3E BC                     MOV.B R6L,@SSR1:8
  H'002282  FE 21                     MOV.B #H'21,R6L
  H'002284  3E BB                     MOV.B R6L,@TDR1:8
  H'002286  7F BC 72 70               BCLR #7,@SSR1:8
  H'00228A  7E BC 73 60               BTST #6,@SSR1:8
  H'00228E  47 58                     BEQ H'0022E8
  H'002290  6A 2E 00 FF FE 10         MOV.B @H'FFFE10:24,R6L
  H'002296  0A 0E                     INC.B R6L
  H'002298  6A AE 00 FF FE 10         MOV.B R6L,@H'FFFE10:24
  H'00229E  1A 0E                     DEC.B R6L
  H'0022A0  18 66                     SUB.B R6H,R6H
  H'0022A2  17 76                     EXTU.L ER6
  H'0022A4  2D BD                     MOV.B @RDR1:8,R5L
  H'0022A6  78 60 6A AD 00 FF FE 87   MOV.B R5L,@(H'FFFE87:24,ER6)
  H'0022AE  6A 2E 00 FF FE 10         MOV.B @H'FFFE10:24,R6L
  H'0022B4  AE 6E                     CMP.B #H'6E,R6L
  H'0022B6  43 08                     BLS H'0022C0
  H'0022B8  18 EE                     SUB.B R6L,R6L
  H'0022BA  6A AE 00 FF FE 10         MOV.B R6L,@H'FFFE10:24
  H'0022C0  7F BC 72 60               BCLR #6,@SSR1:8
  H'0022C4  1D EB                     CMP.W E6,E3
  H'0022C6  46 04                     BNE H'0022CC
  H'0022C8  F5 54                     MOV.B #H'54,R5H
  H'0022CA  40 1C                     BRA H'0022E8
  H'0022CC  79 2B 00 01               CMP.W #H'0001,E3
  H'0022D0  46 04                     BNE H'0022D6
  H'0022D2  F5 72                     MOV.B #H'72,R5H
  H'0022D4  40 12                     BRA H'0022E8
  H'0022D6  79 2B 00 02               CMP.W #H'0002,E3
  H'0022DA  46 04                     BNE H'0022E0
  H'0022DC  F5 4D                     MOV.B #H'4D,R5H
  H'0022DE  40 08                     BRA H'0022E8
  H'0022E0  79 2B 00 03               CMP.W #H'0003,E3
  H'0022E4  46 02                     BNE H'0022E8
  H'0022E6  F5 45                     MOV.B #H'45,R5H
  H'0022E8  58 00 FE 6A               BRA H'002156:16
  H'0022EC  79 06 00 01               MOV.W #H'0001,R6
  H'0022F0  0B 87                     ADDS #2,ER7
  H'0022F2  6D 7E                     POP.W E6
  H'0022F4  6D 75                     POP.W R5
  H'0022F6  01 00 6D 73               POP.L ER3
  H'0022FA  6D 72                     POP.W R2
  H'0022FC  54 70                     RTS

tramp_vec5:
  H'0022FE  6D FE                     PUSH.W E6
  H'002300  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'002306  7D 60 70 10               BSET #1,@ER6
  H'00230A  79 06 00 01               MOV.W #H'0001,R6
  H'00230E  6D 7E                     POP.W E6
  H'002310  54 70                     RTS

tramp_vec6:
  H'002312  6D FE                     PUSH.W E6
  H'002314  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'00231A  7C 60 77 10               BLD #1,@ER6
  H'00231E  1E EE                     SUBX R6L,R6L
  H'002320  17 8E                     NEG.B R6L
  H'002322  18 66                     SUB.B R6H,R6H
  H'002324  6D 7E                     POP.W E6
  H'002326  54 70                     RTS

tramp_vec22:
  H'002328  6D FE                     PUSH.W E6
  H'00232A  7A 06 00 FF FD 1C         MOV.L #H'00FFFD1C,ER6
  H'002330  7C 60 77 20               BLD #2,@ER6
  H'002334  1E EE                     SUBX R6L,R6L
  H'002336  17 8E                     NEG.B R6L
  H'002338  18 66                     SUB.B R6H,R6H
  H'00233A  6D 7E                     POP.W E6
  H'00233C  54 70                     RTS
  H'00233E  40 FE                     BRA H'00233E
  H'002340  0C 42                     MOV.B R4H,R2H
  H'002342  4F 53                     BLE H'002397
  H'002344  00 42                     .WORD H'0042
  H'002346  45 52                     BCS H'00239A
  H'002348  4E 49                     BGT H'002393
  H'00234A  4E 41                     BGT H'00238D
  H'00234C  20 45                     MOV.B @ETCR2AL:8,R0H
  H'00234E  6C 65                     MOV.B @ER6+,R5H
  H'002350  63 74                     BTST R7H,R4H
  H'002352  72 6F                     BCLR #6,R7L
  H'002354  6E 69 63 20               MOV.B @(H'6320:16,ER6),R1L
  H'002358  41 47                     BRN H'0023A1
  H'00235A  0D 00                     MOV.W R0,R0
  H'00235C  42 69                     BHI H'0023C7
  H'00235E  6F 73 56 65               MOV.W @(H'5665:16,ER7),R3
  H'002362  72 73                     BCLR #7,R3H
  H'002364  69 6F                     MOV.W @ER6,E7
  H'002366  6E 3A 20 31               MOV.B @(H'2031:16,ER3),R2L
  H'00236A  2E 32                     MOV.B @MAR1AH:8,R6L
  H'00236C  30 0D                     MOV.B R0H,@H'FFFF0D:8
  H'00236E  00 4A                     .WORD H'004A
  H'002370  75 6C                     BXOR #6,R4L
  H'002372  79 20 39 37               CMP.W #H'3937,R0
  H'002376  0D 00                     MOV.W R0,R0
  H'002378  45 42                     BCS H'0023BC
  H'00237A  00 58                     .WORD VEC_vec22
  H'00237C  58 00 00 00               BRA H'002380:16
  H'002380  00 00                     NOP
