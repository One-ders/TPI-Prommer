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

To read a prob. start the board. connect a terminal emulator, minicom f.ex.
on linux the BlackPill board will publis dev /dev/ttyACM0.

then at the prompt write promrd <CR>, then at the new prompt
write read_prom <CR>

the prom data will be dumped on the screen in Intel hex format.
You can used the terminal emulator to capture the screen.


![Board](./pics/259596032_1619722048384723_8285664125914974745_n.jpg?raw=true "Board")

This is the board,
- D0-D7 of the prom shall go to GPIO pins A1-A8
- A0-A13 of the prom shall go to GPIO pins B0-B10,B12-B14.
- CE of the prom to GPIO C13, will give a flashing blue led during reading.
- OE of the prom to GPIO A0.
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

This will make it possible to output 3 levels to prom, 0 volt low, 3.3 volts hi.
and 12 volts program hi.

GPIO C14 to pin 1 eeprom A15, (which is VPP on the 27C18).
GPIO C15 to 12 volt level shifter as described above.

The pins 1 (A15) and 22 (OE/VPP) of the eeprom will always be high
when the ecm reads the prom.
Because of this the image must be programmed to 0xC000-0xFFFF.

![Board with Calpack](./pics/259091404_477744637000775_3731667812235962616_n.jpg?raw=true "Bord with Calpack")
-------------------------------------------------------
