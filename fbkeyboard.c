/*
fbkeyboard.c : framebuffer softkeyboard for touchscreen devices

Copyright 2017 Julian Winkler <julia.winkler1@web.de>

    This program is free software: you can redistribute it and/or modify
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

#include <stdlib.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <dirent.h>
#include <linux/fb.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <ft2build.h>
#include FT_FREETYPE_H

char *font = "/usr/share/fonts/truetype/dejavu/DejaVuSans.ttf";
char *device = NULL;
char *layout[] = {"qwertyuiopasdfghjklzxcvbnm/.",
                  "QWERTYUIOPASDFGHJKLZXCVBNM?>",
                  "1234567890-={};\'\\,./      /.",
                  "!@#$%^&*()_+[]:\"|<>?      ?>"};
int layoutuse=0;
__u16 keys[3][29] = {{KEY_Q, KEY_W, KEY_E, KEY_R, KEY_T, KEY_Y, KEY_U, KEY_I, KEY_O, KEY_P, KEY_A, KEY_S, KEY_D, KEY_F, KEY_G, KEY_H, KEY_J, KEY_K, KEY_L, KEY_Z, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_SLASH, KEY_DOT},
                     {KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_MINUS, KEY_EQUAL, KEY_LEFTBRACE, KEY_RIGHTBRACE, KEY_SEMICOLON, KEY_APOSTROPHE, KEY_BACKSLASH, KEY_COMMA, KEY_DOT, KEY_SLASH, KEY_X, KEY_C, KEY_V, KEY_B, KEY_N, KEY_M, KEY_SLASH, KEY_DOT},
                     {KEY_LEFTSHIFT, KEY_LEFTCTRL, KEY_ESC, KEY_TAB, KEY_UP, KEY_DOWN, KEY_LEFT, KEY_RIGHT, KEY_SPACE, KEY_ENTER, KEY_BACKSPACE}};

struct fb_var_screeninfo vinfo;
struct fb_fix_screeninfo finfo;
char *buf;
int hight;
int fduinput;
struct input_event ie;
FT_Face face;

void fill_rect(int x, int y, int xend, int yend, int color) {
  int i,j;
  int32_t* line;
  for (i=y; i<yend; i++) {
    line = (int32_t *) (buf + finfo.line_length*(i));
    for (j=x; j<xend; j++) {
      *(line + j) = color;
    }
  }
}

void draw_char(int x, int y, char c) {
  int i, j;
  FT_Load_Char(face, c, FT_LOAD_RENDER);
  y += hight*3/4-face->glyph->bitmap_top;
  for (i=0; i<face->glyph->bitmap.rows; i++)
    for (j=0; j<face->glyph->bitmap.width; j++) {
      *(buf + finfo.line_length*(i+y) + (j+x)*4) = *(face->glyph->bitmap.buffer + face->glyph->bitmap.pitch*i + j);
      *(buf + finfo.line_length*(i+y) + (j+x)*4+1) = *(face->glyph->bitmap.buffer + face->glyph->bitmap.pitch*i + j);
      *(buf + finfo.line_length*(i+y) + (j+x)*4+2) = *(face->glyph->bitmap.buffer + face->glyph->bitmap.pitch*i + j);
    }
}

void draw_text(int x, int y, char *text) {
  while (*text) {
    draw_char(x, y, *text);
    text++;
    x += face->glyph->advance.x >> 6;
  }
}

void send_key(__u16 code) {
  ie.type = EV_KEY;
  ie.code = code;
  ie.value = 1;
  if(write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
    fprintf(stderr, "error: sending uinput event\n");
  ie.value = 0;
  if(write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
    fprintf(stderr, "error: sending uinput event\n");
  ie.code = KEY_LEFTCTRL;
  if(write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
    fprintf(stderr, "error: sending uinput event\n");
  ie.type = EV_SYN;
  ie.code = SYN_REPORT;
  if(write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
    fprintf(stderr, "error: sending uinput event\n");
}

int main(int argc, char *argv[]) {
  int fbfd, fdinput;
  struct input_absinfo abs_x, abs_y;
  FT_Library library;
  int absolute_x, absolute_y, touchdown, pressed=-1, released, key;

  char c;
  while((c = getopt(argc, argv, "d:f:h")) != -1) {
    switch (c) {
      case 'd':
        device = optarg;
        break;
      case 'f':
        font = optarg;
        break;
      case 'h':
        printf("usage: %s [options]\npossible options are:\n -h: print this help\n -d: set path to inputdevice\n -f: set path to font\n", argv[0]);
        exit(0);
        break;
      case '?':
        fprintf(stderr, "unrecognized option -%c\n", optopt);
        break;
    }
  }

  fbfd = open("/dev/fb0", O_RDWR);
  if (fbfd == -1) {
    perror("error: opening framebuffer device /dev/fb0");
    exit(-1);
  }
  if (ioctl(fbfd, FBIOGET_FSCREENINFO, &finfo) == -1) {
    perror("error: reading fixed framebuffer information");
    exit(-1);
  }
  if (ioctl(fbfd, FBIOGET_VSCREENINFO, &vinfo) == -1) {
    perror("error: reading variable framebuffer information");
    exit(-1);
  }
  hight = (vinfo.yres/3+vinfo.xres/2)/10;//hight of one row

  if (FT_Init_FreeType(&library)) {
    perror("error: freetype initialization");
    exit(-1);
  }
  if (FT_New_Face(library, font, 0, &face)) {
    perror("unable to load font file");
    exit(-1);
  }
  if (FT_Set_Pixel_Sizes(face, hight*3/4, hight*3/4)) {
    perror("FT_Set_Pixel_Sizes failed");
    exit(-1);
  }

  if(device) {
    if ((fdinput = open(device, O_RDONLY)) == -1) {
      perror("failed to open input device node");
      exit(-1);
    }
  }
  else {
    DIR *inputdevs = opendir("/dev/input");
    struct dirent *dptr;
    fdinput = -1;
    while(dptr = readdir(inputdevs)) {
      if((fdinput=openat(dirfd(inputdevs), dptr->d_name, O_RDONLY)) != -1 && ioctl(fdinput, EVIOCGBIT(0, sizeof(key)), &key) != -1 && key>>EV_ABS & 1)
        break;
      if(fdinput != -1){
        close(fdinput);
        fdinput = -1;
      }
    }
    if(fdinput == -1) {
      fprintf(stderr, "no absolute axes device found in /dev/input\n");
      exit(-1);
    }
  }
  if(ioctl (fdinput, EVIOCGABS (ABS_MT_POSITION_X), &abs_x) == -1)
    perror("error: getting touchscreen size");
  if(ioctl (fdinput, EVIOCGABS (ABS_MT_POSITION_Y), &abs_y) == -1)
    perror("error: getting touchscreen size");

  fduinput = open("/dev/uinput", O_WRONLY);
  if(fduinput == -1)
    perror("error: cannot open uinput device /dev/uinput");
  if(ioctl(fduinput, UI_SET_EVBIT, EV_KEY) == -1)
    perror("error: SET_EVBIT EV_KEY");
  if(ioctl(fduinput, UI_SET_EVBIT, EV_SYN) == -1)
    perror("error: SET_EVBIT EV_SYN");
  for(key = 0; key < 28; key++)
    ioctl(fduinput, UI_SET_KEYBIT, keys[0][key]);
  for(key = 0; key < 18; key++)
    ioctl(fduinput, UI_SET_KEYBIT, keys[1][key]);
  for(key = 0; key < 11; key++)
    ioctl(fduinput, UI_SET_KEYBIT, keys[2][key]);
  struct uinput_user_dev uidev;
  memset(&uidev, 0, sizeof(uidev));
  snprintf(uidev.name, UINPUT_MAX_NAME_SIZE, "fbkeyboard");
  uidev.id.bustype = BUS_USB;
  uidev.id.vendor  = 1;
  uidev.id.product = 1;
  uidev.id.version = 1;
  if(write(fduinput, &uidev, sizeof(uidev)) != sizeof(uidev))
    fprintf(stderr, "error setting up uinput\n");
  if(ioctl(fduinput, UI_DEV_CREATE) == -1)
    perror("error creating uinput dev");

  buf = malloc(finfo.line_length * hight*5);
  if (buf == 0) {
    perror("malloc failed");
    exit(-1);
  }
  fill_rect(0, 0, vinfo.xres, hight*5, 0x444444);//fill background gray

  while(1) {
    char *special[] = {"ctl", "esc", "tab", "up", "down", "left", "right"};
    for (key=0; key<7; key++) {
      fill_rect(key*vinfo.xres/7, 0, (key+1)*vinfo.xres/7-1, hight*1-1, (key+29==pressed)?0xffffff:0x000000);
      draw_text(key*vinfo.xres/7, 0, special[key]);
    }
    for (key=0; key<10; key++) {
      fill_rect(key*vinfo.xres/10, hight*1, (key+1)*vinfo.xres/10-1, hight*2-1, (key==pressed)?0xffffff:0x000000);
      draw_char(key*vinfo.xres/10, hight*1, layout[layoutuse][key]);
    }
    for (key=0; key<9; key++) {
      fill_rect(vinfo.xres/20+key*vinfo.xres/10, hight*2, vinfo.xres/20+(key+1)*vinfo.xres/10-1, hight*3-1, (key+10==pressed)?0xffffff:0x000000);
      draw_char(vinfo.xres/20+key*vinfo.xres/10, hight*2, layout[layoutuse][key+10]);
    }
    fill_rect(0, hight*3, vinfo.xres*3/20-1, hight*4-1, (28==pressed)?0xffffff:0x000000);
    draw_text(0, hight*3, "shift");
    for (key=0; key<7; key++) {
      fill_rect(vinfo.xres*3/20+key*vinfo.xres/10, hight*3, vinfo.xres*3/20+(key+1)*vinfo.xres/10-1, hight*4-1, (key+19==pressed)?0xffffff:0x000000);
      draw_char(vinfo.xres*3/20+key*vinfo.xres/10, hight*3, layout[layoutuse][key+19]);
    }
    fill_rect(vinfo.xres*17/20, hight*3, vinfo.xres-1, hight*4-1, (38==pressed)?0xffffff:0x000000);
    draw_text(vinfo.xres*17/20, hight*3, "bksp");
    fill_rect(0, hight*4, vinfo.xres*3/20-1, hight*5-1, (39==pressed)?0xffffff:0x000000);
    draw_text(0, hight*4, "?123");
    fill_rect(vinfo.xres*3/20, hight*4, vinfo.xres*5/20-1, hight*5-1, (26==pressed)?0xffffff:0x000000);
    draw_char(vinfo.xres*3/20, hight*4, layout[layoutuse][26]);
    fill_rect(vinfo.xres/4, hight*4, vinfo.xres*3/4-1, hight*5-1, (36==pressed)?0xffffff:0x000000);
    fill_rect(vinfo.xres*3/4, hight*4, vinfo.xres*17/20-1, hight*5-1, (27==pressed)?0xffffff:0x000000);
    draw_char(vinfo.xres*3/4, hight*4, layout[layoutuse][27]);
    fill_rect(vinfo.xres*17/20, hight*4, vinfo.xres-1, hight*5-1, (37==pressed)?0xffffff:0x000000);
    draw_text(vinfo.xres*17/20, hight*4, "enter");

    lseek(fbfd, finfo.line_length * (vinfo.yres-hight*5), SEEK_SET);
    write(fbfd, buf, finfo.line_length * hight*5);

    released = 0;
    while (read(fdinput, &ie, sizeof(struct input_event)) && !(ie.type == EV_SYN && ie.code == SYN_REPORT)) {
      if (ie.type == EV_ABS) {
        switch (ie.code) {
          case ABS_MT_POSITION_X:
            absolute_x = ie.value;
            touchdown = 1;
            key=0;
            break;
          case ABS_MT_POSITION_Y:
            absolute_y = ie.value;
            touchdown = 1;
            key=0;
            break;
          case ABS_MT_TRACKING_ID:
            if (ie.value == -1) {
              touchdown = 0;
              released = 1;
            }
            break;
        }
      }
      if (ie.type == EV_SYN && ie.code == SYN_MT_REPORT && key) {
        touchdown = 0;
        released = 1;
      }
    }

    if(released && pressed!=-1) {
      if(pressed<=27)  //normal keys (abc...)
        send_key(keys[layoutuse>>1][pressed]);
      else if(pressed==28) {  //shift
        layoutuse ^= 1;
        ie.type = EV_KEY;
        ie.code = KEY_LEFTSHIFT;
        ie.value = layoutuse&1;
        if(write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
          fprintf(stderr, "error sending uinput event\n");
      }
      else if(pressed==29) {  //cntl
        ie.type = EV_KEY;
        ie.code = KEY_LEFTCTRL;
        ie.value = 1;
        if(write(fduinput, &ie, sizeof(ie)) != sizeof(ie))
          fprintf(stderr, "error sending uinput event\n");
      }
      else if(pressed<=38)  //special keys (space, tab, ...)
        send_key(keys[2][pressed-28]);
      else if(pressed==39)  //second page
        layoutuse ^= 2;
    }

    pressed = -1;
    if(touchdown) {
      switch(((absolute_y-abs_y.maximum)*vinfo.yres+abs_y.maximum*hight*5)/hight/abs_y.maximum) {
      case 0:
        pressed=29+absolute_x*7/abs_x.maximum;
        break;
      case 1:
        pressed=absolute_x*10/abs_x.maximum;
        break;
      case 2:
        if(absolute_x>abs_x.maximum/20 && absolute_x<abs_x.maximum*19/20)
          pressed=10+(absolute_x*10-abs_x.maximum/2)/abs_x.maximum;
        break;
      case 3:
        if(absolute_x<abs_x.maximum*3/20)
          pressed=28;//shift
        else if(absolute_x<abs_x.maximum*17/20)
          pressed=19+(absolute_x*10-abs_x.maximum*3/2)/abs_x.maximum;
        else
          pressed=38;//bksp
        break;
      case 4:
        if(absolute_x<abs_x.maximum*3/20)
          pressed=39;//123
        else if(absolute_x<abs_x.maximum*5/20)
          pressed=26;
        else if(absolute_x<abs_x.maximum*15/20)
          pressed=36;//space
        else if(absolute_x<abs_x.maximum*17/20)
          pressed=27;
        else
          pressed=37;//enter
        break;
      }
    }
  }
}

