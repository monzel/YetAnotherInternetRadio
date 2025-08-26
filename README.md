# YetAnotherInternetRadio (WebRadio)

I wanted to build a simple yet capable Webradio, that feels more like a standart FM Radio than a Webradio.
It only displays the logo of the Station, there is no Information about the artist or song.
It connects automatically to a WIFI network. I implemented two different ones, because it will be operated on two different places.
The LED blinks as long as it is trying to establish a connection. After that it stays on.
After a successfull connection, the knob can be used to go a station forward or backward.
Thats it, no more features, pretty simple. :)


## Stations
On every startup the Stations get fetched via the _stations.json_ in this repository. In this JSON there is the Name (for overview purposes), the streaming link, and a link to the Logo of the station in the _/images_ folder.
The logos are in a _.bin_ format. I used this really cool tool to convert normal images (_.jpeg, .png, ..._) into _.bin_: https://javl.github.io/image2cpp/

## Hardware
Every Old radio with a turning knob and a aux input can work. I used a broken _Sangean WR-11_, but the aux still worked.
![s-l500](https://github.com/user-attachments/assets/2187e7cc-1f20-4c8a-bca6-7f615675cf18)


## Electronics
By using this setup and connecting the step down converter there is audible noise in the sound (ground loop). This can be fixed with a _"Ground Loop Isolaror"_ connected to the aux cable.
<img width="3000" height="2596" alt="circuit_image" src="https://github.com/user-attachments/assets/7c50c69c-9573-4c87-bad7-169756cb9f4e" />
