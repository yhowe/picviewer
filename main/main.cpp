/*-
 * Copyright (c) 2019 Y. Howe <yhowe01@gmail.com>
 * All rights reserved.
 *
 * Redistribution and use in source and binary forms, with or without
 * modification, are permitted provided that the following conditions
 * are met:
 * 1. Redistributions of source code must retain the above copyright
 *    notice, this list of conditions and the following disclaimer.
 * 2. Redistributions in binary form must reproduce the above copyright
 *    notice, this list of conditions and the following disclaimer in the
 *    documentation and/or other materials provided with the distribution.
 *
 * THIS SOFTWARE IS PROVIDED BY THE AUTHOR ``AS IS'' AND ANY EXPRESS OR
 * IMPLIED WARRANTIES, INCLUDING, BUT NOT LIMITED TO, THE IMPLIED WARRANTIES
 * OF MERCHANTABILITY AND FITNESS FOR A PARTICULAR PURPOSE ARE DISCLAIMED.
 * IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
 * SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT LIMITED TO,
 * PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE, DATA, OR PROFITS;
 * OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY THEORY OF LIABILITY,
 * WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT (INCLUDING NEGLIGENCE OR
 * OTHERWISE) ARISING IN ANY WAY OUT OF THE USE OF THIS SOFTWARE, EVEN IF
 * ADVISED OF THE POSSIBILITY OF SUCH DAMAGE.
 */
#include <Arduino.h>
#include <odroid_go.h>
#include <esp_err.h>
#include <dirent.h>
#include "err.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/time.h>
#include <curses.h>

#include "FS.h"
#include "SD.h"
#include "SPI.h"

#include "display.h"
#include "esp_partition.h"
#include "esp_ota_ops.h"


extern "C" {
#include <system.h>
#include "odroid_settings.h"
}

#define PIN_BLUE_LED 2
#define AUDBUFSZ (200000)


TaskHandle_t sd_reader_task, status_task;
uint8_t *audbuf = NULL;
uint8_t *tmpbuf = NULL;
uint8_t precision = 0;
uint8_t channels = 0;

int brightness = 255;
int interlaced = 0;
int volume = 0;
int mydelay = 0;
int clearme = 0;
jpeg_div_t scale = JPEG_DIV_FS;

bool colourOK; 

const char picsDir[] = { "roms/pics" };
int oldposition;
volatile bool pressEvent = false;
int position;
int current_directory_count;
int nested_directory_index;
File tmpdir;
File file;
char nested_directory_strings[32][256];
char baseDirectory[256];
struct dirent *entries;
char *directory_to_open;
char *file_to_open;
char *command_to_run;
int  program_to_launch;
int  cols;
int  sdbusy = 0, startread = 0, startstatus = 0, statusframe = 0;
int statusactive = 0;
char tmpdisp[160];
int myheight = 240;

int bufwritepos = 0, bufcnt = 0, bufpos = 0, bufpos2 = 0;
/* Default Joystick Button Map */
int P1_NUMAXIS = 2;
int P1_NUMBUTTONS = 10;
int P2_NUMAXIS = 2;
int P2_NUMBUTTONS = 10;

char P1_A = 'a';
char P1_B = 'b';
char P1_START = 't';
char P1_SELECT = 's';
char P1_MENU = 'm';
char P1_VOL = 'v';
int P1_X = 0;
int P1_Y = 3;
char P1_LEFT = 'l';
char P1_RIGHT = 'r';
int P2_A = 1;
int P2_B = 2;
int P2_START = 9;
int P2_SELECT = 8;
int P2_X = 0;
int P2_Y = 3;
int P2_LEFT = 4;
int P2_RIGHT = 5;

bool redo_disp = true;

enum buttonType {
	UP,
	DOWN,
	LEFT,
	RIGHT,
	START,
	SELECT,
	A,
	B,
	X,
	Y,
	SLEFT,
	SRIGHT,
	MAX_JOYBUTTON
};

static void settings_menu(void);
static void sdreaderthread(void * parameter);
static void statusthread(void * parameter);
static void play_view(void);
static void find_next_frame(File *file, int offset);
static int parse_avi_header(File *file);
static int find_marker(uint8_t *samples, int total);
void odroid_system_application_set(int slot)
{
    const esp_partition_t* partition = esp_partition_find_first(
        ESP_PARTITION_TYPE_APP,
        static_cast<esp_partition_subtype_t>(ESP_PARTITION_SUBTYPE_APP_OTA_MIN + slot),
        NULL);
    if (partition != NULL)
    {
        esp_err_t err = esp_ota_set_boot_partition(partition);
        if (err != ESP_OK)
        {
            printf("odroid_system_application_set: esp_ota_set_boot_partition failed.\n");
            abort();
        }
    }
}

