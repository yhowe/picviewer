# picviewer
Movie player and picture viewer for odroid go.

PICVIEWER
=========

Picviewer is a jpg/avi (modified computer playable) player.

This uses the tjpgd library and can only render baseline jpeg images/frames.
It can only play movies in an AVI container format.
Audio can be of any sane sample rate (only tested to 16kHz), 8 bit, mono, PCM.
The smaller the dimensions of the video the faster it renders.
	320 x 240
		7fps
	180 x 160
		12 - 15 fps
	160 x 120
		22 fps

To encode movies with ffmpeg:
	ffmpeg -i my.vid -acodec pcm_u8 -ac 1 -sample_fmt u8 -ar 16000
	    -vodec mjpeg -vf scale=-1:120 output.avi

The keys:
MENU: Settings menu - Volume, Brightness, Scale, Delay (for jpgs), Interlaced,
		      Clear Screen.
		      
      Volume (0 - 10)
		0  - silent
		10 - loudest
      Brightness (0 - 255)
		0 - backlight off
		255 full brightness
      Scale (0 - 5)
		0 - original size
		1 - 1/2 size
		2 - 1/4 size
		3 - 1/8 size
		4 - 1/16 size
		5 - fullscreen (scaled)
      Delay (0 - 20)
		Delay between jpegs on playlist or between
		frames on avis with no audio.
		0 - no delay
		Delay is expressed in seconds.
      Interlaced (0 - 1)
		Use interlaced rendering (speed hack)
			With this its possible to play an avi of
			baseline jpg frames at 160x120 with 8 bit mono
			pcm audio at 16kHz at 22 fps.
      Clear Screen (0 - 1)
		0 - Don't clear screen
		1 - Clear screen
			Use with interlaced to clear the screen before
			decoding the file.
     
START:
      Display playlist.
B:
      Clear playlist/Back ( when in menu settings ).
A:
      Select Item.  If it is a directory it will be opened, otherwize if its a
      file it will be added to the play list

Arrow keys:
	Up down to navigate files.  Left right to change menu settings items.

While playing:
	Volume     - Increase volume by 1.
	Start      - Change scale by 1.
	A          - Pause / Resume
	B          - Abort ( Return to file list ).
	Arrow keys - Navigate around Jpeg.

When playing has finished or has been aborted the current playlist is cleared.

Movies / Jpegs should be placed in the roms/pics folder on the sdcard.

Nested subdirectories are alowed with a maximum displayable entries at 1024
items per directory.

If connected to usb serial while playing the frame rate is printed on the
seial connection.
