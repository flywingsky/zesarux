/*
    ZEsarUX  ZX Second-Emulator And Released for UniX
    Copyright (C) 2013 Cesar Hernandez Bano

    This file is part of ZEsarUX.

    ZEsarUX is free software: you can redistribute it and/or modify
    it under the terms of the GNU General Public License as published by
    the Free Software Foundation, either version 3 of the License, or
    (at your option) any later version.

    This program is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
    GNU General Public License for more details.

    You should have received a copy of the GNU General Public License
    along with this program.  If not, see <http://www.gnu.org/licenses/>.

*/

#include <stdio.h>
#include <stdlib.h>

#include "cpu.h"
#include "tsconf.h"
#include "mem128.h"
#include "debug.h"
#include "contend.h"
#include "menu.h"
#include "screen.h"
#include "ula.h"
#include "operaciones.h"


z80_byte tsconf_last_port_eff7;
z80_byte tsconf_last_port_dff7;
z80_byte tsconf_nvram[256];

z80_byte tsconf_af_ports[256];

//Si se usa vdac con full 15 bits, o sin vdac: en ese caso, cada componente de 5 bits son diferentes hasta valor 24, a partir de ahi son iguales a 255:
/*
0  1   2    3   4  5   6   7   8   9   10   11    12  13   14   15   16   17   18   19   20   21   22   23   24
0, 10, 21, 31, 42, 53, 63, 74, 85, 95, 106, 117, 127, 138, 149, 159, 170, 181, 191, 202, 213, 223, 234, 245, 255,

Y a partir de 24 todos son 255
*/
z80_bit tsconf_with_vdac={1};


z80_byte tsconf_fmaps[TSCONF_FMAPS_SIZE];
/*
Offset  A[11:8] RAM	  Description
#000    000x  CRAM    Color Palette RAM, 256 cells 16 bit wide, 16 bit access
#200    001x  SFILE   Sprite Descriptors, 85 cells 48 bit wide, 16 bit access
#400    0100  REGS    TS-Conf Registers, 8 bit access, adressing is the same as by #nnAF port
*/

//Retorna valor entre 0...32767, segun color de entrada entre 0..255
z80_int tsconf_return_cram_color(z80_byte color)
{
  int offset=color*2;
  z80_byte color_l=tsconf_fmaps[offset];
  z80_byte color_h=tsconf_fmaps[offset+1];

  z80_int color_retorno=(color_h<<8)|color_l;

  return color_retorno;
}


z80_byte tsconf_return_cram_palette_offset(void)
{
 return (tsconf_af_ports[7]&0xF)*16;
}

//Direcciones donde estan cada pagina de rom. 32 paginas de 16 kb
z80_byte *tsconf_rom_mem_table[32];

//Direcciones donde estan cada pagina de ram, en paginas de 16 kb
z80_byte *tsconf_ram_mem_table[256];


//Direcciones actuales mapeadas, bloques de 16 kb
z80_byte *tsconf_memory_paged[4];

//En teoria esto se activa cuando hay traps tr-dos
z80_bit tsconf_dos_signal={0};


//Almacena tamanyo pantalla y border para modo actual
//Los inicializamos con algo por si acaso
//Border indica la mitad del total, o sea, si dice 40, es margen izquierdo 40 y margen derecho 40
int tsconf_current_pixel_width=256;
int tsconf_current_pixel_height=192;
int tsconf_current_border_width=0;
int tsconf_current_border_height=0;

char *tsconf_video_modes_array[]={
  "ZX",
  "16c",
  "256c",
  "Text"
};

char *tsconf_video_sizes_array[]={
  "256x192",
  "320x200",
  "320x240",
  "360x288"
};
 
//z80_bit tsconf_fired_frame_interrupt={0};

#define TSCONF_SCANLINE_TRANSPARENT_COLOR 65535
//32768 colores maximo. Tenemos array de z80_int (16 bits). color 65535 es transparente





//360 pixeles de ancho maximo (720 en modo texto)
//Pero coordenadas sprites pueden ir de 0 a 511, teniendo en cuenta que ocupan x2 de ancho, pues son 1024
//sumamos tambien +8 porque los tiles empiezan en posicion 8 y tienen esos 8 primeros de margen por la izquierda
//Le damos mas margen por si acaso

#define TSCONF_MAX_WIDTH_LAYER (1024+8+256)

z80_int tsconf_layer_ula[TSCONF_MAX_WIDTH_LAYER];
z80_int tsconf_layer_tiles_zero[TSCONF_MAX_WIDTH_LAYER];
z80_int tsconf_layer_tiles_one[TSCONF_MAX_WIDTH_LAYER];
z80_int tsconf_layer_sprites[TSCONF_MAX_WIDTH_LAYER];

//Forzar desde menu a desactivar capas 
z80_bit tsconf_force_disable_layer_ula={0};
z80_bit tsconf_force_disable_layer_tiles_zero={0};
z80_bit tsconf_force_disable_layer_tiles_one={0};
z80_bit tsconf_force_disable_layer_sprites={0};
z80_bit tsconf_force_disable_layer_border={0};

//Prueba ny17: Todo activado: 71-73 %
//Sin ula: 66%
//Sin sprites: 70%
//Sin tiles_zero: 65%
//Sin tiles_one: 69%
//sin ninguna capa: 58%


//Si se hace render de sprites y tiles usando funcion rapida de renderizado
z80_bit tsconf_si_render_spritetile_rapido={0};

z80_byte tsconf_get_video_mode_display(void)
{
  /*
  Modos de video:
0 ZX

1 16c.
Bits are index to CRAM, where PalSel.GPAL is 4 MSBs and the pixel are 4 LSBs of the index.
Pixels are bits7..4 - left, bits3..0 - right.
Each line address is aligned to 256.
GXOffs and GYOffs add offset to X and Y start address in pixels.

2  256c.
Bits 7..0 are index to CRAM.
Each line address is aligned to 512.
GXOffs and GYOffs add offset to X and Y start address in pixels.

3 text

  */
  return (tsconf_af_ports[0]&3);
}

z80_byte tsconf_get_video_size_display(void)
{
  return ((tsconf_af_ports[0]>>6)&3);
}

//Actualiza valores de variables de tamanyo pantalla segun modo de video actual
//Modo de video ZX Spectrum solo tiene sentido con resolucion 256x192
void tsconf_set_sizes_display(void)
{
  z80_byte videosize=tsconf_get_video_size_display();

/*
  00 - 256x192: border size: 104x96
01 - 320x200: border size: 40x88
10 - 320x240: border size: 40x48
11 - 360x288: border size: 0x0
*/

  switch (videosize) {
    case 0:
      tsconf_current_pixel_width=256;
      tsconf_current_pixel_height=192;
    break;

    case 1:
      tsconf_current_pixel_width=320;
      tsconf_current_pixel_height=200;
    break;

    case 2:
      tsconf_current_pixel_width=320;
      tsconf_current_pixel_height=240;
    break;

    case 3:
      tsconf_current_pixel_width=360;
      tsconf_current_pixel_height=288;
    break;

  }

  tsconf_current_border_width=(360-tsconf_current_pixel_width)/2;
  tsconf_current_border_height=(288-tsconf_current_pixel_height)/2;


  //printf ("Current video size. Pixel size: %dX%d Border size: %dX%d\n",
  //    tsconf_current_pixel_width,tsconf_current_pixel_height,tsconf_current_border_width*2,tsconf_current_border_height*2);

}



void tsconf_splash_video_size_mode_change(void)
{
  char buffer_mensaje[64];

  sprintf (buffer_mensaje,"Setting video mode %s, size %s",
    tsconf_video_modes_array[tsconf_get_video_mode_display()],tsconf_video_sizes_array[tsconf_get_video_size_display()]);

  screen_print_splash_text(10,ESTILO_GUI_TINTA_NORMAL,ESTILO_GUI_PAPEL_NORMAL,buffer_mensaje);
}


void tsconf_set_emulator_setting_turbo(void)
{
        /*
        Register 32
        bits 1-0:
        00: 3.5 mhz
        01: 7
        10: 14
        11: reserved


                                */
        z80_byte t=tsconf_af_ports[32] & 3;
        if (t==0) cpu_turbo_speed=1;
        else if (t==1) cpu_turbo_speed=2;
        else cpu_turbo_speed=4;


        //printf ("Setting turbo: %d\n",cpu_turbo_speed);

        cpu_set_turbo_speed();
}


void tsconf_fire_dma_interrupt(void)
{
	//Depende de DI???
	if (iff1.v==0) return;

	//registro intmask 2aH bit 2
	if ((tsconf_af_ports[0x2a]&4)==0) return;				
						
						z80_byte reg_pc_h,reg_pc_l;
                                                reg_pc_h=value_16_to_8h(reg_pc);
                                                reg_pc_l=value_16_to_8l(reg_pc);

                                                poke_byte(--reg_sp,reg_pc_h);
                                                poke_byte(--reg_sp,reg_pc_l);

						reg_r++;

						


						//desactivar interrupciones al generar una
						iff1.v=0;
						

						        z80_int temp_i;
                                                        z80_byte dir_l,dir_h;
                                                        temp_i=reg_i*256+0xFB;  //interrupciones de dma el vector es XXFB
                                                         dir_l=peek_byte(temp_i++);
                                                        dir_h=peek_byte(temp_i);
                                                        reg_pc=value_8_to_16(dir_h,dir_l);
                                                        t_estados += 7;

	debug_printf (VERBOSE_DEBUG,"Calling interrupt dma handler at %04XH",reg_pc);

	//Solo sacar el handler para im2 a modo de debug
	
                                                        temp_i=reg_i*256+255;  
                                                         dir_l=peek_byte(temp_i++);
                                                        dir_h=peek_byte(temp_i);
                                                        z80_int debug_im2=value_8_to_16(dir_h,dir_l);
                                                        
	debug_printf (VERBOSE_DEBUG,"(IM2 handler is at %04XH)",debug_im2);

}