void setup()
{
	GO.begin();
	//fprintf (stderr, "DEV %x\n",GO.lcd);
	GO.battery.setProtection(true);
    	pinMode(PIN_BLUE_LED, OUTPUT);
	xTaskCreatePinnedToCore(sdreaderthread, "SD READ", 10000, NULL, 5,
		&sd_reader_task, xPortGetCoreID() == 1 ? 0 : 1);
	xTaskCreatePinnedToCore(statusthread, "STATUS", 10000, NULL, 2,
		&status_task, xPortGetCoreID() == 0 ? 0 : 1);
}

char anyJoystickAxis(void) {
	char key;
	key = getch();
	if (key == 'r')
		return RIGHT;
	if (key == 'l')
		return LEFT;
	if (key == 'd')
		return DOWN;
	if (key == 'u')
		return UP;

	return -1;
}

char anyJoystickButton(void) {
	char button;
	if ((button = getch()) == 'm' || button == 't' || button == 'v' ||
	    button == 's' || button == 'a' || button == 'b')
			return button;

	return -1;
}

void greeter() {
	clear();

	printSeperator(0, 4, '-');
	prettyPrint( 2, 3, "UTILITIES");
	prettyPrint( 5, 5, "PICVIEWER     ");
	prettyPrint(promptLine, 3, "Please Wait...");
	refresh();

	return;
}

typedef struct settingsItem {
	char name[32];
	int  value[3];
	void *myvar;
};

struct settingsItem settingsItems[] = { { "Volume", { 0, 10, 1 }, &volume },
			        { "Brightness", { 0, 255, 63 }, &brightness },
				{ "Scale", { 0, 5, 1 }, &scale },
				{ "Delay", { 0, 10, 1 }, &mydelay },
				{ "Clear Screen", { 0, 1, 1 }, &clearme },
				{ "Interlaced", { 0, 1, 1 }, &interlaced } };

void settings_print(int item) {
	char toDisp[256];
	int sizetoDisp, tmpvar, numfig;

	sizetoDisp = sizeof(toDisp);
	if (sizetoDisp > cols)
		sizetoDisp = cols;

	memset(toDisp, ' ', sizetoDisp);

	snprintf(toDisp, sizetoDisp, "%s", settingsItems[item].name);
	toDisp[strlen(toDisp)] = ' ';
	tmpvar = *(int *)settingsItems[item].myvar;
	numfig = 1;
	while (tmpvar /= 10)
		numfig++;

	snprintf(toDisp + sizetoDisp - numfig - 1, numfig + 1, "%d",
	    *(int *)settingsItems[item].myvar);
		mvprintw(item + 1, 0, "%s", toDisp);
}

static void settings_menu() {
	static int oldstart = -1;
	static int oldposition = -1;
	int i, axis, button, position = 0;
	bool quit = false;
	const char *updatePrompt[] = {"< Change >", "B-Back"};

	clear();

	printTitle(4,"Settings");
	printSeperator(LINES - 2, 4, '-');

	printPrompt(promptLine, 6, ' ', (char **)updatePrompt,
	    __arraycount(updatePrompt));

	attrset(COLOR_PAIR(1));
	for (i = 0; i < __arraycount(settingsItems); i++)
		settings_print(i);

	attrset(COLOR_PAIR(3));
	settings_print(position);
	oldposition = position;

	while (!quit) {
		axis = anyJoystickAxis();
		button = anyJoystickButton();

		if (axis == DOWN && pressEvent == false) {
			pressEvent = true;
			position++;
			if (position >= __arraycount(settingsItems))
				position = 0;
			attrset(COLOR_PAIR(1));
			if (oldposition != -1)
				settings_print(oldposition);

			attrset(COLOR_PAIR(3));
			settings_print(position);
			oldposition = position;
		} else if (axis == UP && pressEvent == false) {
			pressEvent = true;
			position--;
			if (position < 0)
				position = __arraycount(settingsItems) - 1;
			attrset(COLOR_PAIR(1));
			if (oldposition != -1)
				settings_print(oldposition);

			attrset(COLOR_PAIR(3));
			settings_print(position);
			oldposition = position;
		} else if (button == P1_B && pressEvent == false) {
			pressEvent = true;
			quit = true;
			continue;
		} else if (axis == RIGHT && pressEvent == false) {
			pressEvent = true;
			int tmpsetting = *(int *)settingsItems[position].myvar;
			tmpsetting += settingsItems[position].value[2];
			if (tmpsetting > settingsItems[position].value[1])
				tmpsetting = settingsItems[position].value[0];
			*(int *)settingsItems[position].myvar = tmpsetting;
			settings_print(position);
		} else if (axis == LEFT && pressEvent == false) {
			pressEvent = true;
			int tmpsetting = *(int *)settingsItems[position].myvar;
			tmpsetting -= settingsItems[position].value[2];
			if (tmpsetting < settingsItems[position].value[0])
				tmpsetting = settingsItems[position].value[1];
			*(int *)settingsItems[position].myvar = tmpsetting;
			settings_print(position);
		} else if (button == 0xff && axis == 0xff)
			pressEvent = false;
		taskusleep(10000);
	}
}

