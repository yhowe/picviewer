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
#include <sys/time.h>
#include <stdio.h>
#include <string.h>
#include <curses.h>

#include "./display.h"

#if defined(ESP32)
#include <odroid_go.h>
#endif

#include "debug.h"
#if defined(BT_DEBUG) && defined(ESP32)
#include "btdebug.h"
#endif

int dispLine, currentLine, totalLines;
int promptLine;
bool help_menu = false;


void
printSeperator(int line, int color, const char sep)
{
	int i;

	attrset(COLOR_PAIR(color));
	for (i = 0;i < COLS;i++)
		mvprintw(line, i, "%c", sep);

	return;
}

void
scrollHoriz(int line, int color, char *headline, bool reset)
{
	static long long last_time = 0;
	static int position = 0;
	long long time_now, secs_wait = 300;
	struct timeval mytime;
	char toDisp[255];
	int sizetoDisp = COLS;

	gettimeofday(&mytime, NULL);
	time_now = (mytime.tv_sec * 1000) + (mytime.tv_usec / 1000);
	if (time_now > last_time + secs_wait) {
		last_time = time_now;
		if (reset || position > strlen(headline))
			position = 0;
		snprintf(toDisp, sizetoDisp, "%s", headline + position);
		prettyPrint(line, color, toDisp);
		if (strlen(headline) - position >= COLS)
			position++;
		else
			position = 0;
	}
}

void
printPrompt(int line, int color, char background, char **prompts, int numPrompts)
{
	if (numPrompts < 1)
		return;

	int i, promptPosition;
	int totalPromptSize = 0;
	for (i = 0; i < numPrompts;i++)
		totalPromptSize += static_cast<int>(strlen(prompts[i]));

	int initSpacing = ((COLS - totalPromptSize)/(numPrompts + 1));
	size_t gap = 0, spacing = static_cast<size_t>(initSpacing);

	printSeperator(line, color, background);
	for( promptPosition = 0; promptPosition < numPrompts;
	    promptPosition++ ) {
		mvprintw(line, static_cast<int>(gap + spacing), "%s", prompts[promptPosition]);
		gap += spacing + strlen(prompts[promptPosition]);
	}

	return;
}

void
printTitle(int color, char *headline)
{
#if defined(ESP32)
	char tmp[255];
	int bpercent;

	bpercent = GO.battery.getPercentage();
	snprintf(tmp, sizeof(tmp), "%s BATT:%03.0d%%\n", headline, bpercent);
	if (bpercent < 60)
		printHeadline(0, 6, tmp);
	else if (bpercent < 80)
		printHeadline(0, color, tmp);
	else
		printHeadline(0, 5, tmp);
#else
	printHeadline(0, color, headline);
#endif

	return;
}

void
printHeadline(int line, int color, char *headline)
{
	printPrompt(line, color, '-', &headline, 1);

	return;
}

void
errorPrint(int line, int color, char *headline)
{
	printPrompt(line, color, ' ', &headline, 1);
	DPRINTF("%s", headline);

	return;
}

void
prettyPrint(int line, int color, char *headline)
{
	printPrompt(line, color, ' ', &headline, 1);

	return;
}

void
startupInfo(void)
{
	clear();

	printHeadline(0, 4, "PIC VIEWER");
	attrset(COLOR_PAIR(1));
	mvprintw(2, 0, "PIC VIEWER");
	mvprintw(4, 0,        " A - SELECT"); 
	mvprintw(5, 0,        " B - UNSELECT");
	mvprintw(7, 0,        " START  - START SLIDESHOW");
	mvprintw(8, 0,        " SELECT - TIME DELAY +2s");
	mvprintw(9, 0,        " MENU   - THIS HELP");
	refresh();

	help_menu = true;

	return;
}

int
clearHome(int orig)
{
	if (help_menu) {
		help_menu = false;
		clear();

		return 2;
	}

	return orig;
}
