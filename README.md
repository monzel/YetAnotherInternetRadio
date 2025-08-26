# YetAnotherInternetRadio (WebRadio)

I wanted to build a simple yet capable webradio that feels more like a standard FM radio than a typical internet radio.  
It only displays the logo of the station; there is no information about the artist or song.  
It connects automatically to a Wi-Fi network. I implemented two different networks, because it will be operated in two different places.  
The LED blinks as long as it is trying to establish a connection. After that, it stays on.  
Once the connection is successful, the knob can be used to go forward or backward through the stations.  
That’s it, no more features, pretty simple. :)

## Stations
On every startup, the stations are fetched via the _stations.json_ in this repository.  
In this JSON file, there is the name (for overview purposes), the streaming link, and a link to the logo of the station in the _/images_ folder.  
The logos are in a _.bin_ format. I used this really cool tool to convert normal images (_.jpeg, .png, …_) into _.bin_: https://javl.github.io/image2cpp/  
Of course, the JSON can be changed to a different one.

## Hardware
- ESP32 Wrover Dev Cam (Microcontroller with PSRAM)  
- PCM5102MK 2.0 (Digital Audio Converter)  
- KY-040 (Rotary Encoder)  
- 0.96" 4-pin OLED Display  
- 330 Ω resistor  
- 10 kΩ resistor  
- DC-DC Buck Step-Down Converter  
- Cables, of course  
- An old radio with integrated amplifier  

Every old radio with a turning knob and an AUX input can work.  
I used a broken _Sangean WR-11_ where the AUX still worked.  

![s-l500](https://github.com/user-attachments/assets/2187e7cc-1f20-4c8a-bca6-7f615675cf18)

## Electronics
When using this setup and connecting the step-down converter, there is audible noise in the sound (ground loop).  
This can be fixed with a _Ground Loop Isolator_ connected to the AUX cable.  

<img width="3000" height="2596" alt="circuit_image" src="https://github.com/user-attachments/assets/7c50c69c-9573-4c87-bad7-169756cb9f4e" />
