# TPI Prommer

What is it, and what does it do.
================================
It is software to read the 27C128 proms from the GM ECM 1227165, used
in (among others) 86-89 Corvettes and Camaros (TPI L98).

This software needs to be linked with the small Realtime OS available as a
repo here. First clone that repo, and build it. Then
take out this repos, update the Makefile to point to board directory of
of the OS. then do Make.

I have build it against the small board called "BlackPill", running an
STM32F411 and costs less than 10$ on ali express.

To read a prom. start the board. connect a terminal emulator, minicom f.ex.
on linux the BlackPill board will publish dev /dev/ttyACM0.

then at the prompt write prom <CR>, then at the new prompt
write read <CR>

the prom data will be dumped on the screen in Intel hex format.
You can used the terminal emulator to capture the screen.

For burning, (today only tested with ebay 27SF512 fakes, that seems to work),
make sure the chip is properly erased (dump it and verify 0xff in all bytes).

(After doing an ID read on the chip, it identifies as WINBOND W27C512.
It seems to work but should have 14 volts for erase).

else run an erase.

1. at the prompt do: prom<CR>
2. configure 27SF512<CR>
3. erase

If it fails try to power cycle the chip by removing and re-inserting the
usb connector, and continue from point 1.

To burn a file, first obtain a wanted binary...
either in Intel hex format, or use gnu objcopy or similar to convert to
ihex...
Edit the file and surround the text with the lines:
to start with
- ******************* IHEX ******************

and end with
- *************** End IHEX ******************

. after configuring 27SF512
give cmd: burn<CR>

The program will tell you to transfer the file using the terminal emulator,
on linux use minicom, hit ctrl-A, S, select ascii , etc.

When using 27SF512 as a replacement for the 27C128, the image needs to be
offset towards the upper 16K region of the chip. This is because the upper
address pin A14-A15 on 27SF512, corresponds to VPP and PGM on 27C128 and they
are HI during normal read. So to use 27SF512  for an L98 system do:
burn -o 0xC000<CR>.

![Board](./pics/259596032_1619722048384723_8285664125914974745_n.jpg?raw=true "Board")

This is the board,
- D0-D7 of the prom shall go to GPIO pins A1-A8
- A0-A14 of the prom shall go to GPIO pins B0-B10,B12-B15.
- A15 of the prom connects to GPIO pin A9
-- B9 to A9 needs a detour via a level shifter, since the pin is also used on SF512
   as ERASE pin (A9 12v GPIO PA15)
- CE of the prom to GPIO C13, will give a flashing blue led during chip select.
- OE of the prom via level shifter to GPIO A0.
- OE of the prom is used on SF512 as VPP during program, GPIO pin PA10 to level shifter.
- VSS to ground.
- VDD to 5V.


----------------------------------

Mods to enable programming and using the SST 27SF512.

Add a 12 volts source, a 5 to 12 stepup converter or an external source.

add lines B15 to to pin 27, it corresponds to /PGM on the 27C128 and
should be hi when reading (missed in the version above).
This pin is A14 27SF512.
connect A15 to a cascaded npn/pnp transistor, so that 3 volts in on
the base of the npn, will make it open, and the collector will sink the
base of the pnp that has its emitter to 12 volt. the collector of the
pnp should go via a diode to prom A9. The original prom A9 should have a
diode from gpio b9 to prom a9.

![LevelShifter](./pics/levelshift.png?raw=true "levelshifter")

This will make it possible to output 3 levels to prom, 0 volt low, 3.3 volts hi.
and 12 volts program hi.

GPIO C14 to pin 1 eeprom A15, (which is VPP on the 27C18).
GPIO C15 to 12 volt level shifter as described above.

The pins 1 (A15) and 22 (OE/VPP) of the eeprom will always be high
when the ecm reads the prom.
Because of this the image must be programmed to 0xC000-0xFFFF.

![Board with Calpack](./pics/259091404_477744637000775_3731667812235962616_n.jpg?raw=true "Bord with Calpack")

Adding Level shifters for 12Volts pins on 27SF512 (Winbond 27C512).
One more would be needed for burning the original 27C128.
![Board with 12V levelshifters](./pics/264455280_726742265382862_1499498962192463802_n.jpg?raw=true "Bord with 12V level shifters")



The blackpill stm32411 board pin out:
![Blackpill](./pics/STM32F4x1_PinoutDiagram_RichardBalint.png?raw=true "stm32f411 blackpill")

-------------------------------------------------------