int tsconf_return_dma_address(z80_byte bajo,z80_byte medio,z80_byte alto)
{

//printf ("%d %d %d\n",bajo,medio,alto);

	int f_bajo=bajo&254; //Descartar bit bajo
	int f_medio=medio&63; //Descartar los dos bits altos
	int f_alto=alto;

	f_medio=f_medio<<8;
	f_alto=f_alto<<14;

	int final=f_bajo | f_medio | f_alto;

	//printf ("%d\n",final);

	return final;
}

int tsconf_align_address(int orig_destination,int destination,int addr_align_size)
{

			int lower;

				if (addr_align_size) { //1 alinear a 512
				//printf ("alinear a 512\n");
				lower=orig_destination&0x1FF;
				destination=destination&0xFFFE00;
				destination |=lower;
				destination +=512;
			}
			else { //0 alinear a 256
				//printf ("alinear a 256\n");
				lower=orig_destination&0xFF;
				destination=destination&0xFFFF00;
				destination |=lower;
				destination +=256;
			}

	return destination;
}

int debug_tsconf_dma_source=0;
int debug_tsconf_dma_destination=0;
int debug_tsconf_dma_burst_length=0;
int debug_tsconf_dma_burst_number=0;
z80_byte debug_tsconf_dma_s_align=0;
z80_byte debug_tsconf_dma_d_align=0;
z80_byte debug_tsconf_dma_addr_align_size=0;
z80_byte debug_tsconf_dma_ddev=0;
z80_byte debug_tsconf_dma_rw=0;


z80_bit tsconf_dma_disabled={0};

void tsconf_dma_operation(int source,int destination,int burst_length,int burst_number,z80_byte s_align,z80_byte d_align,z80_byte addr_align_size,z80_byte dma_ddev,z80_byte dma_rw)
{
	int orig_source;
	int orig_destination;


	int source_mask,destination_mask;

	z80_byte *source_pointer;
	z80_byte *destination_pointer;

	//Guardamos estos valores en variable de debug para mostrarlos en menu debug, solo a titulo informativo
	debug_tsconf_dma_source=source;
	debug_tsconf_dma_destination=destination;
	debug_tsconf_dma_burst_length=burst_length;
	debug_tsconf_dma_burst_number=burst_number;
	debug_tsconf_dma_s_align=s_align;
	debug_tsconf_dma_d_align=d_align;
	debug_tsconf_dma_addr_align_size=addr_align_size;
	debug_tsconf_dma_ddev=dma_ddev;
	debug_tsconf_dma_rw=dma_rw;

		debug_printf (VERBOSE_DEBUG,"DMA operaton type: %s",tsconf_dma_types[dma_ddev*2+dma_rw]);

		switch (dma_ddev) {

			case 1:
				if (dma_rw==0) {
					//printf ("RAM (Src) is copied to RAM (Dst)\n");

					source_pointer=tsconf_ram_mem_table[0];
					destination_pointer=tsconf_ram_mem_table[0];

					source_mask=destination_mask=0x3FFFFF; //4 mb

				}
				else {
					//printf ("Pixels from RAM (Src) are copied to RAM (Dst) if they non zero\n");

					source_pointer=tsconf_ram_mem_table[0];
					destination_pointer=tsconf_ram_mem_table[0];

					source_mask=destination_mask=0x3FFFFF; //4 mb

				}

			break;

			case 4:

				if (dma_rw==0) {
					//printf ("RAM (Dst) is filled with word from RAM (Src)\n");

					source_pointer=tsconf_ram_mem_table[0];
					destination_pointer=tsconf_ram_mem_table[0];

					source_mask=destination_mask=0x3FFFFF; //4 mb
			
				}
				else {
					//printf ("RAM (Src) is copied to CRAM (Dst)\n");

					source_pointer=tsconf_ram_mem_table[0];
					destination_pointer=tsconf_fmaps;

					source_mask=0x3FFFFF; //4 mb

					destination_mask=0x1FF; //4 mb
		
				}
			break;


			case 5:
				if (dma_rw==0) {
					debug_printf (VERBOSE_ERR,"Unemulated DMA FDD dump into RAM**");
					return;
				}

				else {
					//printf ("RAM (Src) is copied to SFILE (Dst)\n"); //Digger usa esto
					source_pointer=tsconf_ram_mem_table[0];
					destination_pointer=&tsconf_fmaps[0x200];

					source_mask=0x3FFFFF; //4 mb

					destination_mask=0x1FF; //4 mb					
				}

			break;

			default:
				debug_printf (VERBOSE_ERR,"Unemulated dma type: rw: %d ddev: %02XH",dma_rw,dma_ddev);
				return;
			break;
		}


	//Si desactivada la dma, volver
	if (tsconf_dma_disabled.v) return;

	for (;burst_number>0;burst_number--){
		int i;

	orig_source=source;
	orig_destination=destination;

		for (i=0;i<burst_length;i++) {

			destination &=destination_mask;
			source &=source_mask;

			if (dma_ddev==1 && dma_rw==1) { 

				//printf ("Pixels from RAM (Src) are copied to RAM (Dst) if they non zero\n");
				//edge_grinder usa esto

				//TODO de momento suponer 256 colores. 
				if (source_pointer[source]) destination_pointer[destination]=source_pointer[source];
			}

			else {
				destination_pointer[destination]=source_pointer[source];	
			}

			destination++;

			//Si es fill. en que se diferencia de copia normal de byte??
			if (dma_ddev==4 && dma_rw==0) {
				source++;

				destination &=destination_mask;
				source &=source_mask;

				destination_pointer[destination]=source_pointer[source];
				destination++;

				source--;

				i++; //esto es asi??? realmente copia la misma cantidad de bytes, solo que de dos en dos?
			}
			
			else {
				source++;
			}
		}

		if (d_align) {
			destination=tsconf_align_address(orig_destination,destination,addr_align_size);
		}

		if (s_align) {
			source=tsconf_align_address(orig_source,source,addr_align_size);
		}

	}
}


//Max 18
char *tsconf_dma_types[]={
//   01234567890123456789
	"(Reserved)",   //0
	"(Reserved)",
	"RAM to RAM",
	"Pixels to RAM",
	"SPI to RAM",
	"RAM to SPI	",
	"IDE to RAM",
	"RAM to IDE",
	"RAM fill from RAM",
	"RAM to CRAM",   
	"FDD dump into RAM", //10
	"RAM to SFILE",
//   01234567890123456789	
	"Pixels to RAM blit", //12
	"(Reserved)",
	"(Reserved)",
	"(Reserved)" //15
};

void tsconf_write_af_port(z80_byte puerto_h,z80_byte value)
{

  tsconf_af_ports[puerto_h]=value;

  if (puerto_h==0) {
    tsconf_splash_video_size_mode_change();


    //Cambio vconfig
    tsconf_set_sizes_display();
    modificado_border.v=1;
  }

  //temp debug vpage
  if (puerto_h==1) {
    //printf ("---VPAGE: %02XH\n",puerto_h);
  }

  if (puerto_h==7) {
    //printf ("Palette: %02XH\n",value);
  }

  //Bit 4 de 32765 es bit 0 de #21AF
  if (puerto_h==0x21) {
    puerto_32765 &=(255-16); //Reset bit 4
    //Si bit 0 de 21af, poner bit 4
    if (value&1) puerto_32765|=16;
  }

  //Port 0x7FFD is an alias of Page3, page3=tsconf_af_ports[0x13];
  if (puerto_h==0x13) {
    puerto_32765=value;
  }

  //Gestion fmaps
  /*
  Vamos a empezar por la carga de la paleta. Para ello, debe habilitar la memoria RAM paleta de mapeo,
  incluyendo el puerto FMAddr cuarto bit. En este caso, los bits 0-3 especifica la asignación de dirección
   (por ejemplo, 0 - # 0000 8-8000 # 15 - # F000, etc.). Después de encender el mapeo de las direcciones seleccionadas
   estarán disponibles para la ventana para cargar paleta. A continuación, utilizando LDIR convencional nos movemos paleta de 512 bytes.
   Después de enviar los datos, debe cerrar la ventana de asignación, dejando caer el puerto FMAddr cuarto bit. Considere este ejemplo:

   FMAddr     EQU #15AF
LOAD_PAL   LD  A,%00010000   ; Включаем маппинг по адресу #0000
           LD  BC,FMAddr
           OUT (C),A
           LD  HL,ZXPAL      ; Перебрасываем данные (32 байта)
           LD  DE,#0000
           LD  BC,#0020
           LDIR
           XOR  A            ; Отключаем маппинг
           LD  BC,FMAddr
           OUT (C),A
           RET

ZXPAL      dw  #0000,#0010,#4000,#4010,#0200,#0210,#4200,#4210
           dw  #0000,#0018,#6000,#6018,#0300,#0318,#6300,#6318
  */
  if (puerto_h==0x15) {
    //printf ("Registro fmaps 0x15 valor: %02XH\n",value);
  }

  if (puerto_h==32) {
    tsconf_set_emulator_setting_turbo();
  }

  if (puerto_h==0x27) {
	  //Dmactrl
	  //printf ("Writing DMA CTRL. value: %02XH\n",tsconf_af_ports[0x27]);
		int dmasource=tsconf_return_dma_address(tsconf_af_ports[0x1a],tsconf_af_ports[0x1b],tsconf_af_ports[0x1c]);
		int dmadest=tsconf_return_dma_address(tsconf_af_ports[0x1d],tsconf_af_ports[0x1e],tsconf_af_ports[0x1f]);
		debug_printf (VERBOSE_DEBUG,"Writing DMA DMA source: %XH dest: %XH DMALen: %02XH DMACtrl: %02XH DMANum: %02XH",
			dmasource,dmadest,tsconf_af_ports[0x26],tsconf_af_ports[0x27],tsconf_af_ports[0x28]);
		int dma_burst_length=(tsconf_af_ports[0x26]+1)*2;
		int dma_num=tsconf_af_ports[0x28]+1;
		//int dma_length=dma_burst_length*dma_num;
		//printf ("DMA length: %d x %d = %d\n",dma_burst_length,dma_num,dma_length);

		z80_byte dma_ddev=tsconf_af_ports[0x27]&7;
		z80_byte dma_rw=((tsconf_af_ports[0x27])>>7)&1;

		z80_byte dma_a_sz=((tsconf_af_ports[0x27])>>3)&1;
		z80_byte dma_d_algn=((tsconf_af_ports[0x27])>>4)&1;
		z80_byte dma_s_algn=((tsconf_af_ports[0x27])>>5)&1;

		//printf ("DMA movement type: ");

		tsconf_dma_operation(dmasource,dmadest,dma_burst_length,dma_num,dma_s_algn,dma_d_algn,dma_a_sz,dma_ddev,dma_rw);

		if (tsconf_dma_disabled.v==0) tsconf_fire_dma_interrupt();

		
  }

  //Si cambia registro #21AF (memconfig) o page0-3
  if (puerto_h==0x21 || puerto_h==0x10 || puerto_h==0x11 || puerto_h==0x12 || puerto_h==0x13 ) tsconf_set_memory_pages();
}



