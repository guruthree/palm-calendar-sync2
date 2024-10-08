/*
 *
 * Copyright (C) 2023 guruthree
 *
 * This program is free software; you can redistribute it and/or
 * modify it under the terms of the GNU General Public License
 * as published by the Free Software Foundation; either version 2
 * of the License, or (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program; if not, write to the Free Software
 * Foundation, Inc., 51 Franklin Street, Fifth Floor, Boston, MA  02110-1301, USA.
 *
 */

// sync-calendar attempt to read ical file and write to palm pilot
// if we're really good also try to not repeat any event that already exists
// note not fully ical complient, but should work with google calendar exports

#include <cstring>
#include <filesystem>
#include <iostream>
#include <string>
#include <vector>

#include <libconfig.h++>
#include <curl/curl.h>
#include <libical/ical.h>

#include <libpisock/pi-datebook.h>
#include <libpisock/pi-dlp.h>
#include <libpisock/pi-socket.h>
#include <libpisock/pi-source.h>
#include <libpisock/pi-usb.h>

#include "libusb.h"
#include "sync-calendar2.h"

// add log4cplus for logging?

void helpmessage() {
#ifdef SYNCVERSION
    std::cout << "    sync-calendar2 (" << SYNCVERSION << ") a tool for copying ical calendars to Palm" << std::endl;
#else
    std::cout << "    sync-calendar2, a tool for copying ical calendars to Palm" << std::endl << std::endl;
#endif
    std::cout << "    Usage: sync-calendar2 [options]" << std::endl << std::endl;
    std::cout << "    Options:" << std::endl << std::endl;
    std::cout << "        -c  Specify config file (default " << DEFAULT_CONFIG_FILE << ")" << std::endl;
    std::cout << "        -h  Print this help message and quit" << std::endl;
    std::cout << "        -p  Override config file port (e.g., /dev/ttyS0, net:any, usb:)" << std::endl;
    std::cout << "        -u  Override calendar URI (can be used multiple times)" << std::endl;
    std::cout << std::endl;
}

