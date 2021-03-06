\ Ace Mines
\ by Ricardo Fernandes Lopes. (c) 2004 GNU General Public License.
\
\ The object of Ace Mines is to find all the mines possible without
\ uncovering any of them. If you find a mine, its all over! you lose.
\
\ Keys
\
\ I .. Move left
\ P .. Move right
\ A .. Move up
\ Z .. Move down
\ 
\ F .. to flag or mark a mine.
\ O .. to open or uncover the cell.
\ 
\ To Start a new game select,
\ 
\ B .. for Beginner
\ I .. for Intermediate
\ E .. for Expert
\ 
\ Move your cursor around the cells with keys I,P,A, and Z.
\ To uncover or open a cell press O, if you find a bomb its Game over!
\ 
\ If a number appears in the cell, it indicates how many mines are in
\ the eight cells that surround the numbered one.
\ 
\ To mark a cell you suspect contains a mine, press f to flag it.
\ The listing below has a number of comments in it, give you a clear
\ guide of what each AceForth word does.

: TASK ; HEX

\ Defining some ACE words:

\ Drive the ZON-X81 sound device.
CODE SND ( n1 n2 -- )            ( Write n1 to AY register n2 )
  79 C,          \ ld a,c
  D3 C, DF C,    \ out ($df),a
  E1 C,          \ pop hl
  7D C,          \ ld a,l
  D3 C, 0F C,    \ out ($0f),a
  C1 C,          \ pop bc
  NEXT           \ jp NEXT
DECIMAL

\ Turns off all sound on all channels, A,B and C
: SNDOFF  ( -- )
   255 7 SND ;

\ Simulate the ACE BEEP word
: BEEP ( c n -- )
   SWAP 0 SND
   0 1 SND
   254 7 SND
   15 8 SND
   0 DO LOOP SNDOFF ;

\ Set print position to row n1 and column n2
: AT  ( n1 n2 -- )
   SWAP 33 * + 16530 ( dfile)
   + 17339 ( cur_pos) ! ;

: RECURSE  ( -- )
   LATEST CFA , ; IMMEDIATE

( ACE Mines listing  )

CREATE TABLE 128 ALLOT  ( Graphics )
: GR 8 * TABLE + DUP 8 + SWAP DO I C! LOOP ;
HEX
00 00 00 00 00 00 00 00 00 GR
00 1C 08 08 08 08 18 00 01 GR
00 7E 40 7E 02 02 7E 00 02 GR
00 7E 02 02 3E 02 7E 00 03 GR
00 04 04 7E 44 44 40 00 04 GR
00 7E 02 02 7E 40 7E 00 05 GR
00 7E 42 7E 40 40 7E 00 06 GR
00 02 02 02 02 02 7E 00 07 GR
00 7E 42 42 7E 24 3C 00 08 GR
00 02 02 02 7E 42 7E 00 09 GR
00 3C 7E 7E 7E 3C 08 06 0A GR ( BOMB )
00 7E 5E 4E 56 7A 7E 00 0B GR ( FLAG )
00 7E 7E 7E 7E 7E 7E 00 0C GR ( TILE )
00 10 00 00 10 00 00 00 0D GR ( not used)
7E FF BD C3 FF 99 DB 7E 0E GR ( SAD )
7E C3 81 FF 99 00 99 7E 0F GR ( SMILE )
DECIMAL
: SETGR 128 0 DO TABLE I + C@ 12288 I + C! LOOP ;

\ : 2DUP OVER OVER ;  ( x1 x2 -- x1 x2 x1 x2 )
\ : 2DROP DROP DROP ; ( x1 x2 -- )

: BLIP 100 100 BEEP 50 50 BEEP ;

\ : KEY ( -- c , wait for a keypress )
\ BEGIN INKEY 0= UNTIL
\ BEGIN INKEY ?DUP UNTIL 223 AND ;