void updateDisplay() {
	static int oldstart = -1;
	static int oldposition = -1;
	int i, positionOnScreen, startEntry;
	char toDisp[256];
	int sizetoDisp;
	const char *updatePrompt[] = {"A-Select", "B-Back"};
	const char *updateRootPrompt[] = {"A-Select"};

	sizetoDisp = sizeof(toDisp);
	if (sizetoDisp > cols)
		sizetoDisp = cols;
		
	startEntry = ((int)position / (LINES - 3)) * (LINES - 3);
	positionOnScreen = (int)position % (LINES - 3);

	if (redo_disp || startEntry != oldstart) {
	redo_disp = false;
	clear();

	printTitle(4,"Select Game");
	printSeperator(LINES - 2, 4, '-');

	if (nested_directory_index > 1)
		printPrompt(promptLine, 6, ' ', (char **)updatePrompt,
		    __arraycount(updatePrompt));
	else {
		printPrompt(promptLine, 6, ' ', (char **)updateRootPrompt,
		    __arraycount(updateRootPrompt));
	}

	attrset(COLOR_PAIR(1));
	for (i = 0; i < LINES-3; i++) {
		if ((i + startEntry) >= current_directory_count) {
			break;
		}
		if (strcmp(entries[startEntry + i].d_name, "") != 0) {
			snprintf(toDisp, sizetoDisp, "%s", entries[startEntry + i].
			    d_name);
			mvprintw(i + 1, 0, "%s", toDisp);
		}
	}
	attrset(COLOR_PAIR(5));
	if (strcmp(entries[position].d_name, "") != 0 &&
	    position < current_directory_count) {
		snprintf(toDisp, sizetoDisp, "%s", entries[position].d_name);
		mvprintw(positionOnScreen + 1, 0, "%s", toDisp);
	}

	} else if (position == oldposition) {
	positionOnScreen = (int)oldposition % (LINES - 3);
	if (strcmp(entries[position].d_name, "") != 0 &&
	    position < current_directory_count)
		scrollHoriz(positionOnScreen + 1, 5, entries[position].d_name,
		    false);
	} else {
	attrset(COLOR_PAIR(1));
	positionOnScreen = (int)oldposition % (LINES - 3);
	printSeperator(positionOnScreen + 1, 1, ' ');
	if (strcmp(entries[oldposition].d_name, "") != 0 &&
	    oldposition < current_directory_count) {
		snprintf(toDisp, sizetoDisp, "%s", entries[oldposition].d_name);
		mvprintw(positionOnScreen + 1, 0, "%s", toDisp);
	}

	attrset(COLOR_PAIR(5));
	positionOnScreen = (int)position % (LINES - 3);
	if (strcmp(entries[position].d_name, "") != 0 &&
	    position < current_directory_count) {
		snprintf(toDisp, sizeof(toDisp), "%s", entries[position].d_name);
		scrollHoriz(positionOnScreen + 1, 5, toDisp, true);
	}
	}
	oldposition = position;
	oldstart = startEntry;
	refresh();

	return;
}

void (*configDisp[])() = { NULL };

