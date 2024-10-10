# palm-calendar-sync2
Make your Palm Pilot useful again by downloading your calendar to it (attempt #2).

If you've got an old [Palm Pilot](https://en.wikipedia.org/wiki/PalmPilot) kicking around you've probably had some fun playtime nostalgia getting it out, but noticed that it's not so easy to make use of it's PIM (personal information management) functionality now that almost all that stuff is an online service. This project attempts to remedy that somewhat for the Datebook function.

`calendar-sync2` is a tool to read an [iCalendar](https://en.wikipedia.org/wiki/ICalendar) (ical/ics)  formatted date-book/calendar and send it to a Palm Pilot using a HotSync. This should allow for an up-to-date at the time of HotSync calendar to be available on a Palm device.

A YouTube video outlining the project:

[![Getting Google Calendar on a Palm Pilot](resources/youtube-thumb.jpg)](https://www.youtube.com/watch?v=iAAXIlFZGh8)


## Background

I previously attempted a similar project, [google-calendar-to-palm-pilot](https://github.com/guruthree/google-calendar-to-palm-pilot), but ran into several probably fatal flaws that led to me abandoning it:

1. No way to handle moved/deleted repeat events
2. The calendar was always overwritten
3. Dependency on python2.7 requiring a [conda](https://docs.conda.io/en/latest/) environment
4. Dependency on the [pilot-datebook](https://github.com/guruthree/pilot-datebook) tool
5. No alarms would be copied

These issues have been addressed by writing the application in C(++) this time and directly using the [pilot-link](https://tldp.org/HOWTO/PalmOS-HOWTO/pilotlink.html) libpisock library.


## Features

* Fetches multiple calendars over http/https
* Read and merges multiple calendars with existing calendar, preserving any Palm-only Datebook events
* Works with USB, serial, and network HotSync
* Fully compatible (I hope) with the [Google-calendar ics export](https://support.google.com/calendar/answer/37648?hl=en#zippy=%2Cget-your-calendar-view-only)
* Alarms (optional)
* Repeating events, with exclusions and moved events
* Descriptions, location, and attendees added to a Note
* Dates and times translated to specified time zone


## Getting palm-calendar-sync2

At the moment this project requires Linux of some description. Theoretically I think there's nothing that would stop it from working on Windows, either via the [Windows Subsystem for Linux](https://learn.microsoft.com/en-us/windows/wsl/install) (WSL) or [Cygwin](https://www.cygwin.com/), but I haven't tested it. I've no idea about macOS - let me know if you get it working! Linux in a Virtual Machine with a pass-through connection to your Palm Pilot (either USB, serial, or network) should also be sufficient.

OK, now that you have a Linux environment. There are two options. You could install the dependencies, create a build environment, and compile `calendar-sync2` (see the Compiling section below). Alternatively, I have provided a pre-compiled version of `calendar-sync2` in an [Apptainer](https://apptainer.org/) image.

What is an Apptainer image? It is a containerised environment similar to Docker, containing a the files needed to run the application, but running in user space without escalated privileges. In my case the image consists of stripped down Arch Linux, the dependencies (including pilot-link), and `calendar-sync2`. This is a bit bulkier, but should run on a wide variety of Linux distributions as long as Apptainer is installed and means I don't have to worry about dependencies.

### Getting the Apptainer image

You can build the Apptainer image yourself of the latest version by downloading the [definition file](https://github.com/guruthree/palm-calendar-sync2/blob/main/distribution/calendar-sync2.def), and then running `apptainer build calendar-sync2.sif calendar-sync2.def`.

You can also download an Apptainer image from the [releases page](https://github.com/guruthree/palm-calendar-sync2/releases/) or the most recent release [v0.0.1 (fb7779f)](https://github.com/guruthree/palm-calendar-sync2/releases/download/v0.0.1/calendar-sync2.sif).

### Running calendar-sync2 from the Apptainer image

* `apptainer run calendar-sync2.sif` will run the main application.
* `apptainer run-help calendar-sync2.sif` will provide information on the applications in the image.
* `apptainer run --app pilot-xfer calendar-sync2.sif` will run the pilot-link pilot-xfer tool. pilot-xfer can be replaced with any of the pilot-link applications.

`calendar-sync` may also be run from the image in a short hand form for just  as `./calendar-sync2.sif`. This still requires Apptainer to be installed.

## Usage

After getting and configuring `calendar-sync2`, the general usage would be as follows:

1. Run `calendar-sync2`.
2. Trigger a HotSync.
3. Enjoy using your Palm to browse your upcoming events.

More specifically, `calendar-sync2` is controlled primarily through a configuration file (default `datebook.cfg`) and [an example](https://github.com/guruthree/palm-calendar-sync2/blob/main/datebook.cfg) is included that will sync the libical recurring event test file and the Google UK Holiday calendar to a Palm device connected via USB. The example config file contains explanations of the configuration options, but the most important settings are:

* `URI` which specifies the location(s) of the calendar, either as a single calendar `URI="https://address"` or a list of addresses `URI=("https://address1", "https://address2"`).
* `PORT` which specifies how the Palm will connect (typically either via `PORT="usb:"` or a serial port such as `PORT="/dev/ttyUSB0"`).
* `OVERWRITE` which will specify if `calendar-sync2` overwrites the existing Datebook on the Palm. **WARNING: By default `calendar-sync2` will overwrite the existing Datebook.**

Useful settings include:

* `TIMEZONE` which should be set to your local time zone so that events are at the correct times, as otherwise times will be in UTC (GMT+0).
* `FROMYEAR` as a YYYY year indicates a cut-off year for events to be copied to the palm to reduce resource consumption.
* `PREVIOUSDAYS` as a number of days indicates events older than that number of days at time of sync will not be copied the palm to reduce resource consumption.
* `SKIPNOTES` when set to true will not add a note to events with descriptions/atendees/locations/etc, which can also reduce resource consumption.
* `DOALARMS` true or false, to or not to transfer alarms/reminders to the Palm. The Palm's alarm settings are not very granular so the option to disable them is provided to avoid being woken up at 3 AM.

If your Palm has been recently been reset, a HotSync may not work until the Datebook has been initialised by creating an event yourself on the Palm.

Without options `calendar-sync2` will run according to the `datebook.cfg` configuration file. If no configuration file is found or run as `calendar-sync2 -h` a help message with a list of command line arguments will be displayed.

```
palm-calendar-sync2/build $ ./sync-calendar2 -h
    sync-calendar2 (debug build)

    ==> Reading arguments <==
    Argument -h

    sync-calendar2 (debug build) a tool for copying ical calendars to Palm
    Usage: sync-calendar2 [options]

    Options:

        -c  Specify config file (default datebook.cfg)
        -h  Print this help message and quit
        -p  Override config file port (e.g., /dev/ttyS0, net:any, usb:)
        -u  Override calendar URI (can be used multiple times)
```

While running, `calendar-sync2` will produce output to verify that it is reading events correctly and provide information on the HotSync progress.


## Compiling

Dependencies:

* pilot-link/libpisock
* libconfig
* libcurl
* libical
* libusb/libusb-compat
* gcc and cmake for compiling
* git (if compiling release build)

Unfortunately not all distributions distribute pilot-link any more. [Gentoo](https://packages.gentoo.org/packages/app-pda/pilot-link) and [Fedora](https://packages.fedoraproject.org/pkgs/pilot-link/pilot-link/) do, while others such as OpenSuse no longer include pilot-link with their latest versions. [Pilot Link](https://www.jpilot.org/download/) provides their own builds of pilot-link and libpisock for Debian and Ubuntu. (Thanks to @clintonthegeek for spotting this!) If a pilot-link package is not available for your distribution, unfortunately you can't compile from the original sources any longer due to changes in gcc, etc. However, there are a few sets of patched sources floating around:

* For users of Arch Linux, there is an [pilot-link AUR](https://aur.archlinux.org/packages/pilot-link) that can be used to compile, although additional modifications might be necessary.
* Gentoo provides a [patch set that lets you build from the original sources](https://github.com/jichu4n/pilot-link/issues/3).
* There is [more recently maintained fork](https://github.com/desrod/pilot-link).

After installing the dependencies (which may include dependency -dev/-devel packages on some distributions), then building `calendar-sync2` is straight forward.

In words:

1. Download this repository (e.g., using `git clone` or the GitHub "Download ZIP" function).
1. In the `palm-calendar-sync2` directory, create a `build` directory.
1. Inside `build` directory, initialise the make system using the `cmake -DCMAKE_BUILD_TYPE=Release ..` command.
1. Compile using `make`.

In commands:

```
wget https://github.com/guruthree/palm-calendar-sync2/archive/refs/heads/main.zip
unzip palm-calendar-sync2-main.zip
cd palm-calendar-sync2-main
mkdir build
cd build
cmake -DCMAKE_BUILD_TYPE=Release ..
make
```

These steps will produce the `calendar-sync2` binary, which can be added to your bin path of choice.

If you do not wish to compile the release build, do not add `-DCMAKE_BUILD_TYPE=Release` and instead just run the `cmake ..` command.
This removes the git revision, host, and datetime information compiled into the binary and should also remove the dependency on git.