z80_byte tsconf_get_af_port(z80_byte index)
{

  //casos especiales
  switch(index) {
      case 0:
        //status
        //bit 6: powerup
        //bit 5: fdrver (0=regular, 1=fdd ripper)
        //bits 0-2: vdac type
        /*
        000 2-bit VDAC + PWM levels 0-24
001 3-bit VDAC
010 4-bit VDAC
011 5-bit VDAC
100-110 reserved
111 VDAC2 (FT812)
mis opciones:
"without vdac" - 0 (000)
"with vdac" - 3 (011) or 7 (111)


        */

        if (tsconf_with_vdac.v) return 7;
        else return 0;

      break;


	  //temporal hasta que no vaya la dma
	  case 0x27:
	  	return tsconf_af_ports[index] & 0x7f; //Quitar bit 7
	  break;

  }

  return tsconf_af_ports[index];
}

void tsconf_reset_cpu(void)
{
  tsconf_af_ports[0]=0;

  //Bit 4 de 32765 es bit 0 de #21AF. Por tanto poner ese bit 0 a 0
    tsconf_af_ports[0x21] &=(255-1);

    //TODO. Que otros puertos de tsconf se ponen a 0 en el reset?

  
    //De momento hacemos que cuando se haga un reset, no se vuelve a la system rom
    temp_tsconf_in_system_rom_flag=0;


    tsconf_set_memory_pages();
    tsconf_set_sizes_display();
}

void tsconf_init_memory_tables(void)
{
	debug_printf (VERBOSE_DEBUG,"Initializing TSConf memory pages");

	z80_byte *puntero;
	puntero=memoria_spectrum;

	int i;
	for (i=0;i<TSCONF_ROM_PAGES;i++) {
		tsconf_rom_mem_table[i]=puntero;
		puntero +=16384;
	}

	for (i=0;i<TSCONF_RAM_PAGES;i++) {
		tsconf_ram_mem_table[i]=puntero;
		puntero +=16384;
	}

	//Tablas contend
	contend_pages_actual[0]=0;
	contend_pages_actual[1]=contend_pages_tsconf[5];
	contend_pages_actual[2]=contend_pages_tsconf[2];
	contend_pages_actual[3]=contend_pages_tsconf[0];


}



/*

Memory paging:

af_registers[16] page at 0000 at reset: 0
af_registers[17] page at 4000 at reset: 5
af_registers[18] page at 8000 at reset: 2
af_registers[19] page at c000 at reset: 0

Segmentos 4000 y 8000 siempre determinados por esos 17 y 18


En espacio c000 interviene:
af_registers[19] (page3)
Memconfig=af_register[33]
bits 6,7=lck128
si lck128:

00 512 kb  7ffd[7:6]=page3[4:3]
01 128 kb  7ffd[7:6]=00
10 auto    !a[13] ???????
11 1024kb   #7FFD[7:6] = Page3[4:3], #7FFD[5] = Page3[5]


*/

z80_byte tsconf_get_memconfig(void)
{
  return tsconf_af_ports[0x21];
}

z80_byte tsconf_get_memconfig_lck(void)
{
  return (tsconf_get_memconfig()>>6)&3;
}



int temp_tsconf_in_system_rom_flag=1;


int tsconf_in_system_rom(void)
{
  if (temp_tsconf_in_system_rom_flag) return 1;
  return 0;
  //TODO. No entiendo bien cuando entra aqui: 00 - after reset, only in "no map" mode, System ROM, suponemos que solo al encender la maquina,
            //cosa que no es cierta
}

z80_byte tsconf_get_rom_bank(void)
{
/*
Mapeo ROM
The following ports/signals are in play:
- #7FFD bit4,
- DOS signal,
- #21AF (Memconfig),
- #10AF (Page0).

Memconfig:
- bit0 is an alias of bit4 #7FFD,
- bit2 selects mapping mode (0 - map, 1 - no map),
- bit3 selects what is in #0000..#3FFF (0 - ROM, 1 - RAM).

In "no map" the page in the win0 (#0000..#3FFF) is set in Page0 directly. 8 bits are used if RAM, 5 lower bits - 
if ROM (ROM has 32 pages i.e. 512kB).
In "map" mode Page0 selects ROM page bits 4..2 (which are set to 0 at reset) and bits 1..0 are taken from other sources, 
selecting one of four ROM pages, see table.

bit1/0
00 - after reset, only in "no map" mode, System ROM,
01 - when DOS signal active, TR-DOS,
10 - when no DOS and #7FFD.4 = 0 (or Memconfig.0 = 0), Basic 128,
11 - when no DOS and #7FFD.4 = 1 (or Memconfig.0 = 1), Basic 48.
*/



        z80_byte memconfig=tsconf_get_memconfig();
        if (memconfig & 4) {
          //Modo no map
          //bit3 selects what is in #0000..#3FFF (0 - ROM, 1 - RAM).

          z80_byte banco=tsconf_af_ports[0x10]; 
          return banco;
        }
        else {
          z80_byte banco;
          //Modo map
          z80_byte page0=tsconf_af_ports[0x10];
          page0 = page0 << 2;  //In "map" mode Page0 selects ROM page bits 4..2

          //TODO. No entiendo bien cuando entra aqui: 00 - after reset, only in "no map" mode, System ROM, suponemos que solo al encender la maquina,
          //cosa que no es cierta
          if (tsconf_in_system_rom() ) banco=0;


          else {
            if (tsconf_dos_signal.v) banco=1;
            else banco=((puerto_32765>>4)&1) | 2;
          }

          return page0 | banco;

        }




}

z80_byte tsconf_get_ram_bank_c0(void)
{
    //De momento mapear como un 128k
    z80_byte tsconf_lck=tsconf_get_memconfig_lck();

    /*
    00 512 kb  7ffd[7:6]=page3[4:3]
    01 128 kb  7ffd[7:6]=00
    10 auto    !a[13] ???????
    11 1024kb   #7FFD[7:6] = Page3[4:3], #7FFD[5] = Page3[5]


    Port 0x7FFD is an alias of Page3, thus selects page in 0xC000-0xFFFF window. Its behaviour depends on MemConfig LCK128 bits.
    00 - 512k: #7FFD[7:6] -> Page3[4:3], #7FFD[2:0] -> Page3[2:0], Page3[7:5] = 0,
    01 - 128k: #7FFD[2:0] -> Page3[2:0], Page3[7:3] = 0,
    10 - Auto: OUT (#FD) works as mode 01, OUT (C), r works as mode 00,
    11 - 1024k: #7FFD[7:6] -> Page3[4:3], #7FFD[2:0] -> Page3[2:0], #7FFD[5] -> Page3[5], Page3[7:6] = 0.


    Port 0x7FFD is an alias of Page3, page3=tsconf_af_ports[0x13];
    */

    z80_byte banco=0;

    switch (tsconf_lck) {

        case 0:
          banco=(puerto_32765&7)| ((puerto_32765>>3)&24);
        break;

        case 1:
      	 banco=puerto_32765&7;
        break;

        default:
          cpu_panic("TODO invalid value for tsconf_lck");
        break;
    }

    return banco;

}


z80_byte tsconf_get_text_font_page(void)
{
  //En teoria es la misma pagina que registro af vpage(01) xor 1 pero algo falla, nos guiamos por pagina mapeada en segmento c0
  //z80_byte pagina=tsconf_get_ram_bank_c0() ^ 1;

  z80_byte pagina=tsconf_af_ports[1] ^ 1;
  return pagina;
}