void readDirEntries() {

	dirent tmp_entry;
	int i, off;

    	digitalWrite(PIN_BLUE_LED, HIGH);
	off = 0;
	for (i = 0; i < 32 && i < nested_directory_index; i++) {
		snprintf(directory_to_open + off, 4096 - off, "%s/", nested_directory_strings[i]);
		//fprintf(stderr, "%s %s %d\n", nested_directory_strings[i], directory_to_open, off);
		off = strlen(directory_to_open);
	}
	directory_to_open[off - 1] = '\0';

		//fprintf(stderr, "%s\n", directory_to_open);
	if ((tmpdir = SD.open(directory_to_open, FILE_READ)) == 0) {
		current_directory_count = 0;
		//fprintf(stderr, "HERE %s\n", directory_to_open);
	}
	else {
		i = 0;
		while (i < 4096) {
next_entry:
    			File result_entry = tmpdir.openNextFile();
			if (result_entry == 0) {
				break;
			}
			snprintf(entries[i].d_name, sizeof(entries[i].d_name),
			    "%s", result_entry.name() + off);
        		if(result_entry.isDirectory())
				entries[i].d_type = DT_DIR;
			else
				entries[i].d_type = DT_REG;
			if (strcmp(entries[i].d_name, ".") == 0 ||
			    strcmp(entries[i].d_name, "..") == 0)
				goto next_entry;
			i++;
		}

		current_directory_count = i;
	}

	/* Sort Entries */
	int sortpos;

	for (sortpos = 0; sortpos < current_directory_count; sortpos++) {
		for (i = sortpos; i < current_directory_count; i++) {
		if (strcmp(entries[i].d_name, entries[sortpos].d_name) < 0) {
			tmp_entry = entries[sortpos];
			entries[sortpos] = entries[i];
			entries[i] = tmp_entry;
		}
		}
	}
    	digitalWrite(PIN_BLUE_LED, LOW);
			
	return;
}

void quitFunc() {

	endwin();

	return;
}

int slideshow = 0;

#define MAX_SHOW 80
char *mylist = (char *)malloc(257 * MAX_SHOW);

void
loop(void) {
	bool quit = false;
	char axis, button;

	int argc = 2;
	char *argv[2] = { "loader", "" };

	if (argc != 2) {
		errx(EXIT_FAILURE, "usage: %s program root-directory", argv[0]);
	}

	atexit(quitFunc);
	sprintf(nested_directory_strings[0],"%s/%s", baseDirectory,
	    picsDir);
	entries = (struct dirent*)malloc(4096 * sizeof(struct dirent));
	directory_to_open = (char*)malloc(4096);
	file_to_open = (char*)malloc(4096);
	command_to_run = (char*)malloc(4096);
	sprintf(baseDirectory,"%s", argv[1]);

	position = oldposition = 0;
	nested_directory_index = 1;
	current_directory_count = -1;

	if (!(initscr())) {
		errx(EXIT_FAILURE, "\nUnknown terminal type.");
	}

	timeout(1);
	colourOK = has_colors();
	promptLine = LINES - 1;
	cols = COLS;

	if (colourOK) {
		start_color();
 
		init_pair(1, COLOR_WHITE, COLOR_BLACK);
		init_pair(2, COLOR_BLACK, COLOR_WHITE);
		init_pair(3, COLOR_RED, COLOR_WHITE);
		init_pair(4, COLOR_BLUE, COLOR_WHITE);
		init_pair(5, COLOR_WHITE, COLOR_GREEN);
		init_pair(6, COLOR_WHITE, COLOR_RED);

		attrset(COLOR_PAIR(1));
	}

	greeter();
	sleep(1);

	readDirEntries();
	position = 0;
	oldposition = -1;

	#define MAX_DELAY 10

	audbuf = (uint8_t *)malloc(AUDBUFSZ + 1000);
	tmpbuf = (uint8_t *)malloc(8192 * 10);
	while (!quit) {
		updateDisplay();
		oldposition = position;
		redo_disp = false;

		axis = anyJoystickAxis();
		button = anyJoystickButton();

		if (axis == DOWN && pressEvent == false) {
			pressEvent = true;
			position++;
			if (position >= current_directory_count)
				position = 0;
		} else if (axis == UP && pressEvent == false) {
			pressEvent = true;
			position--;
			if (position < 0)
				position = current_directory_count -1;
		} else if (axis == LEFT && pressEvent == false) {
			pressEvent = true;
			position -= LINES - 3;
			if (position < 0)
				position = current_directory_count -1;
		} else if (axis == RIGHT && pressEvent == false) {
			pressEvent = true;
			position += LINES - 3;
			if (position >= current_directory_count)
				position = 0;
		} else if (button == P1_B && pressEvent == false) {
			pressEvent = true;
			tmpdir.close();
			if (nested_directory_index > 1) {
				nested_directory_index--;
				position = 0;
				oldposition = -1;
				redo_disp = true;
				readDirEntries();
			}
		} else if (button == P1_SELECT && pressEvent == false) {
			pressEvent = true;
		} else if (button == P1_MENU && pressEvent == false) {
			pressEvent = true;
			settings_menu();
			redo_disp = true;
			continue;
		} else if (button == P1_A && pressEvent == false) {
			pressEvent = true;
			if (entries[position].d_type == DT_DIR &&
			    position < current_directory_count) {
				sprintf(nested_directory_strings
				    [nested_directory_index], "%s", entries
				    [position].d_name);
				nested_directory_index++;
				redo_disp = true;
				position = 0;
				oldposition = -1;
				tmpdir.close();
				readDirEntries();
			} else if (entries[position].d_type == DT_REG) {
				/* Launch File */
				sprintf(file_to_open,"%s/%s", directory_to_open,
				    entries[position].d_name);
				if (slideshow < MAX_SHOW) {
					snprintf(&mylist[257 * slideshow++],
					    256, "%s", file_to_open);
				}
				position++;
				if (position >= current_directory_count)
					position = 0;
				oldposition = -1;
			}
		} else if (button == P1_START && pressEvent == false) {
			pressEvent = true;
			play_view();
			slideshow = 0;
			redo_disp = true;
		} else if (button == 0xff && axis == 0xff)
			pressEvent = false;
		taskusleep(10000);
	}
}