int main(int argc, char **argv) {

    // configuration settings & defaults
    std::string configfile(DEFAULT_CONFIG_FILE);
    std::vector<std::string> alluris;
    std::string port, timezone("UTC");
    int fromyear = 0, previousdays = 0;
    bool dohotsync = true, readonly = false, doalarms = false, skipnotes = false, overwrite = true, onlynew = false, secure = false;
    bool portoverride = false, urioverride = false; // command line argument overrides config file argument

    // use to keep track if something happened or not (often for exiting on an error)
    bool failed = true;

    // if the default configfile doesn't exist and there aren't any arguments, help the user out
    if (argc == 1 && !std::filesystem::exists(DEFAULT_CONFIG_FILE)) {
        helpmessage();
        return EXIT_SUCCESS;
    }
#ifdef SYNCVERSION
    std::cout << "    sync-calendar2 (" << SYNCVERSION << ")" << std::endl << std::endl;
#endif

    // the time now for calculating events age in days
    time_t today = time(NULL);

    /** read in command line arguments **/

    std::cout << "    ==> Reading arguments <==" << std::endl << std::flush;

    // based on https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
    for (int c; (c = getopt(argc, argv, "hp:u:c:")) != -1; ) { // man 3 getopt
        switch (c) {
            case 'h': // port
                std::cout << "    Argument -h" << std::endl;
                std::cout << std::endl;
                helpmessage();
                return EXIT_SUCCESS;

            case 'u': // uri
                failed = false;
                alluris.push_back(optarg);
                std::cout << "    Argument -u: " << optarg << std::endl;
                urioverride = true;
                break;

            case 'p': // port
                failed = false;
                port = optarg;
                std::cout << "    Argument -p: " << port << std::endl;
                portoverride = true;
                break;

            case 'c': // config file
                failed = false;
                configfile = optarg;
                std::cout << "    Argument -c: " << configfile << std::endl;
                break;

            case '?':
                return EXIT_FAILURE;

            default:
                break;
        }
    }

    if (failed) {
        std::cout << "    No arguments" << std::endl << std::flush;
    }

    std::cout << std::endl << std::flush;


    /** read in configuration settings using libconfig **/

    std::cout << "    ==> Reading configuration <==" << std::endl << std::flush;

    libconfig::Config cfg;
    cfg.setOptions(libconfig::Config::OptionAutoConvert); // float to int and viceversa?
    try {
        std::cout << "    Reading from " << configfile << std::endl;
        cfg.readFile(configfile);
    }
    catch (const libconfig::FileIOException &fioex) {
        std::cerr << "    ERROR while reading" << std::endl;
        return EXIT_FAILURE;
    }
    catch (const libconfig::ParseException &pex) {
        std::cerr << "    ERROR parsing at " << pex.getFile() << ":" << pex.getLine() << " - " << pex.getError() << std::endl;
    }

    // reading the URI info out of the config is tricky because it may or may not be a list
    // also ignore if specified on the command line
    // there has got to be a nicer way to do this
    if (!urioverride) {
        failed = false;
        if (cfg.exists("URI")) {
            libconfig::Setting& urisettings = cfg.lookup("URI");
            if (urisettings.isList()) {
                for (libconfig::Setting const& urisetting : urisettings) {
                    if (urisetting.getType() == libconfig::Setting::TypeString) {
                        alluris.push_back(std::string(urisetting));
                    }
                }
            }
            else if (urisettings.getType() == libconfig::Setting::TypeString){
                alluris.push_back(std::string(urisettings));
            }
            else {
                failed = true;
            }
        }
        else {
            failed = true;
        }
        if (failed) {
            std::cerr << "    ERROR with URI setting in configuration file, failing." << std::endl;
            return EXIT_FAILURE;
        }
        else {
            for (std::string uri : alluris) {
                std::cout << "    Config URI: " << uri << std::endl;
            }
        }
    }
    // use macros to tidy up reading config options
    // first in caps config item (will be a string), second variable name
    if (!portoverride) {
        FAIL_CFG(PORT, port)
    }
    NON_FAIL_CFG(DOHOTSYNC, dohotsync)
    NON_FAIL_CFG(READONLY, readonly)
    NON_FAIL_CFG(TIMEZONE, timezone)
    NON_FAIL_CFG(FROMYEAR, fromyear)
    NON_FAIL_CFG(PREVIOUSDAYS, previousdays)
    NON_FAIL_CFG(SKIPNOTES, skipnotes)
    NON_FAIL_CFG(OVERWRITE, overwrite)
    NON_FAIL_CFG(ONLYNEW, onlynew)
    NON_FAIL_CFG(DOALARMS, doalarms)
    NON_FAIL_CFG(SECURE, secure)
    std::cout << std::endl << std::flush;


    /** palm pilot communication part 1 **/

    int sd = -1; // socket descriptor (like an fid)
    PilotUser User;

    if (dohotsync) {

        std::cout << "    ==> Connecting to Palm <==" << std::endl << std::flush;

        // a lot of this comes from pilot-link userland.c, pilot-install-datebook.c, or pilot-read-ical.c

        if ((sd = pi_socket(PI_AF_PILOT, PI_SOCK_STREAM, PI_PF_DLP)) < 0) {
            std::cerr << "    ERROR unable to create socket '" << port << "'" << std::endl;
            return EXIT_FAILURE;
        }

        if (pi_bind(sd, port.c_str()) < 0) {
            std::cerr << "    ERROR unable to bind to port: " << port << std::endl;
            return EXIT_FAILURE;
        }

        std::cout << "    Listening for incoming connection on " << port << "... " << std::flush;

        if (pi_listen(sd, 1) < 0) {
            std::cerr << std::endl << "    ERROR listening on " << port << std::endl;
            pi_close_fixed(sd, port);
            return EXIT_FAILURE;
        }

        sd = pi_accept_to(sd, 0, 0, 0); // last argument is a timeout in seconds - 0 is wait forever?
        if (sd < 0) {
            std::cerr << "    ERROR accepting data on " << port << std::endl;
            pi_close_fixed(sd, port);
            return EXIT_FAILURE;
        }

        std::cout << "connected!" << std::endl << std::endl << std::flush;

        SysInfo sys_info;
        if (dlp_ReadSysInfo(sd, &sys_info) < 0) {
            std::cerr << "    ERROR reading system info on " << port << std::endl;
            pi_close_fixed(sd, port);
            return EXIT_FAILURE;
        }

        dlp_ReadUserInfo(sd, &User);

        // tell the palm we're going to be communicating
        if (dlp_OpenConduit(sd) < 0) {
            std::cerr << "    ERROR opening conduit with Palm" << std::endl;
            pi_close_fixed(sd, port);
            return EXIT_FAILURE;
        }

    }


    /** read in calendar data using libcurl **/

    // store all of the calendar events packed ready for copying to the palm
    std::vector<Appointment> Appointments;
    std::vector<std::string> uids; // for detecting & merging duplicate ical entries
    std::vector<bool> docopy; // actually copy to the palm?
    for (std::string uri : alluris) {

        std::cout << "    ==> Downloading calendar <==" << std::endl << std::flush;

        // the downloaded ical data
        std::string icaldata;

        failed = false;
        CURL *curl;
        CURLcode res;
        curl_global_init(CURL_GLOBAL_DEFAULT);
        curl = curl_easy_init();
        if(curl) {
            std::cout << "    Fetching " << uri << std::endl;

            curl_easy_setopt(curl, CURLOPT_URL, uri.c_str());

            // disable some SSL checks, reduced security
            if (!secure) {
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
                curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
            }

            // cache the CA cert bundle in memory for a week
            curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);

            // tell curl to write to a std::string
            curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
            curl_easy_setopt(curl, CURLOPT_WRITEDATA, &icaldata);
            curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, true);

            // perform the request, res will get the return code
            res = curl_easy_perform(curl);

            // check for errors
            if (res != CURLE_OK) {
                std::cerr << "    ERROR curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
                failed = true;
            }
            else {
                long http_code = 0;
                curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &http_code);
                char *scheme;
                curl_easy_getinfo(curl, CURLINFO_SCHEME, &scheme);
                if (http_code != 200 && !(strcmp(scheme, "FILE") == 0)) {
                    std::cerr << "    ERROR fetching URI, http response code " << http_code << std::endl;
                    failed = true;
                }
            }

            // always cleanup!
            curl_easy_cleanup(curl);
        }
        else {
            std::cerr << "    ERROR initialising curl" << std::endl;
        }

        curl_global_cleanup();
     
        if (failed) {
            // something went wrong along the way, exit
            std::cerr << "    Exiting after curl error" << std::endl << std::endl;
            if (dohotsync) {
                pi_close_fixed(sd, port);
            }
            return EXIT_FAILURE;
        }
        std::cout << "    Calendar downloaded successfully" << std::endl << std::endl << std::flush;


        /** parse calendar using libical **/

        std::cout << "    ==> Parsing calendar <==" << std::endl << std::flush;

        // ical specifies components, properties, values, and parameters and I think there's
        // a bit of a metaphor relating that to HTML, with COMPONENTS corresponding to an
        // opening BEGIN tag which contains PROPERTY tags that then have VALUES (the main
        // thing in that tag, and then PARAMETERS which are kind of like HTML attributes,
        // adding extra information about the PARAMETER

        // https://github.com/libical/libical/blob/master/doc/UsingLibical.md
        // https://libical.github.io/libical/apidocs/icalcomponent_8h.html
        // https://libical.github.io/libical/apidocs/icalproperty_8h.html
        // https://libical.github.io/libical/apidocs/icalparameter_8h.html
        // https://libical.github.io/libical/apidocs/icaltime_8h.html
        // https://libical.github.io/libical/apidocs/structicaltimetype.html
        
        // parse the string into a series of components to iterate through
        icalcomponent* components = icalparser_parse_string(icaldata.c_str());

        // remove errors (which also includes empty descriptions, locations, and the like) 
        icalcomponent_strip_errors(components);

        // if there is some valid ical data to work through
        if (components != nullptr) {

            std::cout << "    Calendar parsed successfully" << std::endl << std::endl << std::flush;

            // the specific component we're on in our iteration
            icalcomponent *c;

            // we're only interested in calendar events, iterate through them
            for(c = icalcomponent_get_first_component(components, ICAL_VEVENT_COMPONENT); c != 0;
                    c = icalcomponent_get_next_component(components, ICAL_VEVENT_COMPONENT)) {

                // palm only has start time, end time, alarm, repeat, description, and note
                // so we only need to extract those things from the component if they're there
                // every event should have a DTSTART, DTEND, and DTSUMMARY (which is palm description)
                // palm won't take an event with a description

                std::cout << "    ==> Processing event <==" << std::endl;
                failed = false;

                /* create Appointment if it doesn't already exist */

                // use event UID to detect duplicates & merge (Google seems to split things out sometimes?)
                icalproperty *uidp = icalcomponent_get_first_property(c, ICAL_UID_PROPERTY);

                bool isarecurrence = icalcomponent_count_properties(c, ICAL_RECURRENCEID_PROPERTY) > 0;

                std::string uid("");
                if (uidp != nullptr) {
                    uid = icalproperty_get_uid(uidp);
                    if (!isarecurrence) {
                        std::cout << "    UID: " << uid << std::endl;
                    }
                    else {
                        std::cout << "    Recurrence of UID: " << uid << std::endl;
                    }
                }


                int uidmatched = -1;
                for (int i = 0; i < uids.size(); i++) {
                    if (uids[i] == uid && uid != "") {
                        std::cout << "        Previous UID match" << std::endl;
                        uidmatched = i;
                        break;
                    }
                }

                // store information about this event in the pilot-link Appointment struct
                // see pi-datebook.h for details of format
                Appointment appointment;

                if (uidmatched != -1 && !isarecurrence) {
                    // if this uid exists twice, assume the later one in the file is newer and overwrite any properties specified
                    // it can only be allowed to match a previous UID if there's not a RECURRENCE-ID
                    appointment = Appointments[uidmatched];
                }


                /* date time description essentials */

                // convert dates and times to tm for transfer to palm
                icaltimetype start = icalcomponent_get_dtstart(c);
                time_t start_time_t = icaltime_as_timet_with_zone(start, icaltime_get_timezone(start));
                appointment.begin = *gmtime(&start_time_t); // dereference to avoid subsequent calls overwriting
                timegm(&appointment.begin); // convert localtime to UTC
                std::cout << "    Start UTC: " << asctime(&appointment.begin);

                icaltimetype end = icalcomponent_get_dtend(c);
                time_t end_time_t = icaltime_as_timet_with_zone(end, icaltime_get_timezone(end));
                appointment.end = *gmtime(&end_time_t);
                timegm(&appointment.end);
                std::cout << "    End UTC: " << asctime(&appointment.end);

                // if it's just a date in ical the appointment is an all day event
                if (icaltime_is_date(start) && icaltime_is_date(end))
                    appointment.event          = 1;
                else
                    appointment.event          = 0;

                // there is some tedious conversion from char to const char to string and around things
                // (probably a side effect of mixing C and C++)

                // get the summary
                std::string summary("");
                const char* summary_c = icalcomponent_get_summary(c);
                if (summary_c != nullptr) {
                    summary = summary_c;
                }

                // get the description / note
                std::string description("");
                const char* description_c = icalcomponent_get_description(c);
                if (description_c != nullptr) {
                    description = description_c;
                }

                // if there's no summary and a 1 line description, use the description as a summary instead
                // (mostly for the ical recur checks really)
                if (summary.length() == 0 && description.length() > 0 && description.find("\n") == std::string::npos) {
                    summary = description;
                    description = "";
                }

                if (summary.length() > 0) {
                    std::cout << "    Summary: " << summary << std::endl;

                    // an annoying round about route from const char* to std::string to char*
                    // https://stackoverflow.com/questions/7352099/stdstring-to-char
                    char *summary_c2 = new char[summary.length()+1];
                    strcpy(summary_c2, summary.c_str());
                    appointment.description        = summary_c2;
                }
                else if (uidmatched == -1 || isarecurrence) { // only apply default if it's not part of merging another appointment
                    appointment.description        = nullptr;
                }

                // get the location (palmos5 has a location but pilot-link doesn't support it - different database format?)
                std::string location("");
                const char* location_c = icalcomponent_get_location(c);
                if (location_c != nullptr) {
                    location = location_c;
                }

                // merge location and description into the note
                std::string note;
                if (location.length() > 0) {
                    note = note + "Location:\n" + location;
                }

                // add attendees list to the note
                int numattendees = icalcomponent_count_properties(c, ICAL_ATTENDEE_PROPERTY);
                if (numattendees > 0) {
                    
                    std::cout << "    " << numattendees << " attendees" << std::endl;

                    if (note.length() > 0) {
                        note = note + "\n\n";
                    }
                    note = note + "Attendees:";

                    for(icalproperty *attendeep = icalcomponent_get_first_property(c, ICAL_ATTENDEE_PROPERTY); attendeep != 0;
                            attendeep = icalcomponent_get_next_property(c, ICAL_ATTENDEE_PROPERTY)) {

                        std::string attendee = icalproperty_get_attendee(attendeep);

                        icalparameter *cnp2 = icalproperty_get_first_parameter(attendeep, ICAL_CN_PARAMETER);
                        std::string attendee_cn("");
                        if (cnp2 != nullptr) {
                            attendee_cn = icalparameter_get_iana_value(cnp2);
                        }

                        if (attendee_cn != "") {
//                            std::cout << "CN: " << attendee_cn << std::endl;
                            note = note + "\n" + attendee_cn;
                        }
                        else {
                            // if there's no CN (common name) fall back to the base value which is usually an e-mail address
                            int k = attendee.find("mailto:");
                            if (k != std::string::npos) {
                                // strip off the mailto
                                attendee = attendee.substr(7);
                            }
                            note = note + "\n" + attendee_cn;
                        }

                    }

                } // numattendees

                // add the description to the note if present
                if (description.length() > 0) {
                    if (note.length() > 0) {
                        note = note + "\n\n";
                    }
                    note = note + description;
                }

                if (note.length() > 0 && !skipnotes) { // note might be empty

                    std::cout << "    Note:\n" << note << std::endl;

                    char *note_c2 = new char[note.length()+1];
                    strcpy(note_c2, note.c_str());
                    appointment.note               = note_c2;
                }
                else if (uidmatched == -1 || isarecurrence) { // only apply when not merging
                    appointment.note               = nullptr;
                }


                /* what about an alarm */

                // palm os only supports one alarm, so find the nearest one and add that
                int shortest_alarm = 9999989; // default value to c.f. if an alarm has been set
                int numalarms = icalcomponent_count_components(c, ICAL_VALARM_COMPONENT);
                if (numalarms > 0 && doalarms) {

                    // a VALARM is a sub-sub-component
                    icalcomponent *c2;

                    for(c2 = icalcomponent_get_first_component(c, ICAL_VALARM_COMPONENT); c2 != 0;
                            c2 = icalcomponent_get_next_component(c, ICAL_VALARM_COMPONENT)) {

                        // this is ignoring absolute alarms with TRIGGER;VALUE=DATE-TIME
                        // also assuming there's only ever one TRIGGER property
                        icaltriggertype trigger = \
                            icalproperty_get_trigger(icalcomponent_get_first_property(c2, ICAL_TRIGGER_PROPERTY));

                        if (trigger.duration.is_neg == 1) { // alarms only occur beforehand

                            // how far in advance is the alarm? we only work in minutes
                            int advance = (trigger.duration.weeks * 7 * 86400 + \
                                           trigger.duration.days * 86400 + \
                                           trigger.duration.hours * 3600 + \
                                           trigger.duration.minutes * 60 + 
                                           trigger.duration.seconds) / 60;
                            if (advance < shortest_alarm) {
                                shortest_alarm = advance;
                            }
                        }
                    } // c2

                    if (shortest_alarm != 9999989) {
                        std::cout << "    Alarm: " << shortest_alarm << " minutes before" << std::endl;
                        appointment.alarm              = 1;
                        appointment.advance            = shortest_alarm;
                        appointment.advanceUnits       = advMinutes;
                    }

                } // numalarms
                else if (uidmatched == -1 || isarecurrence) {
                    // more initialisation
                    appointment.alarm              = 0;
                    // we can't store the alarm if it's not enabled so just dummy values here
                    appointment.advance            = 0;
                    appointment.advanceUnits       = advMinutes;
                }


                /* repat stuff, repeat stuff */

                // RRULE repeating sets
                // EXDATE dates in the set skipped, stored as a tm struct with year/month/day only
                // RDATE one off of repeating events that were moved, they appear as a normal event so ignore?
                // RECURRENCE-ID similar except they have the same UID so shouldn't be merged

                // https://libical.github.io/libical/apidocs/icalrecur_8h.html
                // https://libical.github.io/libical/apidocs/structicalrecurrencetype.html
                // https://freetools.textmagic.com/rrule-generator
                // https://icalendar.org/validator.html
                // https://icalendar.org/iCalendar-RFC-5545/3-3-10-recurrence-rule.html

                // ical to pilot-link mapping
                // X INTERVAL => repeatFrequency
                // X UNTIL => repeatEnd, repeatForever
                // X COUNT => repeatEnd
                // X WKST => repeatWeekstart
                // X BYMONTHDAY => repeatMonthlyByDay
                // X BYDAY => repeatDays
                // X FREQ => repeatType, repeatDay (montly), repeatDays (weekly)
                // X EXDATE => exception, exceptions

                // for final repetition checks, need to test COUNT, EXDATE, RECURRENCE-ID thoroughly
                //     for each daily, weekly, monthly, yearly
                //     repeat forever, repeat until, repeat count
                //     exclude or move three
                // can also check against recur.txt from libical test-data

                // declare some saine defaults to start with
                if (uidmatched == -1 || isarecurrence) { // will already have been set if it's uidmatched
                    appointment.repeatType         = repeatNone;
                    appointment.repeatForever      = 0;
                    appointment.repeatEnd.tm_year  = 0;
                    appointment.repeatEnd.tm_mon   = 0;
                    appointment.repeatEnd.tm_mday  = 0;
                    appointment.repeatEnd.tm_wday  = 0;
                    appointment.repeatFrequency    = 0;
                    appointment.repeatWeekstart    = 1; // 0-6 Sunday to Saturday, ical default is Monday so 1
                    for (int i = 0; i < 7; i++) appointment.repeatDays[i] = 0;
//                    appointment.repeatDay          = 0;
                    appointment.exceptions         = 0;
                    appointment.exception          = nullptr; // this is an array, yikes
                }

                // this assumes there's only ever one RRULE property (palm can only support one anyway)
                icalproperty *rrule = icalcomponent_get_first_property(c, ICAL_RRULE_PROPERTY);
                if (rrule != nullptr) {

                    icalrecurrencetype recur = icalproperty_get_rrule(rrule);
                    std::cout << "    Recurrence: " << icalrecurrencetype_as_string(&recur) << std::endl;

                    // we're assuming UNTIL and COUNT are mutually exclusive
                    if (recur.until.year != 0) {

                        // there's an until date, use that
                        time_t until_time_t = icaltime_as_timet_with_zone(recur.until, icaltime_get_timezone(recur.until));
                        appointment.repeatEnd = *gmtime(&until_time_t); // dereference to avoid subsequent calls overwriting
                        timegm(&appointment.repeatEnd); // needed?
                        appointment.repeatEnd.tm_hour = 23; // as below
                        appointment.repeatEnd.tm_min = 59;
                        appointment.repeatEnd.tm_sec = 59;
                    }
                    else if (recur.count == 0) {

                        // no until date, for the moment assume repeating forever
                        appointment.repeatForever = 1;
                    }
                    else {

                        //  we'll have to figure out what repeatEnd should be based on count, but this depends on frequency...
                        appointment.repeatEnd = appointment.begin; // this should already be UTC
                        // we add count * freq, but palm os ends on the day specified
                        // end last moment of the day before (we'll have to subtract that day for them later...)
                        appointment.repeatEnd.tm_hour = 23;
                        appointment.repeatEnd.tm_min = 59;
                        appointment.repeatEnd.tm_sec = 59;
                    }

                    appointment.repeatFrequency = recur.interval < 1 ? 1 : recur.interval; // 1 or INTERVAL

                    // palm looks a bit different than libical here with the 1 being monday as opposed to 2 (ICAL_MONDAY_WEEKDAY)
                    appointment.repeatWeekstart = weekday2int(recur.week_start);

                    // palm doesn't support anything less than daily
                    UNSUPPORTED_ICAL(by_second, BYSECOND)
                    UNSUPPORTED_ICAL(by_minute, BYMINUTE)
                    UNSUPPORTED_ICAL(by_hour, BYHOUR)

                    icalrecurrencetype_frequency freq = recur.freq;
                    if (freq == ICAL_NO_RECURRENCE || 
                            freq == ICAL_SECONDLY_RECURRENCE || 
                            freq == ICAL_MINUTELY_RECURRENCE || 
                            freq == ICAL_HOURLY_RECURRENCE) {

                        // palm doesn't support anything less than daily, so no repeating
                        appointment.repeatType = repeatNone;
                        failed = true;
                        std::cout << "        WARNING unsupported frequency, won't copy!" << std::endl;
                    }
                    else if (freq == ICAL_DAILY_RECURRENCE) {

                        std::cout << "    Repeating daily" << std::endl;
                        appointment.repeatType = repeatDaily;

                        UNSUPPORTED_ICAL(by_month, BYMONTH)

                        if (!appointment.repeatForever && recur.count != 0) {
                            appointment.repeatEnd.tm_mday += recur.count - 1;
                            timegm(&appointment.repeatEnd);
                        }
                    }
                    else if (freq == ICAL_WEEKLY_RECURRENCE) {

                        std::cout << "    Repeating weekly" << std::endl;
                        appointment.repeatType = repeatWeekly; // repeatDays from BYDAY

                        // need to loop as there might be more than one day..?
                        for (int i = 0; recur.by_day[i] != ICAL_RECURRENCE_ARRAY_MAX; i++) {
                            int day = weekday2int(icalrecurrencetype_day_day_of_week(recur.by_day[i]));
                            appointment.repeatDays[day] = 1;
                            std::cout << "        Repeating day " << day << std::endl;
                        }

                        // check to make sure some repeat days are selected, if not, repeat on all days
                        int numrepeatdays = 0;
                        for (int i = 0; i < 7; i++) {
                            numrepeatdays += appointment.repeatDays[i];
                        }
                        if (numrepeatdays == 0) {
                            std::cout << "        Repeating all days (assumed)" << std::endl;
                            for (int i = 0; i < 7; i++) {
                                appointment.repeatDays[i] = 1;
                            }
//                            recur.count = recur.count * 7; // recur.count is otherwise days?
                        }

                        // convert repeat count to repeat end date
                        if (!appointment.repeatForever && recur.count != 0) {
                            // the logic here is tricky! in effect we need to step forwards from the start date
                            // counting days it happens ignoring says not selected to work out end date

                            // break out when repeats exceeds the recurrence count
                            int atday = 0; // count how many days that takes
                            for (int i = appointment.repeatEnd.tm_wday, repeats = 0; repeats < recur.count; i++, atday++) {

                                // only count days when the event occurs towards repeats
                                if (appointment.repeatDays[i]) {
                                    repeats++;
                                }
                                
                                // for looping through days of the week
                                if (i == 6) {
                                    i = -1; // at the end of the loop ++ brings it back to 0?
                                }
                            }

                            appointment.repeatEnd.tm_mday += atday;
                            appointment.repeatEnd.tm_mday--;
                            timegm(&appointment.repeatEnd);
                        }
                    }
                    else if (freq == ICAL_MONTHLY_RECURRENCE) {

                        std::cout << "    Repeating montly" << std::endl;
                        // events usually only repeat on one day of the month, so just check the 0th index
                        if (recur.by_month_day[0] != ICAL_RECURRENCE_ARRAY_MAX) { // BYMONTHDAY

                            // nothing extra to set, palm will just assume it's the date of the start
                            appointment.repeatType = repeatMonthlyByDate;
                            // day of the month in by day repeat - this is done in pilot-datebook, but doesn't seem needed based on pi-datebook.h
                            appointment.repeatDay = (DayOfMonthType)recur.by_month_day[0]; 
                            std::cout << "        Repeating on " << appointment.begin.tm_mday << std::endl;

                            UNSUPPORTED_ICAL1(by_month_day, BYMONTHDAY)
                        }
                        else { // BYDAY   
               
                            // should only ever be the first day as monthly things can't occur more than once a month
                            if (recur.by_day[0] != ICAL_RECURRENCE_ARRAY_MAX) {
                                int week = icalrecurrencetype_day_position(recur.by_day[0]);
                                if (week <= 0) {
                                    // all days of the month (0) or counting backwards from end of month
                                    // can't use macro here because some BYDAY are supported
                                    failed = true;
                                    std::cout << "        WARNING unsupported BYDAY, won't copy!" << std::endl;
                                }
                                else {
                                    int day = weekday2int(icalrecurrencetype_day_day_of_week(recur.by_day[0]));

                                    // not ideal, but I don't expect the DayOfMonthType enum to change anytime soon
                                    appointment.repeatDay = (DayOfMonthType)((week - 1)*7 + day);

                                    std::cout << "        Repeating the " << day << " of week " << week <<
                                        " (enum " << appointment.repeatDay << " " << DayOfMonthString[appointment.repeatDay] << ")" << std::endl;

                                    appointment.repeatType = repeatMonthlyByDay;
                                }
                            }
                            else {
                                std::cout << "        WARNING unexpected repeat???" << std::endl;
                            }
                        }

                        if (!appointment.repeatForever && recur.count != 0) {
                            appointment.repeatEnd.tm_mon += recur.count;
                            appointment.repeatEnd.tm_mday--;
                            timegm(&appointment.repeatEnd);
                        }
                    }
                    else if (freq == ICAL_YEARLY_RECURRENCE) {
                        std::cout << "    Repeating yearly" << std::endl;
                        appointment.repeatType = repeatYearly;

                        UNSUPPORTED_ICAL(by_day, BYDAY)
                        UNSUPPORTED_ICAL(by_month, BYMONTH)
                        UNSUPPORTED_ICAL(by_year_day, BYYEARDAY)  
                        UNSUPPORTED_ICAL(by_week_no, BYWEEKNO)                    

                        if (!appointment.repeatForever && recur.count != 0) {
                            appointment.repeatEnd.tm_year += recur.count;
                            appointment.repeatEnd.tm_mday--;
                            timegm(&appointment.repeatEnd);
                        }
                    }
                    else {
                        std::cout << "    Unknown repeat frequency" << std::endl;
                    }
                    if (!appointment.repeatForever) {
                        std::cout << "        Until " << asctime(&appointment.repeatEnd);
                    }


                    /* argh exceptions */

                    appointment.exceptions = icalcomponent_count_properties(c, ICAL_EXDATE_PROPERTY);
                    if (appointment.exceptions != 0) {
                        std::cout << "    There are " << appointment.exceptions << " exceptions" << std::endl;
                    }

                    appointment.exception = (tm*)malloc(appointment.exceptions * sizeof(tm)); // this should be freed by free_Appointment later

                    int exceptionat = 0;
                    for(icalproperty *exdatep = icalcomponent_get_first_property(c, ICAL_EXDATE_PROPERTY); exdatep != 0;
                            exdatep = icalcomponent_get_next_property(c, ICAL_EXDATE_PROPERTY), exceptionat++) {

                        icaltimetype exdate = icalproperty_get_exdate(exdatep);
                        time_t exdate_time_t = icaltime_as_timet_with_zone(exdate, icaltime_get_timezone(exdate));

                        appointment.exception[exceptionat] = *gmtime(&exdate_time_t); // dereference to avoid subsequent calls overwriting
                        timegm(&appointment.exception[exceptionat]); // localtime to UTC
                        std::cout << "        Excluding " << asctime(&appointment.exception[exceptionat]);
                    } // for exdatep
                } // rrule

                if (isarecurrence && uidmatched != -1) { 
                    // some things to do if an event is a recurrence

                    // recurrence events are moved events from a repeating set, but ical doesn't add an exdate for them
                    // we don't want the moved event to appear so exclude it from the parent event based on UID
                    // i.e., if the event is a recurrence attached to another event, that other event needs an exclusion

                    // new array at new size - argh, array resizing
                    Appointments[uidmatched].exceptions++;
                    tm *newexception = (tm*)malloc(Appointments[uidmatched].exceptions * sizeof(tm));

                    // copy over existing exceptions into new array
                    for (int i = 0; i < Appointments[uidmatched].exceptions-1; i++) {
                        newexception[i] = Appointments[uidmatched].exception[i];
                    }
                    newexception[Appointments[uidmatched].exceptions-1] = appointment.begin;

                    // cuckoo time
                    free(Appointments[uidmatched].exception); // clear out old egg
                    Appointments[uidmatched].exception = newexception; // put new egg in nest

                    // we could copy parent note/summary if there is one and the appointment doesn't have its own?
                }


                /* phew, done with this event */

                // store the Appointment, either overwriting itself in the list of appointments or adding a new one
                if (uidmatched != -1 && !isarecurrence) {
                    Appointments[uidmatched] = appointment;
                    std::cout << "    Merging" << std::endl;
                }
                else {
                    Appointments.push_back(appointment);
                    if (!isarecurrence) {
                        uids.push_back(uid);
                    }
                    else {
                        // don't store the uid of a recurrence so that all exclusions get added to the correct one
                        uids.push_back("");
                        std::cout << "    Not storing UID" << std::endl;
                    }


                    // skip if it's older than FROMYEAR and not repeating until after FROMYEAR
                    if ((appointment.begin.tm_year + 1900) < fromyear && 
                            !appointment.repeatForever && 
                            !((appointment.repeatEnd.tm_year + 1900) >= fromyear)) {
                        failed = true; // gets put in docopy
                        std::cout << "    Earlier than " << fromyear << ", ignoring" << std::endl;
                    }

                    // skip if it's older than PREVIOUSDAYS and not repeating until now
                    if ((today - timelocal(&appointment.begin)) / 86400.0 > previousdays && 
                            !appointment.repeatForever &&
                            // the end date of the repeat is more than previous days in the past
                            (today - timelocal(&appointment.repeatEnd)) / 86400.0 > previousdays ) {
                        failed = true; // gets put in docopy
                        std::cout << "    Older than " << previousdays << " days, ignoring" << std::endl;
                    }

                    docopy.push_back(!failed);
                    if (docopy.back()) { // should get last element
                        std::cout << "    Stored for sync" << std::endl << std::endl;
                    }
                    else {
                        std::cout << "    WARNING won't sync" << std::endl << std::endl;
                    }
                }

            } // for c
//            if (c != nullptr) icalcomponent_free(c);
            icalcomponent_free(components); // already not null by definition inside this if statement
        } // if components

    } // for alluris


    /* adjust time zone to specified timezone */

    if (timezone != "UTC") {

        std::cout << "    ==> Timezone Conversion <==" << std::endl << std::flush;

        // mktime apparently reads off the TZ environment variable
        // per https://rl.se/convert-utc-to-local-time
        char *tz = getenv("TZ");
        timezone = "TZ=" + timezone;
        putenv((char*)timezone.c_str());

        for (int i = 0; i < Appointments.size(); i++) {

            // no timezone adjustment needed for all day events
            if (Appointments[i].event) {
                continue;
            }

            time_t stamp;  

            // timegm takes the tm struct and ignores TZ converting to time_t (assuming UTC, which it is)
            // localtime take time_t and converts to a tm struct taking TZ into account
            stamp = timegm(&Appointments[i].begin);
            Appointments[i].begin = *localtime(&stamp);

            stamp = timegm(&Appointments[i].end);
            Appointments[i].end = *localtime(&stamp);

            // std::cout << "    Summary: " << Appointments[i].description << std::endl;
            // std::cout << "    Start " << Appointments[i].begin.tm_zone << ": " << asctime(&Appointments[i].begin);
            // std::cout << "    End " << Appointments[i].end.tm_zone << ": " << asctime(&Appointments[i].end) << std::endl;

            // repeat end? don't need to as it always is 23:59:59 on the day
            // loop exceptions? don't need to because also only a day
        }

        //put env back the way it was?
        if (tz != nullptr) {
            timezone = "TZ=" + std::string(tz);
            putenv((char*)timezone.c_str());
        }
        else {
            putenv((char*)"TZ=");
        }

        std::cout << "    Converted to " << timezone.substr(3) << std::endl << std::endl;
    }


    /** palm pilot communication part 2 **/

    if (!dohotsync) {
        return EXIT_SUCCESS;
    }

    std::cout << "    ==> Downloading to Palm <==" << std::endl << std::flush;

    // open the datebook and store a handle to it in db
    int db;
    if (dlp_OpenDB(sd, 0, 0x80 | 0x40, "DatebookDB", &db) < 0) {
        std::cerr << "    ERROR unable to open DatebookDB on Palm" << std::endl;
        // (char*) is a little unsafe, but function does not edit the string
        dlp_AddSyncLogEntry(sd, (char*)"Unable to open DatebookDB.\n"); // log on palm
        pi_close_fixed(sd, port);
        return EXIT_FAILURE;
    }
    else {
        std::cout << "    DatebookDB opened." << std::endl << std::flush;
    }


    /* handle datebook manipulation on the palm */

    // delete records if need be
    if (overwrite && !readonly) {
        // delete ALL records
        std::cout << "    Deleting existing Palm datebook..." << std::flush;
        if (dlp_DeleteRecord(sd, db, 1, 0) < 0) {
            std::cerr << std::endl << "    ERROR unable to delete DatebookDB records on Palm" << std::endl;
            // (char*) is a little unsafe, but function does not edit the string
            dlp_AddSyncLogEntry(sd, (char*)"Unable to delete DatebookDB records.\n"); // log on palm
            pi_close_fixed(sd, port);
            return EXIT_FAILURE;
        }
        std::cout << " done!" << std::endl << std::flush;
    }

    // read existing calendar events off of palm pilot and either add ONLYNEW events or delete existing events to be refreshed/updated
    // store UID in note and then grab it out UID for updating? or is datetime + name good enough? (config option?)
    if (!overwrite) {
        std::cout << "    Getting list of datebook entries for merging... ";

        #define REC_MAX 10000  // we're betting no one's got more than 10k records
        recordid_t recids[REC_MAX];
        int reccount;

        if (dlp_ReadRecordIDList(sd, db, 0, 0, REC_MAX, recids, &reccount) < 0) {
            // this fails with zero records, so zero it is
            reccount = 0;
        }
        std::cout << "done, " << reccount << " records" << std::endl;
        if (reccount == REC_MAX) {
            std::cout << "    WARNING " << REC_MAX << " record limit hit, some entries may not have been processed..." << std::endl;
        }

        std::cout << "    Reading existing datebook entries for merging... ";
        if (!onlynew && !readonly) {
            std::cout << std::endl << "    Deleting matching entries for updating... ";
        }
        std::cout << std::flush;

        // get the existing datebook entries
        // if onlynew is set then sync only entries that don't already appear (matched by date and time)
        // otherwise overwrite those previous entries, in effect updating them
        for (int i = 0; i < reccount; i++) {

            int attr; // record attributes so we don't deal with deleted or archived records?
            pi_buffer_t *Appointment_buf = pi_buffer_new(0xffff); // store the read record
            dlp_ReadRecordById(sd, db, recids[i], Appointment_buf, 0, &attr, 0);

            // records marked for deletion or archival are no longer on the palm after sync so skip as if they don't exist
            if ((attr & dlpRecAttrDeleted) || (attr & dlpRecAttrArchived)) {
                pi_buffer_free(Appointment_buf);
                continue;
            }

            // convert the packed data to something we can manipulate
            struct Appointment appointment;
            unpack_Appointment(&appointment, Appointment_buf, datebook_v1);

            // compare against the created appointments
            time_t starttime = timegm(&appointment.begin);
            time_t endtime = timegm(&appointment.end);

            for (int j = 0; j < Appointments.size(); j++) {
                if (strcmp(appointment.description, Appointments[j].description) == 0 &&
                        difftime(starttime, timegm(&Appointments[j].begin)) == 0  &&
                        // palm issues end time = start time for all day events, so just check if it's an all day instead
                        (difftime(endtime, timegm(&Appointments[j].end)) == 0 || appointment.event == 1 && Appointments[j].event == 1)) {

                    if (onlynew) {

                        // if the event already exists then we shouldn't copy a new one if only new events are to be copied
                        docopy[j] = false;
                    }
                    else {

                        // if we're OK with copying existing events, we don't want loads of them to show up so delete the existing one
                        if (!readonly) {
                            dlp_DeleteRecord(sd, db, 0, recids[i]);
                        }
                    }
//                    break; // we expect there to be only one matching event by summary, date & time, break out
                }
            } // for Appointments
            
            // free up used resources
            free_Appointment(&appointment);
            pi_buffer_free(Appointment_buf);
        }
        std::cout << "done!" << std::endl << std::flush;
    }

    // some tidying since we've been deleting things, might not do anything
    dlp_CleanUpDatabase(sd, db);
    dlp_ResetDBIndex(sd, db);

    // send the appointments across one by one
    if (!readonly) {
        std::cout << "    Writing calendar appointments... " << std::flush;
        for (int i = 0; i < Appointments.size(); i++) {

            // skip records not marked for transfer
            if (docopy[i] == false) {
                continue;
            }

            // pack the appointment struct for copying to the palm
            pi_buffer_t *Appointment_buf = pi_buffer_new(0xffff);
            pack_Appointment(&Appointments[i], Appointment_buf, datebook_v1);
            // could free here? should be fine to delete some things after the appointment is packed

            // send to the palm, this will return < 0 if there's an error - might want to do check that?
            dlp_WriteRecord(sd, db, 0, 0, 0, Appointment_buf->data, Appointment_buf->used, 0);
            // last argument is recordid_t *newrecuid, could store that between syncs with list of ical UIDs for better record updating
            // using PilotUser and SysInfo to identify individual palm pilots?

            // free up memory
            pi_buffer_free(Appointment_buf); // might as well free each buffer after it's written
//            free_Appointment(&Appointments[i]); // also frees string pointers (or just let these live until quitting hopefully destroys all)

        }
        std::cout << "done!" << std::endl << std::flush;
    }


    /* wrap palm things up */

    // close the datebook
    dlp_CloseDB(sd, db);
    db = -1;
    std::cout << "    DatebookDB closed." << std::endl << std::flush;

    // tell the user who it is, with a different PC id
    User.lastSyncPC     = 0x00010000;
    User.successfulSyncDate = time(NULL);
    User.lastSyncDate     = User.successfulSyncDate;
    dlp_WriteUserInfo(sd, &User);

    // (char*) is a little unsafe, but function does not edit the string
    dlp_AddSyncLogEntry(sd, (char*)"Successfully wrote Appointments to Palm.\n"); // log on palm

    // close the connection
    if (pi_close_fixed(sd, port) < 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