z80_byte tsconf_get_vram_page(void)
{
  return tsconf_af_ports[1];
}


void tsconf_set_memory_pages(void)
{
	z80_byte rom_page=tsconf_get_rom_bank();

  

  z80_byte ram_page_40=tsconf_af_ports[17];
  z80_byte ram_page_80=tsconf_af_ports[18];
  z80_byte ram_page_c0=tsconf_af_ports[19];


	/*
  TODO
	Port 1FFDh (read/write)
	Bit 0 If 1 maps banks 8 or 9 at 0000h (switch off rom).
	Bit 1 High bit of ROM selection and bank 8 (0) or 9 (1) if bit0 = 1.
	*/

	//memconfig
	//bit3 selects what is in #0000..#3FFF (0 - ROM, 1 - RAM).

	if (tsconf_get_memconfig()&8) {
    debug_paginas_memoria_mapeadas[0]=rom_page;
    tsconf_memory_paged[0]=tsconf_ram_mem_table[rom_page];
  }

	else {
    debug_paginas_memoria_mapeadas[0]=DEBUG_PAGINA_MAP_ES_ROM+rom_page;
    tsconf_memory_paged[0]=tsconf_rom_mem_table[rom_page];
  }



	tsconf_memory_paged[1]=tsconf_ram_mem_table[ram_page_40];
	tsconf_memory_paged[2]=tsconf_ram_mem_table[ram_page_80];
	tsconf_memory_paged[3]=tsconf_ram_mem_table[ram_page_c0];


	debug_paginas_memoria_mapeadas[1]=ram_page_40;
	debug_paginas_memoria_mapeadas[2]=ram_page_80;
	debug_paginas_memoria_mapeadas[3]=ram_page_c0;

  //printf ("32765: %02XH rom %d ram1 %d ram2 %d ram3 %d\n",puerto_32765,rom_page,ram_page_40,ram_page_80,ram_page_c0);

}


void tsconf_hard_reset(void)
{

  debug_printf(VERBOSE_DEBUG,"TSconf Hard reset cpu");
 
  reset_cpu();


	int i;
	for (i=0;i<TSCONF_FMAPS_SIZE;i++) tsconf_fmaps[i]=0;

       //Borrar toda memoria ram
        int d;
        z80_byte *puntero;
        
        for (i=0;i<TSCONF_RAM_PAGES;i++) {
                puntero=tsconf_ram_mem_table[i];
                for (d=0;d<16384;d++,puntero++) {
                        *puntero=0;
                }
        }


  //Valores por defecto
  tsconf_af_ports[0]=0;
  tsconf_af_ports[1]=5;
  tsconf_af_ports[2]=0;

  tsconf_af_ports[3] &=(255-1);
  tsconf_af_ports[4]=0;
  tsconf_af_ports[5] &=(255-1);
  tsconf_af_ports[6] &=3;
  tsconf_af_ports[7]=15;

  //Paginas RAM
  tsconf_af_ports[0x10]=0;
  tsconf_af_ports[0x11]=5;
  tsconf_af_ports[0x12]=2;
  tsconf_af_ports[0x13]=0;

  tsconf_af_ports[0x15] &=(255-16);
  tsconf_af_ports[0x20]=1;

  temp_tsconf_in_system_rom_flag=1;
  tsconf_af_ports[0x21]=4;

  tsconf_af_ports[0x22]=1;
  tsconf_af_ports[0x23]=0;
  tsconf_af_ports[0x24] &=(2+4+8);

  tsconf_af_ports[0x29] &=(255-1-2-4-8);

  tsconf_af_ports[0x2A] &=(255-1-2-4);
  tsconf_af_ports[0x2A] |=1;

  tsconf_af_ports[0x2B] &=(255-1-2-4-8);

  tsconf_set_memory_pages();
  tsconf_set_sizes_display();
  tsconf_set_emulator_setting_turbo();
}



//Funcion para convertir paleta de 5 bits de tsconf a 8 bits (rgb15 a rgb24)
//Tiene en cuenta parametro vdac de tsconf , si esta disponible o no
z80_byte tsconf_rgb_5_to_8(z80_byte color)
{

	//Con vdac
	if (tsconf_with_vdac.v) return color*8;  //5 bits: 0..31. max valor 31*8=248


	//Sin vdac
//Si se usa vdac con full 15 bits, o sin vdac: en ese caso, cada componente de 5 bits son diferentes hasta valor 24, a partir de ahi son iguales a 255:
/*
0  1   2    3   4  5   6   7   8   9   10   11    12  13   14   15   16   17   18   19   20   21   22   23   24
0, 10, 21, 31, 42, 53, 63, 74, 85, 95, 106, 117, 127, 138, 149, 159, 170, 181, 191, 202, 213, 223, 234, 245, 255,

Y a partir de 24 todos son 255
*/

	if (color>=24) return 255;


	//entre 0 y 23
				 //0  1   2    3   4  5   6   7   8   9   10   11    12  13   14   15   16   17   18   19   20   21   22   23   24
	z80_byte without_vdac[24]={0, 10, 21, 31, 42, 53, 63, 74, 85, 95, 106, 117, 127, 138, 149, 159, 170, 181, 191, 202, 213, 223, 234, 245};

	return without_vdac[color];


}


//Hace putpixel tsconf pero teniendo en cuenta desplazamiento de border, que en el caso de tsconf es variable
void scr_tsconf_putpixel_sum_border(int x,int y,unsigned color)
{
	//tsconf_current_border_width almacena el ancho de una franja, la izquierda por ejemplo
	//Dado que el border (y la zona de pixeles) son pixeles de tamanyo 2x2, multiplicar en ancho y alto

	scr_putpixel_zoom(x+tsconf_current_border_width*2,y+tsconf_current_border_height*2,color);
}


//Hace putpixel pero teniendo en cuenta tamanyo de 1x2
void scr_tsconf_putpixel_text_mode(int x,int y,unsigned color)
{
	y*=2;

	int border_x=tsconf_current_border_width*2;
	int border_y=tsconf_current_border_height*2;

	int menu_x=(x+border_x)/8;
	int menu_y=(y+border_y)/8;

	//Suponemos que y e y+1 van a estar dentro de una cuadricula igual los dos, por tanto la comprobacion siguiente solo la hacemos una vez
	if (scr_ver_si_refrescar_por_menu_activo(menu_x,menu_y)) {
		scr_tsconf_putpixel_sum_border(x,y,color);
		scr_tsconf_putpixel_sum_border(x,y+1,color);
	}
}

//Hace putpixel pero teniendo en cuenta tamanyo de 2x2
void scr_tsconf_putpixel_zx_mode(int x,int y,unsigned color)
{
	y*=2;
	x*=2;
	scr_tsconf_putpixel_sum_border(x,y,color);
	scr_tsconf_putpixel_sum_border(x,y+1,color);
	scr_tsconf_putpixel_sum_border(x+1,y,color);
	scr_tsconf_putpixel_sum_border(x+1,y+1,color);
}

void scr_tsconf_putpixel_zoom_rainbow_text_mode(unsigned color,z80_int *puntero_rainbow,int ancho_linea)
{




	//puntero_rainbow +=margeny_arr*ancho_linea;
	//puntero_rainbow +=margenx_izq;

	*puntero_rainbow=color;

	puntero_rainbow +=ancho_linea;
	*puntero_rainbow=color;

}




//Muestra un caracter en pantalla, al estilo del spectrum o zx80/81 o jupiter ace
//entrada: puntero=direccion a tabla del caracter
//ink, paper
void scr_tsconf_putsprite_comun(z80_byte *puntero,int alto,int x,int y,z80_bit inverse,z80_byte tinta,z80_byte papel,z80_int *puntero_layer)
{

        z80_int color;
        z80_byte bit;
        z80_byte line;
        z80_byte byte_leido;

		

        for (line=0;line<alto;line++,y++) {

          	byte_leido=*puntero++;
          	if (inverse.v==1) byte_leido = byte_leido ^255;

          	for (bit=0;bit<8;bit++) {
                if (byte_leido & 128 ) color=tinta;
                else color=papel;

                byte_leido=(byte_leido&127)<<1;

								
								if (puntero_layer!=NULL) {
									//Lo mete en buffer rainbow

											color=tsconf_return_cram_color(tsconf_return_cram_palette_offset()+color);

											//scr_tsconf_putpixel_zoom_rainbow_text_mode(color,puntero_rainbow,ancho_rainbow);
											*puntero_layer=color;
											puntero_layer++;

								}


								else {
									color=tsconf_return_cram_color(tsconf_return_cram_palette_offset()+color);

									//if (scr_ver_si_refrescar_por_menu_activo((x+bit)/8,y/8)) {

										scr_tsconf_putpixel_text_mode(x+bit,y,TSCONF_INDEX_FIRST_COLOR+color);
									//}
								}

           }

			//puntero_rainbow=puntero_rainbow_orig;
			//puntero_rainbow +=ancho_rainbow;
        }
}


int tsconf_get_current_visible_scanline(void)
{
	return t_scanline_draw-screen_invisible_borde_superior;;
}

int temp_conta_nogfx;