#define SKIPSZ 1000000
static void
find_next_frame(File *file, int offset)
{
	taskusleep(2000);
	while(sdbusy)
		taskusleep(1000);
	int mypos = file->position();
	int mysize = file->size();

	int newpos = mypos + offset;
	if (newpos >= mysize)
		newpos = mysize - SKIPSZ;
	if (newpos < 0)
		newpos = 0;
//fprintf(stderr, "offset = %d NEWPOS: %d\n",offset, newpos);
    	digitalWrite(PIN_BLUE_LED, HIGH);
	file->seek(newpos, SeekSet);
	mysize = file->read(tmpbuf, 8192 * 10);
    	digitalWrite(PIN_BLUE_LED, LOW);
	int found = find_marker(tmpbuf, mysize);
	if (found < 0)
		found = -found;
	if (found)
		found--;

	file->seek(newpos + found, SeekSet);
}

static int
find_marker(uint8_t *samples, int total)
{
	int newpos = 0;
again:
	char mydat;
	bool audiomk = false;
	bool videomk = false;
//fprintf(stderr, "%x",mydat);
	if (newpos >= total - 3)
		return 0;
	mydat = *(samples + newpos++);
	if (mydat == 0xff || mydat == '0') {
		if (mydat == 0xff)
			videomk = true;
		if (mydat == '0')
			audiomk = true;
	} else
		goto again;

	mydat = *(samples + newpos++);
	if (videomk && mydat != 0xd8)
		goto again;
	if (audiomk && mydat != '1')
		goto again;
	mydat = *(samples + newpos++);
	if (videomk && mydat != 0xff)
		goto again;
	if (audiomk && mydat != 'w')
		goto again;
	mydat = *(samples + newpos++);
	if (videomk && mydat != 0xe0)
		goto again;
	if (audiomk && mydat != 'b')
		goto again;

//fprintf(stderr, "NEWPOS %x video %x audio %x\n", newpos,videomk,audiomk);
	if (videomk)
		return -(newpos - 3);

	if (audiomk)
		return newpos - 3;
	return 0;
}

static void statusthread(void * parameter) {
	struct timeval now;
	int mystatus = 0;
	long int timenow, lasttime = 0;
	for (;;) {
		gettimeofday(&now, NULL);
		timenow = now.tv_sec * 1000000 + now.tv_usec;
		while (!startstatus && !mystatus)
			taskusleep(10000);
		if (startstatus == 1) {
			mystatus = statusframe;
			lasttime = timenow + 2000;
		}
		startstatus = 0;

		if ((timenow > lasttime) && mystatus) {
		//fprintf(stderr, "STATUS: %d\n",mystatus);
			statusactive = 1;
			lasttime = timenow + 200000;
			if (mystatus > 8) {
				//fprintf (stderr, "dev %x\n",GO.lcd);
				GO.lcd.initShadow();
				GO.lcd.lock_display_thread();
				prettyPrint(promptLine, 1, tmpdisp);
				myheight = 220;
				GO.lcd.unlock_display_thread();
			}
		//fprintf(stderr, "STATUS: %d\n",mystatus);

			if (mystatus < 2) {
				memset(tmpdisp, ' ', strlen(tmpdisp));
				GO.lcd.initShadow();
				GO.lcd.lock_display_thread();
				prettyPrint(promptLine, 1, tmpdisp);
				myheight = 240;
				GO.lcd.unlock_display_thread();
			}
			mystatus--;
			statusframe--;
			statusactive = 0;
		}
		taskusleep(1000);
	}
}