( random number generator)
0 VARIABLE RND
: RANDOMIZE 16436 @ 32767 AND RND ! ;
: RANDOM ( n1 -- n2 , generate a random number from 0 to n1-1)
 RND @ 31421 * 6927 + DUP RND !
 UM* SWAP DROP ;

: MENU ( -- n , Print menu and wait user option )
 CLS ." aceminesv"
  5  3 AT ." CHOOSE$  bEGINNER"
  7 12 AT ." iNTERMEDIATE"
  9 12 AT ." eXPERT"
 11 12 AT ." qUIT"
 19  0 AT ." 2004 (C) RICARDO FERNANDES LOPES" CR
 ." - GNU GENERAL PUBLIC LICENSE -"
 KEY BLIP ;

8 VARIABLE XMAX
8 VARIABLE YMAX
10 VARIABLE BOMBS
: LEVEL! BOMBS ! YMAX ! XMAX ! ; ( bombs height width -- )
: BEGINNER  8  8 10 ;
: INTERMED 16 16 40 ;
: EXPERT   30 16 99 ;
: SETLEVEL
 DUP [ ASCII I ] LITERAL = IF INTERMED
 ELSE DUP [ ASCII E ] LITERAL = IF EXPERT ELSE BEGINNER THEN
 THEN LEVEL! DROP ;

10 CONSTANT BOMB
11 CONSTANT FLAG
12 CONSTANT TILE

0 VARIABLE X
0 VARIABLE Y
16530 CONSTANT SCREEN
CREATE BOARD 30 16 * ALLOT
: XY@ X @ Y @ ;                 (     -- x y , get cursor coord)
: XY>BOARD XMAX @ * + BOARD + ; ( x y -- adr , convert coord to board address)
: BOARD> XY@ XY>BOARD ;         (     -- adr , get current board address)
: XY>SCREEN 1+ 33 * 1+ + SCREEN + ;   ( x y -- adr , convert coord to screen address)
: SCREEN> XY@ XY>SCREEN ;       (     -- adr , get current screen address)
: CLRBOARD ( Clear Board)
 BOARD XMAX @ YMAX @ * + BOARD DO 0 I C! LOOP ;

: XY-OK? ( x y -- x y f , check for valid coord)
 2DUP
 OVER XMAX @ < OVER YMAX @ < AND
 SWAP 0< 0= AND SWAP 0< 0= AND ;

( Toogle screen cursor inverse/normal)
: .CURSOR SCREEN> DUP C@ 128 XOR SWAP C! ;

( Move coord)
: UP    ( x y -- x y-1)   1- ; 
: DOWN  ( x y -- x y+1)   1+ ; 
: LEFT  ( x y -- x-1 y)   SWAP 1- SWAP ; 
: RIGHT ( x y -- x+1 y)   SWAP 1+ SWAP ; 

: SHOW ( x y -- c , show coord contents)
 2DUP XY>SCREEN   ROT ROT XY>BOARD C@   DUP ROT C! ;

( Tracking winning condition)
0 VARIABLE CLOSED
: CLOSEALL XMAX @ YMAX @ * BOMBS @ - CLOSED ! ; ( Initialize count of closed tiles)
: WIN? CLOSED @ 0= ; ( -- f , Check winning condition)
: .WIN ( Win/Loose icon/tune)
 0 XMAX @ 2 / AT
 WIN?
 IF   201 200 BEEP 100 250 BEEP  50 300 BEEP 63
 ELSE  50 200 BEEP 100 250 BEEP 201 300 BEEP 58
 THEN EMIT ;

: OPENALL ( Open all tiles)
 YMAX @ 0 DO
  XMAX @ 0 DO
   I J XY>BOARD C@
   I J XY>SCREEN C!
  LOOP
 LOOP ;

