# MIDItoSyncConverter
Converts MIDI clock messages to 2PPQ sync signals.

## How to build
### Target microcontroller
- Microchip ATtiny202

![schematic](./schematic.png)  

### Build
This project uses PlatformIO to build.
- platform: Atmel megaAVR

When the build is complete, write via [UPDI](https://www.google.com/search?q=microchip+updi).

## Usage
Connects to the sending MIDI device with a MIDI cable and to the receiving device with a 3.5 mm stereo cable.  
The mode switch can be used to select the action to be taken when a MIDI START/STOP message is received.

### Mode switch
The PA2 pin is assigned to mode switch. PA2 is pulled up and goes High when open.

|PA2 |behaviour|
|----|---------|
|Low |Sends out or stops the sync signal in conjunction with MIDI Start and Stop messages.|
|High|Free run. When MIDI Clock is received, the Sync signal is sent regardless of the playback status of the MIDI device.|

## About the pulse width of the sync signal
In this project, the pulse width of the sync signal is set to 5 ms.  
The reason for this is that the KORG specification is 15 ms, but many other companies' devices use 5 ms.  
If you want to change this, change the value of ```TCA.SINGLE.PER``` in ```TCA0_init()``` and rebuild.

## License
MIDItoSyncConverter is open source and licensed under the [GPL3](/LICENSE) License.