static void sdreaderthread(void * parameter) {
	for (;;) {
		int nr = 0;
		while (startread == 0)
			vTaskDelay(1);
		if (audbuf != NULL) {
			sdbusy++;
    			digitalWrite(PIN_BLUE_LED, HIGH);
			int toget = (AUDBUFSZ - bufcnt);

#if 0
			while(GO.lcd.dispActive()) {
				taskusleep(1000);
			}
#endif
			int total = toget;
			int dataread = 0;
			while (total > 0 ) {
				if (bufwritepos + total > AUDBUFSZ)
					dataread = AUDBUFSZ - bufwritepos;
				else
					dataread = total;

				nr = file.read(audbuf + bufwritepos, dataread);
				if (nr <= 0)
					break;

				bufcnt += nr;
				bufwritepos += nr;
				if (bufwritepos >= AUDBUFSZ)
					bufwritepos = 0;
				total -= dataread;
			}
			if (total) {
				memset(audbuf + bufwritepos, '0',
				    total);
			}
    			digitalWrite(PIN_BLUE_LED, LOW);

			sdbusy--;
		}
		startread = 0;
	}
}

static void
play_view(void)
{
	int offY, offX, picstop = 0;
	for (int i = 0; i < slideshow && !picstop; i++) {
		int avail = 0;
		myheight = 240;
		while (statusframe == 10)
			taskusleep(1000);
		statusframe = 0;
		offY = 0;
		offX = 0;
		bool select_again = true;
		while (GO.lcd.dispActive())
			taskusleep(1000);
		GO.lcd.initShadow();
		GO.lcd.setInterlaced(interlaced);
		GO.lcd.setBrightness(brightness);
		while(sdbusy)
			taskusleep(1000);
		file = SD.open(&mylist[i * 257]);
		bool next_file = false;
		int next_frame = 0;
		if (!file)
			continue;
		int mysize = file.size();
		if (clearme)
			clear();
		int keyframe = 0, after_frame = 0;
		int audlen = 0, audorig = 0, audstart = 0, audtotal = 0;
		bool audio_playing = false;
		int len = 0, jumpsize = SKIPSZ;;
		int oldbufpos, oldbufcnt;
		char prev_key = 0;

		bufwritepos = 0;
		bufcnt = 0;
		bufpos = 0;
		bufpos2 = 0;
		oldbufpos = 0;
		oldbufcnt = 0;
		#define MAX_ERRORS 2
		int errors = 0;
		bool has_audio = false;
		pressEvent = false;
		int samplerate = parse_avi_header(&file);
		if (samplerate > 22000)
			samplerate += 4000;
		find_next_frame(&file, 0);
		startread = 1;
		do {
			bufpos += len;
			bufcnt -= len;
			startread = 1;
			if (bufcnt <= 60000) {
				taskusleep(2000);
				while(sdbusy)
					taskusleep(1000);
			}
			if (bufpos >= AUDBUFSZ)
				bufpos -= AUDBUFSZ;
			//fprintf(stderr, "BUF %d\n",bufcnt);
			if (bufcnt <= 0) {
				next_file = true;
				keyframe = 0;
				goto next_frame;
			}

			avail = ((AUDBUFSZ - bufpos - 1) % AUDBUFSZ) + 1;
			if (avail > bufcnt)
				avail = bufcnt;
			if (avail < 0)
				avail = 0;
	//fprintf(stderr, "BUF %d LEN %d\n", bufcnt, len);
		//fprintf(stderr, "CNT %d AVAIL %d\n", bufcnt, avail);
			next_frame = find_marker(&audbuf[bufpos], avail);
			if (errors) {
				int res = next_frame;
				if (res < 0)
					res = -res;
				if (res)
					res--;
	//fprintf(stderr, "res %d\n",res);
				errors = 0;
	//fprintf(stderr, "ZERO - RES %d LEN %d\n", res, len);
				len = res;
				continue;
			}
		//fprintf(stderr, "NEXT %x\n", next_frame);
			keyframe++;
			if (audio_playing)
				keyframe = 0;
			after_frame = find_marker(&audbuf[bufpos + 1], avail -1);
			len = after_frame;
			if (after_frame < 0)
				len = -after_frame;

	//fprintf(stderr, "NF %x AF %x len %d\n", next_frame, after_frame, len);
			if (after_frame == 0) {
	//fprintf(stderr, "ZERO %d\n", len);
				len = avail;
	//fprintf(stderr, "ZERO2 %d\n", len);
				keyframe = 0;
				errors = 1;
				if (bufcnt <= 0) {
					next_file = true;
					goto next_frame;
				} else if (has_audio)
					continue;
			} else if (after_frame == 1 && audio_playing)
				len = audtotal;

			if (audio_playing) {
				next_frame = 1;
				after_frame = 1;
			}

			oldbufpos = bufpos;
			oldbufcnt = bufcnt;

			if (next_frame < 0 && !audio_playing) {
			//fprintf(stderr, "DISPLAY\n");
				if (!has_audio)
					keyframe = 0;
				if (after_frame != 0)
					errors = 0;
			} else if (samplerate > 0) {
				if (!audio_playing) {
					audio_playing = true;
					audlen = audbuf[bufpos + 7] << 24 |
					    audbuf[bufpos + 6] << 16 |
					    audbuf[bufpos+5] << 8 |
					    audbuf[bufpos + 4];
					if (audlen <= 0)
						audlen = 0;
					audstart = bufpos + 8;
					audtotal = audlen;
					audorig = audlen;
	//fprintf(stderr, "ALEN %x\n", audlen);
					if (audorig < 20 && has_audio) {
						len = bufcnt;
						//fprintf(stderr, "NONONO");
						continue;
					}
				} else
					audstart = bufpos;
				if (audtotal > AUDBUFSZ - bufpos)
					audtotal = (AUDBUFSZ - bufpos) / 8;
	//fprintf(stderr, "AUDTOTAL %d\n", audtotal);
				
				has_audio = true;
    				GO.Speaker.setVolume(volume);
		//fprintf(stderr, "AUDIO\n");

				if (has_audio && samplerate > 0 && samplerate
				    <= 44100 && audtotal > 0) {
					GO.Speaker.playMusic
					    (&audbuf[audstart],
					    samplerate, audtotal);
				}
				audlen -= audtotal;
				if (audlen <= 0)
					audio_playing = false;
			}
displayme:
			//fprintf(stderr, "POS %d CNT %d\n",bufpos, avail);

			if (next_frame < 0) {
				while (!has_audio && GO.lcd.dispActive())
					taskusleep(1000);
				if (statusframe == 10)
					startstatus = 1;
				GO.lcd.drawJpgThreaded (audbuf + bufpos, len,
				    0, 0, 320, myheight, offX, offY, scale,
				    true);
			}
next_frame:
			if (keyframe % 32 != 0)
				continue;

			if (!has_audio)
				taskusleep((mydelay * 1000000) / 2);
			int axis = anyJoystickAxis();
			int btn = anyJoystickButton();
			if (btn == P1_B && pressEvent == false) {
				pressEvent = true;
				select_again = false;
				picstop = 1;
				break;
			} else if (btn == P1_A && pressEvent == false) {
				pressEvent = true;
					taskusleep(800000);
				while (anyJoystickButton() == 0xff)
					taskusleep(1000);
			} else if (btn == P1_START && pressEvent == false) {
				pressEvent = true;
				select_again = false;
				next_file = true;
			} else if (btn == P1_VOL && pressEvent == false) {
				pressEvent = true;
				volume++;
				if (volume > 10)
					volume = 0;
				snprintf(tmpdisp, sizeof(tmpdisp),
				    "Volume: %d", volume);
				statusframe = 10;
    				GO.Speaker.setVolume(volume);
			} else if (btn == P1_SELECT && pressEvent == false) {
				pressEvent = true;
				switch (scale) {
				case JPEG_DIV_FS:
					scale = JPEG_DIV_NONE;
					break;
				case JPEG_DIV_NONE:
					scale = JPEG_DIV_2;
					break;
				case JPEG_DIV_2:
					scale = JPEG_DIV_4;
					break;
				case JPEG_DIV_4:
					scale = JPEG_DIV_8;
					break;
				case JPEG_DIV_8:
				default:
					scale = JPEG_DIV_FS;
					break;
				}
				while(GO.lcd.dispActive())
					vTaskDelay(1);
				clear();
				GO.lcd.initShadow();
				snprintf(tmpdisp, sizeof(tmpdisp),
				    "Scale: %d", scale);
				statusframe = 10;
			} else if (axis == DOWN && pressEvent == false) {
				pressEvent = true;
				while(GO.lcd.dispActive())
					vTaskDelay(1);
				GO.lcd.initShadow();
				offY += 10;
				if (offY > 230)
					offY = 230;
			} else if (axis == UP && pressEvent == false) {
				pressEvent = true;
				while(GO.lcd.dispActive())
					vTaskDelay(1);
				GO.lcd.initShadow();
				offY -= 10;
				if (offY < 0)
					offY = 0;
			} else if (axis == RIGHT && pressEvent == false) {
				pressEvent = true;
				while(GO.lcd.dispActive())
					vTaskDelay(1);
				GO.lcd.initShadow();
				if (after_frame != 0) {
					if (prev_key == btn)
						jumpsize += SKIPSZ;;
					prev_key = btn;
					find_next_frame(&file, jumpsize);
					errors = 0;
					bufpos = 0;
					bufcnt = 0;
					bufwritepos = 0;
					len = 0;
					audlen = 0;
					has_audio = false;
					audio_playing = false;
					audtotal = 0;
					float pused = ((float)file.position()
					    / (float)mysize) * 100;
					snprintf(tmpdisp, sizeof(tmpdisp),
					    "Position: %3.0f%%", pused);
					    
				statusframe = 10;
				} else {
					offX += 10;
					if (offX > 310)
						offX = 310;
				}
			} else if (axis == LEFT && pressEvent == false) {
				pressEvent = true;
				while(GO.lcd.dispActive())
					vTaskDelay(1);
				GO.lcd.initShadow();
				if (after_frame != 0) {
					if (prev_key == btn)
						jumpsize += SKIPSZ;;
					prev_key = btn;
					find_next_frame(&file, -(bufcnt +
					    jumpsize));
					errors = 0;
					bufpos = 0;
					bufcnt = 0;
					bufwritepos = 0;
					len = 0;
					audlen = 0;
					has_audio = false;
					audio_playing = false;
					audtotal = 0;
					float pused = ((float)file.position()
					    / (float)mysize) * 100;
					snprintf(tmpdisp, sizeof(tmpdisp),
					    "Position: %3.0f%%", pused);
				statusframe = 10;
				} else {
					offX -= 10;
					if (offX < 0)
						offX=0;
				}
			} else if (btn == P1_MENU && pressEvent == false) {
				pressEvent = true;
				float pused = ((float)file.position() -
				    (float)(AUDBUFSZ - bufpos)) / (float)mysize
				    * 100;
				snprintf(tmpdisp, sizeof(tmpdisp),
				    "Position: %3.0f%%", pused);
				statusframe = 10;
			} else if (btn == 0xff && axis == 0xff) {
				select_again = false;
				jumpsize = SKIPSZ;
				prev_key = 0;
			}
			pressEvent = false;
			if (select_again && !has_audio) {
				next_file = false;
				errors = 0;
				len = 0;
				bufpos = oldbufpos;
				bufcnt = oldbufcnt;
			}
			if (!has_audio)
				taskusleep((mydelay * 1000000) / 2);
		} while (!picstop && next_file == false);
		while(GO.lcd.dispActive() || statusframe) {
		//	fprintf(stderr, "status: %d disp %x\n",statusframe,
		//	    GO.lcd.dispActive());
			startstatus = 1;
			taskusleep(10000);
		}
		file.close();
    		GO.Speaker.mute();
	}
}