//tipo: 0 arriba, abajo. 1 izquierda derecha
void tsconf_store_scanline_border_supinf_izqder(int tipo)
{
//printf ("border\n");
//  tsconf_current_border_width=(360-tsconf_current_pixel_width)/2;
  //tsconf_current_border_height=(288-tsconf_current_pixel_height)/2;

  //Si no esta esa capa, salir
  if (tsconf_force_disable_layer_border.v) return;

  //Si borde invisible superior
  int visible_scanline=tsconf_get_current_visible_scanline();

  if (visible_scanline<0) return;

	int ancho_linea,alto;

        ancho_linea=get_total_ancho_rainbow();
        alto=get_total_alto_rainbow();

  z80_int *destino;
  destino=&rainbow_buffer[visible_scanline*2*ancho_linea]; //doble pixel en altura

	int color; //TODO. solo pillamos un color por scanline

	color=out_254 & 7;

	color=TSCONF_INDEX_FIRST_COLOR+tsconf_return_cram_color  (tsconf_return_cram_palette_offset()+color);

	//color +=240; //Colores de speccy en tsconf empiezan en 240



//      printf ("Refresco border\n");

        int x;

        //parte superior e inferior
       
	   	if (tipo==0) {
                for (x=0;x<ancho_linea;x++) {
					*destino=color;       
					destino[ancho_linea]=color; //doble pixel en altura

					destino++;
                              
                }
		}
      
	  	//laterales
		
	  	if (tipo==1) {
			  	int ancho_border=tsconf_current_border_width*2;
                for (x=0;x<ancho_border;x++) {
					
					//izq
					*destino=color;       
					destino[ancho_linea]=color; //doble pixel en altura

					//der
					destino[ancho_border+tsconf_current_pixel_width*2]=color;  
					destino[ancho_border+tsconf_current_pixel_width*2+ancho_linea]=color;    //doble pixel en altura   

					destino++;
                              
                }
		}

        //laterales
        /*for (y=0;y<tsconf_current_pixel_height;y++) {
                for (x=0;x<tsconf_current_border_width;x++) {
                        scr_tsconf_putpixel_zoom_border(x,y+tsconf_current_border_height,color);
                        scr_tsconf_putpixel_zoom_border(x+tsconf_current_border_width+tsconf_current_pixel_width,y+tsconf_current_border_height,color);
                }

        }*/

}




void tsconf_store_scanline_ula(void) 
{


	int scanline_copia=tsconf_get_current_visible_scanline();

    //linea que se debe leer
    scanline_copia -=tsconf_current_border_height;

	//zona borde superior
	if (scanline_copia<0) return;

	//Si zona border inferior
	if (scanline_copia>tsconf_current_pixel_height) return;

	//int total_ancho_rainbow=get_total_ancho_rainbow();

	int y_origen_pixeles=scanline_copia; 

	//sumar offset
	int y_offset=tsconf_af_ports[4]+256*(tsconf_af_ports[5]&1);

	
	//controlar si sale de rango
	y_origen_pixeles +=y_offset;

	y_origen_pixeles=y_origen_pixeles % 512;
      

    int x,bit;
    z80_int direccion;
	z80_int dir_atributo;
    z80_byte byte_leido;


    int color=0;
    int fila;

    z80_byte attribute,bright,flash;
	z80_int ink,paper,aux;

    z80_byte *screen=get_base_mem_pantalla();

    direccion=screen_addr_table[(y_origen_pixeles<<5)];

				
    fila=y_origen_pixeles/8;
    dir_atributo=6144+(fila*32);


  	int puntero_layer_ula=0;

	z80_byte videomode=tsconf_get_video_mode_display();


	if (videomode==3) {
		//modo texto
		int ancho_caracter=8;
		int ancho_linea=tsconf_current_pixel_width*2;

		z80_int puntero=0x0000;

		z80_byte *screen;
		screen=tsconf_ram_mem_table[tsconf_get_vram_page() ];

		//int ancho_linea_caracteres=256;
		int x=0;
		int y=scanline_copia;
		puntero=fila*256;

		z80_byte font_page=tsconf_get_text_font_page();

		z80_byte *puntero_fuente;
		puntero_fuente=tsconf_ram_mem_table[font_page];

		//z80_int puntero_orig=puntero;

		z80_byte caracter;
		//z80_byte caracter_text;

		z80_bit inverse;

 		inverse.v=0;

    	z80_int offset_caracter;

    	//z80_byte tinta,papel;
    	unsigned int tinta,papel;

    	z80_byte atributo;

    

        for (x=0;x<ancho_linea;x+=ancho_caracter) {

                caracter=screen[puntero];
                atributo=screen[puntero+128];

				

                puntero++;
               

                offset_caracter=caracter*8;

				//Sumarle scanline % 8
				offset_caracter +=(y % 8);

                //No tengo ni idea de si se leen los atributos asi, pero parece similar al real
                tinta=atributo&15;
                papel=(atributo>>4)&15;


				scr_tsconf_putsprite_comun(&puntero_fuente[offset_caracter],1,x,y,inverse,tinta,papel,&tsconf_layer_ula[puntero_layer_ula]);


				puntero_layer_ula+=ancho_caracter;


		}
	}

				//16 o 256 clores
				if (videomode==1 || videomode==2) {
					//puntero a vram
					//Indice a linea
					int offset;

					

					/*if (videomode==1) { //16 colores
						//offset=y_origen_pixeles*(tsconf_current_pixel_width/2);
						offset=y_origen_pixeles*512;
					}
					
					else {
						//offset=y_origen_pixeles*(tsconf_current_pixel_width);
						offset=y_origen_pixeles*512;
					}*/



					int x_offset=tsconf_af_ports[2]+256*(tsconf_af_ports[3]&1);
					//edge grinder usa scroll x
					
					int posicion_byte_inicial;

					//16 colores
					if (videomode==1) {
						offset=y_origen_pixeles*256;

						offset +=x_offset/2;
						posicion_byte_inicial=x_offset/2;
					}

					//256 colores
					else {
						offset=y_origen_pixeles*512;

						offset +=x_offset;
						posicion_byte_inicial=x_offset;
					}






					//Ver cuantas paginas salta esto
					int pagina_offset=offset/16384;

					//y offset final
					z80_int offset_final=offset % 16384;

					z80_byte vram_page=tsconf_get_vram_page()+pagina_offset;
					z80_byte *screen;

					screen=tsconf_ram_mem_table[vram_page]+offset_final;

					z80_byte *screen_izquierda=screen; //primera posicion con x=0


					//Sumamos scrolll x
					//16 colores
					if (videomode==1) {
						screen +=x_offset/2;
						posicion_byte_inicial=x_offset/2;
					}

					//256 colores
					else {
						screen +=x_offset;
						posicion_byte_inicial=x_offset;
					}


					z80_byte color,color_orig;
					z80_int color_final;

					


					for (x=0;x<tsconf_current_pixel_width;x++,posicion_byte_inicial++) {
						if (videomode==2) { //256 colores
							color=*screen;
						}


						if (videomode==1) { //16 colores
							color_orig=*screen;

							//pixel de la izquierda
							color=(color_orig>>4)&0xF;


							//Con paleta
							color +=tsconf_return_cram_palette_offset();

							color_final=tsconf_return_cram_color(color);
						            //doble ancho
						            tsconf_layer_ula[puntero_layer_ula++]=color_final;
						            tsconf_layer_ula[puntero_layer_ula++]=color_final;

							x++;

                                                        //pixel de la derecha
                                                        color=color_orig&0xF;

                                                        //Con paleta
                                                        color +=tsconf_return_cram_palette_offset();

						}


						color_final=tsconf_return_cram_color(color);
                                                //doble ancho
                                                tsconf_layer_ula[puntero_layer_ula++]=color_final;
                                                tsconf_layer_ula[puntero_layer_ula++]=color_final;

						screen++;


						if (posicion_byte_inicial>=512) {  //Volver a inicial
							//printf ("---LLegado scroll derecha\n");
							screen=screen_izquierda;
							posicion_byte_inicial=0;
						}


					}
				}

				if (videomode==0) {
        	for (x=0;x<32;x++) {


                        byte_leido=screen[direccion];

                        attribute=screen[dir_atributo];

                        ink=attribute &7; 
                        paper=(attribute>>3) &7; 
                        bright=(attribute)&64; 
                        flash=(attribute)&128; 
                        if (flash) { 
                          if (estado_parpadeo.v) { 
                                        aux=paper; 
                                        paper=ink; 
                                        ink=aux; 
                                } 
                        } 
            
                        if (bright) {   
                          paper+=8; 
                          ink+=8; 
                        } 
           


                        for (bit=0;bit<8;bit++) {



																color= ( byte_leido & 128 ? ink : paper ) ;

																color= tsconf_return_cram_color  (tsconf_return_cram_palette_offset()+color);

																//doble ancho
                                            //doble ancho
                                tsconf_layer_ula[puntero_layer_ula++]=color;
                                tsconf_layer_ula[puntero_layer_ula++]=color;
																




                                byte_leido=byte_leido<<1;



                        }
												direccion++;
												dir_atributo++;



        			}
					}




}


//Hace putpixel doble de ancho
//No lo uso, lo hago directamente desde las funciones que lo usan para que gaste menos cpu
/*
void tsconf_store_scanline_putsprite_putpixel(z80_int *puntero,z80_int color,z80_byte spal)
{

  //Con paleta
  z80_int color_final=tsconf_return_cram_color(color+16*spal);

	*puntero=color_final;
	puntero++;
	*puntero=color_final;

}
*/