: OPENXY ( x y -- x y , open tile at coord)
 XY-OK?
 IF
  2DUP XY>SCREEN C@
  DUP TILE = SWAP FLAG = OR
  IF
   CLOSED @ 1- CLOSED !
   2DUP SHOW 0=
   IF
    UP    RECURSE ( OPENXY)
    RIGHT RECURSE ( OPENXY)
    DOWN  RECURSE ( OPENXY)
    DOWN  RECURSE ( OPENXY)
    LEFT  RECURSE ( OPENXY)
    LEFT  RECURSE ( OPENXY)
    UP    RECURSE ( OPENXY)
    UP    RECURSE ( OPENXY)
    RIGHT DOWN
   THEN
  THEN
 THEN ;

: OPEN ( -- f , open tile and return true if BOMB )
 BLIP XY@ OPENXY   XY>BOARD C@ BOMB = ;

: FLAGIT ( Mark/Unmark tile with a flag)
 SCREEN> C@ DUP
 TILE = IF FLAG SCREEN> C! THEN
 FLAG = IF TILE SCREEN> C! THEN ;

: INC ( x y -- x y , Increment value of cell at coord)
 XY-OK?
 IF
  2DUP XY>BOARD DUP C@ BOMB =
  IF DROP
  ELSE DUP C@ 1+ SWAP C!
  THEN
 THEN ;

: BOMB! ( x y -- , Place Bomb at coord and increment neighbor)
 2DUP XY>BOARD BOMB SWAP C!
 UP    INC
 RIGHT INC
 DOWN  INC
 DOWN  INC
 LEFT  INC
 LEFT  INC
 UP    INC
 UP    INC
 2DROP ;

: BOMB? ( x y -- x y f , check if Bomb at coord)
 2DUP XY>BOARD C@ BOMB = ;

: SEED ( place Bombs at mine field)
 CLRBOARD
 BOMBS @ 0
 DO
  46 EMIT
  BEGIN
   XMAX @ RANDOM YMAX @ RANDOM BOMB?
  WHILE
   2DROP
  REPEAT
  BOMB!
 LOOP ;

( Draw board ) 
: HBAR XMAX @ 2+ 0 DO 128 EMIT LOOP ;
: DRAW
 CLS HBAR
 YMAX @ 1+ DUP 1
 DO
  I 0 AT 128 EMIT
  XMAX @ 0
  DO
   95 ( TILE) EMIT
  LOOP
  128 EMIT
 LOOP
 0 AT HBAR
 19 0 AT
 ."    a MOVE UP      z MOVE DOWN"  CR
 ."    i MOVE LEFT    p MOVE RIGHT" CR
 ."    f PLACE FLAG   o OPEN"       CR
 ."    q QUIT" ;

: INIT   0 X ! 0 Y !  SEED CLOSEALL DRAW ;

: XY! ( x y -- )
 XY-OK? IF Y ! X ! ELSE 2DROP THEN ;

: ACTION ( c -- f , execute key command, return true if end)
 DUP [ ASCII Q ] LITERAL = DUP 0=
 IF
  OVER [ ASCII I ] LITERAL = IF XY@ LEFT  XY! THEN
  OVER [ ASCII P ] LITERAL = IF XY@ RIGHT XY! THEN
  OVER [ ASCII A ] LITERAL = IF XY@ UP    XY! THEN
  OVER [ ASCII Z ] LITERAL = IF XY@ DOWN  XY! THEN
  OVER [ ASCII F ] LITERAL = IF FLAGIT THEN
  OVER [ ASCII O ] LITERAL = IF OPEN OR WIN? OR THEN
 THEN
 SWAP DROP ;

: GAME ( n -- , Play game at given level)
 CLS ." WAIT" SETLEVEL INIT
 BEGIN .CURSOR KEY .CURSOR ACTION UNTIL
 .WIN OPENALL .CURSOR KEY DROP ;

(  Main code, run it to play ACE Mines)
: MINES
   SETGR RANDOMIZE
   BEGIN
    MENU DUP [ ASCII Q ] LITERAL = 0=
   WHILE
    GAME
   REPEAT
   DROP CLS ." BYE." ;
 