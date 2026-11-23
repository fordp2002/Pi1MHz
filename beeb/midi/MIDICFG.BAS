   10 REM ---------------------------------------------------------------
   20 REM  MIDICFG - set up and test the Pi1MHz MIDI port (or any 6850)
   30 REM  Machine generated: Claude Opus 5.5 (claude-opus-5-5)
   40 REM  The port is switched on in /Pi1MHz/Pi1MHz.cfg (MIDI_addr=...),
   50 REM  see docs/user/midi.md.  Uses OSBYTE 146/147 for every access,
   60 REM  so it works from a 6502 second processor too.
   70 REM ---------------------------------------------------------------
   80 B%=&08 : CR%=&16 : CH%=1 : F%=TRUE
   90 ON ERROR IF ERR=17 THEN PRINT'"Escape" : PROCwait : GOTO 110 ELSE REPORT : PRINT " at line ";ERL : END
  100 MODE 7
  110 CLS
  120 PRINT CHR$141;CHR$131;"Pi1MHz MIDI port"
  130 PRINT CHR$141;CHR$131;"Pi1MHz MIDI port"
  140 PRINT
  150 PRINT "Port:    &FC";FNh(B%);"  (status &FC";FNh(B%);", data &FC";FNh(B%+1);")"
  160 PRINT "Control: &";FNh(CR%);"  (divide by ";FNdiv(CR%);", 8N1)"
  170 PRINT "Channel: ";CH%
  180 PRINT
  190 PRINT CHR$130;"D";CHR$135;"Detect the 6850"
  200 PRINT CHR$130;"N";CHR$135;"Play a scale (MIDI OUT)"
  210 PRINT CHR$130;"M";CHR$135;"Monitor MIDI IN"
  220 PRINT CHR$130;"L";CHR$135;"Loopback test (OUT linked to IN)"
  230 PRINT CHR$130;"P";CHR$135;"Panic: all notes off"
  240 PRINT CHR$130;"A";CHR$135;"Change port address"
  250 PRINT CHR$130;"V";CHR$135;"Change divide ratio (16/64)"
  260 PRINT CHR$130;"H";CHR$135;"Change MIDI channel"
  270 PRINT CHR$130;"C";CHR$135;"Show the Pi1MHz.cfg lines"
  280 PRINT CHR$130;"Q";CHR$135;"Quit"
  290 PRINT
  300 PRINT "Choice? ";
  310 K$=GET$ : IF K$>="a" AND K$<="z" THEN K$=CHR$(ASC(K$)-32)
  320 PRINT K$ : PRINT
  330 IF K$="D" THEN PROCdetect
  340 IF K$="N" THEN PROCscale
  350 IF K$="M" THEN PROCmonitor
  360 IF K$="L" THEN PROCloop
  370 IF K$="P" THEN PROCpanic : PRINT "Sent."
  380 IF K$="A" THEN PROCaddress
  390 IF K$="V" THEN PROCdivide
  400 IF K$="H" THEN INPUT "Channel (1-16) ";C% : IF C%>=1 AND C%<=16 THEN CH%=C%
  410 IF K$="C" THEN PROCconfig
  420 IF K$="Q" THEN END
  430 IF INSTR("DNMLPC",K$) THEN PROCwait
  440 GOTO 110
  450 :
  460 REM ===== port access: OSBYTE 146 reads FRED, 147 writes it ======
  470 DEF FNr(R%) : A%=146 : X%=B%+R% : =(USR&FFF4 AND &FF0000) DIV &10000
  480 DEF PROCw(R%,V%) : A%=147 : X%=B%+R% : Y%=V% : CALL &FFF4 : ENDPROC
  490 :
  500 REM Master reset, then the chosen control byte (no interrupts)
  510 DEF PROCinit : PROCw(0,3) : PROCw(0,CR%) : ENDPROC
  520 :
  530 REM One byte out, waiting at most half a second for TDRE
  540 DEF PROCtx(V%)
  550 LOCAL T%,S%
  560 T%=TIME
  570 REPEAT S%=FNr(0) : UNTIL (S% AND 2) OR TIME-T%>50
  580 IF (S% AND 2)=0 THEN PRINT "TDRE never came: is the port on?" : PROCwait : GOTO 110
  590 PROCw(1,V%)
  600 ENDPROC
  610 :
  620 DEF PROCdetect
  630 LOCAL S0%,S1%
  640 PROCw(0,3) : S0%=FNr(0)
  650 PROCw(0,CR%) : S1%=FNr(0)
  660 PRINT "Status in reset:  &";FNh(S0%)
  670 PRINT "Status after it:  &";FNh(S1%)
  680 IF (S0% AND 3)=0 AND (S1% AND &0E)=2 THEN PRINT'CHR$130;"A 6850 answers at &FC";FNh(B%) : ENDPROC
  690 PRINT'CHR$129;"No 6850 answers at &FC";FNh(B%)
  700 PRINT'"Check MIDI_addr in /Pi1MHz/Pi1MHz.cfg"
  710 PRINT "and the MIDI row of the Pi's /status page."
  720 ENDPROC
  730 :
  740 DEF PROCscale
  750 LOCAL I%,N%
  760 PROCinit
  770 PRINT "C major on channel ";CH%;"..."
  780 RESTORE 850
  790 FOR I%=1 TO 8
  800   READ N%
  810   PROCtx(&8F+CH%) : PROCtx(N%) : PROCtx(100)
  820   T%=TIME : REPEAT UNTIL TIME-T%>25
  830   PROCtx(&7F+CH%) : PROCtx(N%) : PROCtx(0)
  840 NEXT
  850 DATA 60,62,64,65,67,69,71,72
  860 ENDPROC
  870 :
  880 DEF PROCmonitor
  890 LOCAL S%,D%,O%
  900 PROCinit
  910 PRINT "MIDI IN in hex; ESCAPE to stop."
  920 IF F% THEN PRINT "(clock F8 and active sensing FE hidden)"
  930 PRINT
  940 REPEAT
  950   S%=FNr(0)
  960   IF S% AND &20 THEN O%=O%+1 : PRINT CHR$129;"[lost]";CHR$135;
  970   IF S% AND 1 THEN PROCshow(FNr(1))
  980 UNTIL FALSE
  990 :
  992 REM A status byte starts a new line; F8 and FE hidden if F%
  994 DEF PROCshow(D%)
  996 IF F% AND (D%=&F8 OR D%=&FE) THEN ENDPROC
  997 IF D%>=&80 THEN PRINT
  998 PRINT FNh(D%);" ";
  999 ENDPROC
 1000 DEF PROCloop
 1010 LOCAL I%,E%,L%,S%,D%,T%,T0%
 1020 PROCinit
 1030 PRINT "Sending 256 bytes, each read back..."
 1040 T0%=TIME
 1050 FOR I%=0 TO 255
 1060   PROCtx(I%)
 1070   T%=TIME
 1080   REPEAT S%=FNr(0) : UNTIL (S% AND 1) OR TIME-T%>20
 1090   IF (S% AND 1)=0 THEN L%=L%+1 ELSE D%=FNr(1) : IF D%<>I% THEN E%=E%+1
 1100 NEXT
 1110 PRINT "Missing: ";L%;"  wrong: ";E%;"  in ";(TIME-T0%)/100;" s"
 1120 IF L%=256 THEN PRINT "Nothing came back: is OUT linked to IN?"
 1130 IF L%+E%=0 THEN PRINT CHR$130;"Loopback OK"
 1140 ENDPROC
 1150 :
 1160 DEF PROCpanic
 1170 LOCAL C%
 1180 PROCinit
 1190 FOR C%=0 TO 15
 1200   PROCtx(&B0+C%) : PROCtx(&40) : PROCtx(0)
 1210   PROCtx(&78) : PROCtx(0) : PROCtx(&7B) : PROCtx(0)
 1220 NEXT
 1230 ENDPROC
 1240 :
 1250 DEF PROCaddress
 1260 LOCAL A$,A%
 1270 PRINT "08 = &FC08, the Music 2000's first port"
 1280 PRINT "00 = &FC00 (move Rambyte first)"
 1290 INPUT "Address &FC";A$
 1300 A%=EVAL("&"+A$)
 1310 IF A%<0 OR A%>&FE THEN PRINT "Must be 00 to FE" : PROCwait : ENDPROC
 1320 B%=A%
 1330 ENDPROC
 1340 :
 1350 DEF PROCconfig
 1360 PRINT "In /Pi1MHz/Pi1MHz.cfg on the SD card:"
 1370 PRINT CHR$134;"MIDI_addr=0x";FNh(B%)
 1380 PRINT CHR$134;"midi_tx_depth=64"
 1390 PRINT CHR$134;"midi_break_panic=1"
 1400 IF B%<=3 THEN PRINT CHR$134;"Rambyte_addr=0x04"'"(or the MIDI port will not start)"
 1410 PRINT'"In cmdline.txt (one line, space between):"
 1420 PRINT CHR$134;"uart_midi=1"
 1430 PRINT'"Then power-cycle the Pi."
 1440 ENDPROC
 1450 :
 1460 DEF FNh(V%)=RIGHT$("0"+STR$~V%,2)
 1470 DEF FNdiv(C%) : IF (C% AND 3)=1 THEN =16 ELSE IF (C% AND 3)=2 THEN =64 ELSE =1
 1475 DEF PROCdivide : IF (CR% AND 3)=1 THEN CR%=(CR% AND &FC) OR 2 ELSE CR%=(CR% AND &FC) OR 1
 1476 ENDPROC
 1480 DEF PROCwait : PRINT'"Press a key"; : K$=GET$ : ENDPROC