//Comun para tiles y sprites
void tsconf_store_scanline_putsprite(int ancho, int tnum_x GCC_UNUSED, int tnum_y GCC_UNUSED,z80_byte spal,z80_byte *sprite_origen,z80_int *layer)
{

               

	  z80_int *puntero_buf_sprite;


			//puntero_buf_sprite=&tsconf_layer_sprites[ x*2 ];
      //puntero_buf_sprite=&layer[ x*2 ];
			puntero_buf_sprite=layer;

		z80_byte byte_sprite;
		z80_int color_final;

			for (;ancho;ancho-=2) { //-=2 porque son a 4bpp
				byte_sprite=*sprite_origen;
				z80_int color_izq=(byte_sprite>>4)&15;
				if (color_izq) { //0 es transparente
					//tsconf_store_scanline_putsprite_putpixel(puntero_buf_sprite,color_izq,spal);

					//Lo hacemos asi para que sea mas rapido
					color_final=tsconf_return_cram_color(color_izq+16*spal);
					*puntero_buf_sprite=color_final;
					puntero_buf_sprite++;
					*puntero_buf_sprite=color_final;
					puntero_buf_sprite++;
				}

				else {
					puntero_buf_sprite+=2;
				}



				z80_int color_der=byte_sprite&15;
				if (color_der) { //0 es transparente
					//tsconf_store_scanline_putsprite_putpixel(puntero_buf_sprite,color_der,spal);         

                                        //Lo hacemos asi para que sea mas rapido
                                        color_final=tsconf_return_cram_color(color_der+16*spal);
                                        *puntero_buf_sprite=color_final;
                                        puntero_buf_sprite++;
                                        *puntero_buf_sprite=color_final;
                                        puntero_buf_sprite++;
				}


				else {
					puntero_buf_sprite+=2;
				}


				sprite_origen++;
			}
		
		

}


void tsconf_store_scanline_sprites_putsprite(int y_offset,int ancho, int tnum_x, int tnum_y,z80_byte spal,z80_int *layer)
{

                int direccion=tsconf_af_ports[0x19]>>3;
                direccion=direccion & 31;
                direccion=direccion << 17;

                z80_byte *sprite_origen;

                sprite_origen=tsconf_ram_mem_table[0];

                sprite_origen +=direccion;

                int ancho_linea=256; //512 pixeles a 4bpp

                tnum_x *=8;
                tnum_y *=8;

                //a 4bpp
                tnum_x /=2;

                sprite_origen+=(tnum_y*ancho_linea)+tnum_x;

                //Sumar a sprite_origen el y_offset
                sprite_origen +=y_offset*(ancho_linea);
                //printf ("sprite_origen: %d\n",sprite_origen);

		            tsconf_store_scanline_putsprite(ancho, tnum_x, tnum_y,spal,sprite_origen,layer);
}


void tsconf_store_scanline_sprites(void)
{

   //linea que se debe leer
        int scanline_copia=t_scanline_draw-tsconf_current_border_height;

				//TODO: tener en cuenta zona invisible border
				if (scanline_copia<0) return;

		int i;
		int offset=0;
		int salir=0;

	z80_int *layer=tsconf_layer_sprites;

    //Los 85 sprites
		for (i=0;i<85 && !salir;i++,offset+=6) {
			if (tsconf_fmaps[0x200+offset+1]&64) {
				salir=1; //Bit Leap, ultimo sprite
				//printf ("\nUltimo sprite");
			}
			if (tsconf_fmaps[0x200+offset+1]&32) {
        int y=tsconf_fmaps[0x200+offset]+256*(tsconf_fmaps[0x200+offset+1]&1);
	      z80_byte ysize=8*(1+((tsconf_fmaps[0x200+offset+1]>>1)&7));
	               

        //Ver si esta en rango y
        if (scanline_copia>=y && scanline_copia<y+ysize) { 

		      int x=tsconf_fmaps[0x200+offset+2]+256*(tsconf_fmaps[0x200+offset+3]&1);
			    z80_byte xsize=8*(1+((tsconf_fmaps[0x200+offset+3]>>1)&7));
			    z80_int tnum=(tsconf_fmaps[0x200+offset+4])+256*(tsconf_fmaps[0x200+offset+5]&15);
			    //Tile Number for upper left corner. Bits 0-5 are X Position in Graphics Bitmap, bits 6-11 - Y Position.
			    z80_int tnum_x=tnum & 63;
    			z80_int tnum_y=(tnum>>6)&63;

		    	z80_byte spal=(tsconf_fmaps[0x200+offset+5]>>4)&15;

			    /*
			    En demo ny17, xsize=ysize=32. tnum_x va de 0,4,8, etc. Asumimos que para posicionar en el sprite adecuado,
			    es un desplazamiento de tnum_x*8. Mismo para tnum_y
			    Para pasar de cada coordenada y, hay que sumar 512 pixeles (son de 4bpp), por tanto, 256 direcciones
			    */

	        //if (x>320) printf ("\nsprite %d x: %d y: %d xs: %d ys: %d tnum_x: %d tnum_y: %d spal: %d",i,x,y,xsize,ysize,tnum_x,tnum_y,spal);
				  //temp_sprite_xy(x,y,1+8);
          int y_offset=scanline_copia-y;
          //printf ("\nscanline: %d yoff: %d sprite %d x: %d y: %d xs: %d ys: %d tnum_x: %d tnum_y: %d spal: %d",scanline_copia,y_offset,i,x,y,xsize,ysize,tnum_x,tnum_y,spal);
          //temp_sprite_xy_putsprite(x,y,xsize,ysize,tnum_x,tnum_y,spal);

			
	int final_layer_x_offset=x*2;  //*2 porque la resolucion de pixeles es de 360 maximo mientras que el scanline entero es de 720,
	//y solo se pueden usar 720 con el modo texto

          tsconf_store_scanline_sprites_putsprite(y_offset,xsize,tnum_x,tnum_y,spal,&layer[final_layer_x_offset]);
        }
				
			}
		}

		//printf ("\n");
}

//Retorna los dos bytes de definicion de un tile para una columna x dada (teniendo en cuenta que >=64, resetea a 0)
void tsconf_tile_return_column_values(z80_byte *start_line,int x,z80_byte *valor1,z80_byte *valor2)
{

	x=x&63;

	z80_byte *puntero=&start_line[x*2];
	*valor1=*puntero;
	puntero++;
	*valor2=*puntero;
}





void tsconf_store_scanline_tiles(z80_byte layer,z80_int *layer_tiles)
{

	int scanline_copia=tsconf_get_current_visible_scanline();


   //linea que se debe leer
    scanline_copia -= tsconf_current_border_height;

	//TODO: tener en cuenta zona invisible border ??
	if (scanline_copia<0) return;
  
	int direccion_graficos=tsconf_af_ports[0x17+layer]>>3;
	
	direccion_graficos=direccion_graficos & 31;
    direccion_graficos=direccion_graficos<<17;


	int direccion_tile=tsconf_af_ports[0x16];
    direccion_tile=direccion_tile<< 14;

		//printf ("direccion_tile: %06XH\n",direccion_tile);




	z80_byte *puntero_layer;
	z80_byte *puntero_graficos;

	puntero_layer=tsconf_ram_mem_table[0]+direccion_tile;
	puntero_graficos=tsconf_ram_mem_table[0]+direccion_graficos;

	

	z80_byte puntero_offset_scroll=0x40+4*layer;

	int offset_x=tsconf_af_ports[puntero_offset_scroll]+256*(tsconf_af_ports[puntero_offset_scroll+1]&1);
	int offset_y=tsconf_af_ports[puntero_offset_scroll+2]+256*(tsconf_af_ports[puntero_offset_scroll+3]&1);

  //aplicar offset_y
  //esto esta bien asi??
  scanline_copia +=offset_y;
  //if (offset_y<0) return;
  

  
  

  //http://forum.tslabs.info/viewtopic.php?f=35&t=157

	int y=scanline_copia/8;
	//Valores mayores que 64, volver al principio
	y=y&63;
	
	puntero_layer +=256*y; //Cada linea en mapa de tiles ocupa 256 bytes

	//Lo de antes es equivalente a:
	//puntero_layer +=32*(scanline_copia & 0xFFF8); //Ignorar los primeros 3 bits, es como dividir entre 8 y multiplicar de nuevo por 8

  /*
  O sea: se almacena en la forma (64 (layer0) + 64 (layer1)) x 64 tile = (128 + 128) x 64 bytes = 16kB = 1 page
  En la misma zona de memoria estan los 64 datos de layer0 (2 bytes por cada) y los 64 datos de layer1
  */
  

  //printf ("scanline: %d tile y: %d\n",scanline_copia,y);

	z80_int *layer_final=&layer_tiles[8]; //Empezamos en posicion 8 para tener 8 pixeles de margen por la izquierda
	//layer_final -=offset_x;

	puntero_layer +=128*layer;

    
	//En que scanline esta 0...7
	int desplazamiento_scanline=(scanline_copia & 7)*256;

	//Cada linea del bitmap ocupa 256 bytes

	z80_byte valor1, valor2;

	int total_tiles;
	int tile_x=offset_x/8;

	//printf ("offset x: %d\n",offset_x);

	//Luego a layer final, restarle entre 0 y 7 segun offset
	layer_final -=(offset_x%8);

	for (total_tiles=0;total_tiles<64;total_tiles++,tile_x++) {
			

				tsconf_tile_return_column_values(puntero_layer,tile_x,&valor1,&valor2);
						
				z80_int tnum=valor1+256*(valor2&15);

				z80_byte tpal=(valor2>>4)&3;

				int tnum_x=tnum&63;
				int tnum_y=(tnum>>6)&63;

			
				z80_byte *sprite_origen;
				
				sprite_origen=puntero_graficos+(tnum_y*256*8) + tnum_x*8/2;

				//printf ("desplazamiento scanline: %d\n",desplazamiento_scanline);
        		sprite_origen +=desplazamiento_scanline;


				//printf ("tile x %d sprite_origen %p\n",x,sprite_origen);

				tsconf_store_scanline_putsprite(8, 0,0,tpal,sprite_origen,layer_final);

					
				layer_final+=16;
       
	   			offset_x+=8;

			
	}

	

}