static int
parse_avi_header(File *file)
{
	file->seek(0, SeekSet);
	int mysize = file->size();
	if (mysize > 0x1200)
		mysize = 0x1200;

	precision = 0;
	channels = 0;
	int sample_rate = 0;
	for(;;) {
		if (file->position() >= 0x1200)
			break;
		if (file->read() != 'L')
			continue;
		if (file->read() != 'I')
			continue;
		if (file->read() != 'S')
			continue;
		if (file->read() != 'T')
			continue;
		if (file->read() != 0x7c)
			continue;
		if (file->read() != 0x10)
			continue;

		file->seek(file->position() + 14, SeekSet);
		if (file->read() != 'a')
			continue;
		if (file->read() != 'u')
			continue;
		if (file->read() != 'd')
			continue;
		if (file->read() != 's')
			continue;
		if (file->read() != 0x01)
			continue;
	//fprintf(stderr, "HERE\n");
		file->seek(file->position() + 19, SeekSet);
		file->read((uint8_t *)&sample_rate, sizeof(sample_rate));
		file->seek(file->position() + 16, SeekSet);
		file->read((uint8_t *)&precision, sizeof(precision));
		file->seek(file->position() + 21, SeekSet);
		file->read((uint8_t *)&channels, sizeof(channels));
	}

	if (channels)
		precision /= channels;
	precision *= 8;

	fprintf(stderr, "INFO: %d BIT / %d CHANNELS / %d Hz\n", precision,
	    channels, sample_rate);
	if (channels != 1 || precision != 8) {
		if (channels != 1)
			fprintf(stderr, "No of channels is not supported\n");
		if (precision != 8)
			fprintf(stderr, "This precision is not supported\n");
		fprintf(stderr, "INFO: AUDIO DISABLED\n");
		sample_rate = 0;
	}

	file->seek(0, SeekSet);
	return sample_rate;
}