int tsconf_if_ula_enabled(void)
{
	if (tsconf_af_ports[0]&32) return 0;
	return 1;
}

int tsconf_if_sprites_enabled(void)
{
	z80_byte tsconfig=tsconf_af_ports[6];
	if (tsconfig&128) return 1;
	else return 0;
        //printf ("Sprite layers enable ");
}

int tsconf_if_tiles_zero_enabled(void)
{
	z80_byte tsconfig=tsconf_af_ports[6];
	if (tsconfig&32) return 1;
	else return 0;
}


int tsconf_if_tiles_one_enabled(void)
{
	z80_byte tsconfig=tsconf_af_ports[6];
	if (tsconfig&64) return 1;
	else return 0;
}

//Para zona de border o pantalla
void screen_store_scanline_rainbow_solo_display_tsconf(void)
{

	int linea_render_visible=tsconf_get_current_visible_scanline();

	//El dibujado de borde no es real. Tendria que usar un buffer border como en spectrum ,pero de momento ya me vale
	/*
	De momento he visto que la demo "fast" cambia los colores del border, aunque no todo el border de golpe, no a cada scanline 
	*/

	//Zona de borde superior o inferior. Dibujar directamente en buffer rainbow
	if (linea_render_visible<tsconf_current_border_height || linea_render_visible>=tsconf_current_border_height+tsconf_current_pixel_height) {
		tsconf_store_scanline_border_supinf_izqder(0);
		//No hay mas que eso en el scanline. volver
		return;
	}

	//Border laterales. Dibujar directamente en buffer rainbow
	tsconf_store_scanline_border_supinf_izqder(1);

  //Inicializamos array de capas
  int i;
  for (i=0;i<TSCONF_MAX_WIDTH_LAYER;i++) {
    tsconf_layer_ula[i]=tsconf_layer_tiles_zero[i]=tsconf_layer_tiles_one[i]=tsconf_layer_sprites[i]=TSCONF_SCANLINE_TRANSPARENT_COLOR;
  }

  //Dibujamos las capas

        //if (tsconf_af_ports[0]&32) {
                //Si no hay graficos normales, poner a negro
		
                /*int size=get_total_ancho_rainbow()*get_total_alto_rainbow();
                if (size) {
                        z80_int *p;
                        p=rainbow_buffer;
                        for (;size;size--,p++) *p=0;
                }*/

		//no meter capa de ula. demo zenloops hace esto. Ponerla en negro

		//for (i=0;i<TSCONF_MAX_WIDTH_LAYER;i++) tsconf_layer_ula[i]=0;


        //}

	//else if (tsconf_force_disable_layer_ula.v==0) tsconf_store_scanline_ula();

	if (tsconf_if_ula_enabled() && tsconf_force_disable_layer_ula.v==0) tsconf_store_scanline_ula();


//Si estamos en zona de superior o inferior, ni sprites ni tiles
int spritestiles=1;


  //Y las pasamos a buffer rainbow
        //printf ("scan line de pantalla fisica (no border): %d\n",t_scanline_draw);

        //linea que se debe leer
        int scanline_copia=linea_render_visible-tsconf_current_border_height;

				//borde superior
				if (scanline_copia<0) spritestiles=0;

				//Si zona border inferior
				if (scanline_copia>tsconf_current_pixel_height) spritestiles=0;

  if (spritestiles) {

  if (tsconf_si_render_spritetile_rapido.v==0) {
	  z80_byte tsconfig=tsconf_af_ports[6];
	  //if (tsconfig&128) {
	  if (tsconf_if_sprites_enabled() ) {
        //printf ("Sprite layers enable ");
        if (tsconf_force_disable_layer_sprites.v==0) tsconf_store_scanline_sprites();
	  }

	  //if (tsconfig&32) {
	  if (tsconf_if_tiles_zero_enabled() ) {
			//printf ("Tile layer 0 enable- ");
		  if (tsconf_force_disable_layer_tiles_zero.v==0) tsconf_store_scanline_tiles(0,tsconf_layer_tiles_zero);
		}

	  //if (tsconfig&64) {
	  if (tsconf_if_tiles_one_enabled() ) {
        	//printf ("Tile layer 1 enable- ");
	    if (tsconf_force_disable_layer_tiles_one.v==0) tsconf_store_scanline_tiles(1,tsconf_layer_tiles_one);
	  }


  }
  }




		int total_ancho_rainbow=get_total_ancho_rainbow();


        //la copiamos a buffer rainbow
        z80_int *puntero_buf_rainbow;

        int y_rainbow=scanline_copia*2;
				//printf ("store y: %d\n",y_rainbow);

        puntero_buf_rainbow=&rainbow_buffer[ y_rainbow*total_ancho_rainbow ];

				//Margenes border
				puntero_buf_rainbow +=tsconf_current_border_width*2;
				puntero_buf_rainbow +=total_ancho_rainbow*tsconf_current_border_height*2;

        int x;
  
	   
				//scanline_copia tiene coordenada scanline de dentro de zona pantalla



		z80_int *layer_one;
		z80_int *layer_two;
		z80_int *layer_three;
		z80_int *layer_four;


            	//Sprites encima de tiles y encima de ula
		layer_one=tsconf_layer_sprites;
		layer_two=&tsconf_layer_tiles_one[8];     //Empieza en offset 8. Tenemos 8 pixeles de margen a la izquierda que no se ven
		layer_three=&tsconf_layer_tiles_zero[8]; //Empieza en offset 8. Tenemos 8 pixeles de margen a la izquierda que no se ven
		layer_four=tsconf_layer_ula;


	for (x=0;x<tsconf_current_pixel_width*2;x++) {
						

            z80_int color_final;



            //Gestion de capas

        color_final=*layer_one;
		if (color_final==TSCONF_SCANLINE_TRANSPARENT_COLOR) {
			color_final=*layer_two;
			if (color_final==TSCONF_SCANLINE_TRANSPARENT_COLOR) {
				color_final=*layer_three;
				if (color_final==TSCONF_SCANLINE_TRANSPARENT_COLOR) {
					color_final=*layer_four;
					//Si transparente, color 0
	                if (color_final==TSCONF_SCANLINE_TRANSPARENT_COLOR) color_final=0;
					
				}	
			}
		}



            color_final +=TSCONF_INDEX_FIRST_COLOR;

						*puntero_buf_rainbow=color_final;
					
						//doble alto
						*(puntero_buf_rainbow+total_ancho_rainbow)=color_final;

						//Siguiente pixel
						puntero_buf_rainbow++;

		layer_one++;
		layer_two++;
		layer_three++;
		layer_four++;
	}
}

			



void screen_tsconf_refresca_text_mode(void)
{

	int ancho_caracter=8;
	int ancho_linea=tsconf_current_pixel_width*2;
	int alto_pantalla=tsconf_current_pixel_height;

	//z80_int puntero=0xc000;

	z80_int puntero=0x0000;

	z80_byte *screen;
	screen=tsconf_ram_mem_table[tsconf_get_vram_page() ];

	int ancho_linea_caracteres=256;
	int x=0;
	int y=0;

	z80_byte font_page=tsconf_get_text_font_page();

	z80_byte *puntero_fuente;
	puntero_fuente=tsconf_ram_mem_table[font_page];

	z80_int puntero_orig=puntero;

	z80_byte caracter;
	//z80_byte caracter_text;




	z80_bit inverse;

	inverse.v=0;

	z80_int offset_caracter;

	z80_byte tinta,papel;

	z80_byte atributo;

	for (;puntero<7680;) {

		caracter=screen[puntero];
		atributo=screen[puntero+128];

		//printf ("%d ",atributo);

		puntero++;


		offset_caracter=caracter*8;

		//No tengo ni idea de si se leen los atributos asi, pero parece similar al real
		tinta=atributo&15;
		papel=(atributo>>4)&15;

		scr_tsconf_putsprite_comun(&puntero_fuente[offset_caracter],8,x,y,inverse,tinta,papel,NULL);


		x+=ancho_caracter;
		if (x+ancho_caracter>ancho_linea) {
			//printf ("\n");
			x=0;
			y+=8;
			if (y+8>alto_pantalla) {
				//provocar fin
				puntero=7680;
			}
			puntero=puntero_orig+ancho_linea_caracteres; //saltar atributos
			puntero_orig=puntero;
		}
	}
}


//Putpixel de pixeles 2x2 de border de tsconf para modo no rainbow
void scr_tsconf_putpixel_zoom_border(int x,int y, unsigned int color)
{
	x*=2;
	y*=2;

	scr_putpixel_zoom(x,y,color);
	scr_putpixel_zoom(x+1,y,color);
	scr_putpixel_zoom(x,y+1,color);
	scr_putpixel_zoom(x+1,y+1,color);
}

void scr_refresca_border_tsconf_cont(void)
{
	int color;

	color=out_254 & 7;


	if (scr_refresca_sin_colores.v) color=7;

//      printf ("Refresco border\n");

        int x,y;

	//Top border cambia en spectrum y zx8081 y ace
	//int topborder=TOP_BORDER;

        //parte superior e inferior
        for (y=0;y<tsconf_current_border_height;y++) {
                for (x=0;x<TSCONF_DISPLAY_WIDTH/2;x++) {
                                scr_tsconf_putpixel_zoom_border(x,y,color);
																scr_tsconf_putpixel_zoom_border(x,y+tsconf_current_pixel_height+tsconf_current_border_height,color);
                }
        }

        //laterales
        for (y=0;y<tsconf_current_pixel_height;y++) {
                for (x=0;x<tsconf_current_border_width;x++) {
                        scr_tsconf_putpixel_zoom_border(x,y+tsconf_current_border_height,color);
                        scr_tsconf_putpixel_zoom_border(x+tsconf_current_border_width+tsconf_current_pixel_width,y+tsconf_current_border_height,color);
                }

        }

}



void screen_tsconf_refresca_border(void)
{
	if (rainbow_enabled.v==0) {
					if (border_enabled.v) {
									//ver si hay que refrescar border
									if (modificado_border.v)
									{
													scr_refresca_border_tsconf_cont();
													modificado_border.v=0;
									}

					}
	}
}

//z80_int temp_cc=0;

//Refresco pantalla sin rainbow en tsconf
void scr_tsconf_refresca_pantalla_zxmode_no_rainbow_comun(void)
{
	//printf ("refresca\n");
	int x,y,bit;
        z80_int direccion,dir_atributo;
        z80_byte byte_leido;
        int color=0;
        int fila;
        //int zx,zy;

        z80_byte attribute,ink,paper,bright,flash,aux;


       z80_byte *screen;
			 z80_byte vram_page=tsconf_get_vram_page();
			 //vram_page=temp_cc/64;
			 //temp_cc++;


			 //printf ("refresca modo 0. vram: %d\n",vram_page);
			 screen=tsconf_ram_mem_table[vram_page];
			 //temp
			 //screen=tsconf_ram_mem_table[tsconf_af_ports[1]];

        //printf ("dpy=%x ventana=%x gc=%x image=%x\n",dpy,ventana,gc,image);
	z80_byte x_hi;

        for (y=0;y<192;y++) {

                direccion=screen_addr_table[(y<<5)];


                fila=y/8;
                dir_atributo=6144+(fila*32);
                for (x=0,x_hi=0;x<32;x++,x_hi +=8) {


			//Ver en casos en que puede que haya menu activo y hay que hacer overlay
			if (1==1) {
			//if (scr_ver_si_refrescar_por_menu_activo(x,fila)) {

                	        byte_leido=screen[direccion];
	                        attribute=screen[dir_atributo];

				if (scr_refresca_sin_colores.v) attribute=56;


        	                ink=attribute &7;
                	        paper=(attribute>>3) &7;
	                        bright=(attribute) &64;
        	                flash=(attribute)&128;
                	        if (flash) {
                        	        //intercambiar si conviene
	                                if (estado_parpadeo.v) {
        	                                aux=paper;
                	                        paper=ink;
	                                        ink=aux;
        	                        }
                	        }

				if (bright) {
					ink +=8;
					paper +=8;
				}

                        	for (bit=0;bit<8;bit++) {

					color= ( byte_leido & 128 ? ink : paper );

					color=TSCONF_INDEX_FIRST_COLOR+ tsconf_return_cram_color  (tsconf_return_cram_palette_offset()+color);


					scr_tsconf_putpixel_zx_mode(x_hi+bit,y,color);

	                                byte_leido=byte_leido<<1;
        	                }
			}

			//temp
			//else {
			//	printf ("no refrescamos zona x %d fila %d\n",x,fila);
			//}


                        direccion++;
			dir_atributo++;
                }

        }

}

z80_byte temp_conta_ts=0;
z80_byte temp_conta_ts2=0;

//Refresco pantalla sin rainbow en tsconf
void scr_tsconf_refresca_pantalla_16c_256c_no_rainbow(int modo)
{

	int x,y;


        z80_byte color;





       z80_int puntero=0;

			 z80_byte vrampage;
			 vrampage=tsconf_get_vram_page();


			 //vrampage=temp_conta_ts;

//printf ("refresca 16c/256c. vram page: %d modo: %d pagina forzada: %d\n",tsconf_af_ports[1],modo,vrampage);

			 //temp_conta_ts2++;
			 //if ((temp_conta_ts2 % 4)==0) temp_conta_ts++;





			 z80_byte *screen=tsconf_ram_mem_table[vrampage];


        for (y=0;y<tsconf_current_pixel_height;y++) {
                //direccion=16384 | devuelve_direccion_pantalla(0,y);

								z80_int puntero_orig=puntero;

                for (x=0;x<tsconf_current_pixel_width;) {


			//Ver en casos en que puede que haya menu activo y hay que hacer overlay
									if (1==1) {
			//if (scr_ver_si_refrescar_por_menu_activo(x,fila)) {

										if (modo==1) { //16c
                	    color=screen[puntero++];
											//printf ("color: %d\n",color);
	        						scr_tsconf_putpixel_zx_mode(x++,y,TSCONF_INDEX_FIRST_COLOR+ tsconf_return_cram_color (tsconf_return_cram_palette_offset()+( (color>>4)&0xF) ) );
											scr_tsconf_putpixel_zx_mode(x++,y,TSCONF_INDEX_FIRST_COLOR+ tsconf_return_cram_color  (tsconf_return_cram_palette_offset()+ (color&0xF) ) );
										}

										if (modo==2) { //256c
											color=screen[puntero++];
											scr_tsconf_putpixel_zx_mode(x++,y,TSCONF_INDEX_FIRST_COLOR+tsconf_return_cram_color(color) );
										}


        	         }
							}
								//Siguiente linea
								if (modo==1) puntero=puntero_orig+256;
								else puntero=puntero_orig+512;

								if (puntero>=16384) {
									puntero=0;
									vrampage++;
									screen=tsconf_ram_mem_table[vrampage];

								}




        }

}



void scr_tsconf_refresca_pantalla_zxmode_no_rainbow(void)
{

	if (border_enabled.v) {
		//ver si hay que refrescar border
		if (modificado_border.v) {
			//int color;
			//color=out_254 & 7;

			//screen_prism_refresca_no_rainbow_border(color);
			scr_refresca_border_tsconf_cont();

			modificado_border.v=0;
		}

	}

	scr_tsconf_refresca_pantalla_zxmode_no_rainbow_comun();

}


void screen_tsconf_refresca_rainbow(void) {

	int ancho,alto;

        ancho=get_total_ancho_rainbow();
        alto=get_total_alto_rainbow();

		//printf ("ancho: %d alto: %d\n",ancho,alto);

        int x,y,bit;

        //margenes de zona interior de pantalla. Para overlay menu
        /*int margenx_izq=screen_total_borde_izquierdo*border_enabled.v;
        int margenx_der=screen_total_borde_izquierdo*border_enabled.v+512;
        int margeny_arr=screen_borde_superior*border_enabled.v;
        int margeny_aba=screen_borde_superior*border_enabled.v+384;*/

				//en tsconf menu no aparece con margen de border. Sale tal cual desde 0,0

        //para overlay menu tambien
        //int fila;
        //int columna;

        z80_int color_pixel;
        z80_int *puntero;

        puntero=rainbow_buffer;
        int dibujar;

	int menu_x,menu_y;

        for (y=0;y<alto;y++) {
                for (x=0;x<ancho;x+=8) {
                        dibujar=1;

                        //Ver si esa zona esta ocupada por texto de menu u overlay

                        //if (y>=margeny_arr && y<margeny_aba && x>=margenx_izq && x<margenx_der) {
			if (y<192 && x<256) {


                                //normalmente a 48
                                //int screen_total_borde_izquierdo;

				dibujar=0;
				//menu_x=(x-margenx_izq)/8;
				//menu_y=(y-margeny_arr)/8;
				menu_x=x/8;
				menu_y=y/8;

				if (scr_ver_si_refrescar_por_menu_activo(menu_x,menu_y)) dibujar=1;

                        }

												//temp
												//dibujar=1;

                        if (dibujar==1) {

                                        for (bit=0;bit<8;bit++) {


                                                //printf ("prism refresca x: %d y: %d\n",x,y);

                                                color_pixel=*puntero++;

                                                scr_putpixel_zoom_rainbow(x+bit,y,color_pixel);
                                        }
                        }
                        else puntero+=8;

                }
        }


}


void screen_tsconf_refresca_pantalla(void)
{

	
	//Como spectrum clasico

	//modo clasico. sin rainbow
	if (rainbow_enabled.v==0) {
			z80_byte modo_video=tsconf_get_video_mode_display();


			//printf ("modo video: %d\n",modo_video );
					if (modo_video==0) scr_tsconf_refresca_pantalla_zxmode_no_rainbow();
					if (modo_video==1)scr_tsconf_refresca_pantalla_16c_256c_no_rainbow(1);
					if (modo_video==2)scr_tsconf_refresca_pantalla_16c_256c_no_rainbow(2);
					if (modo_video==3) {
						screen_tsconf_refresca_border();
						screen_tsconf_refresca_text_mode();
					}

	}

	else {
	//modo rainbow - real video
				if (tsconf_si_render_spritetile_rapido.v) tsconf_fast_tilesprite_render();

				screen_tsconf_refresca_rainbow();
	}

}
