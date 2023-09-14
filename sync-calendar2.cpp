// TODO: copyright, GPLv2

// sync-calendar attempt to read ical file and write to palm pilot
// if we're really good also try to not repeat any event that already exists
// note not fully ical complient, but should work with google calendar exports

#include <cstring>
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

int main(int argc, char **argv) {

    // configuration settings & defaults
    std::string configfile("datebook.cfg");
    std::string uri, port, timezone("UTC");
    int fromyear = 0;
    bool dohotsync = true, readonly = false, doalarms = false, overwrite = true, onlynew = false, insecure = false;
    bool portoverride = false, urioverride = false; // command line argument overrides config file argument

    // use to keep track if something happened or not (often for exiting on an error)
    bool failed = true;

    /** read in command line arguments **/

    std::cout << "    ==> Reading arguments <==" << std::endl << std::flush;

    // based on https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
    for (int c; (c = getopt(argc, argv, "hp:u:c:")) != -1; ) { // man 3 getopt
        switch (c) {
            case 'h': // port
                std::cout << "    Argument -h" << std::endl;
                std::cout << std::endl;
                std::cout << "    sync-calendar2, a tool for copying an ical calendar to Palm" << std::endl << std::endl;
                std::cout << "    Usage: sync-calendar2 [options]" << std::endl << std::endl;
                std::cout << "    Options:" << std::endl << std::endl;
                std::cout << "        -c  Specify config file (default datebook.cfg)" << std::endl;
                std::cout << "        -h  Print this help message and quit" << std::endl;
                std::cout << "        -p  Override config file port (e.g., /dev/ttyS0, net:any, usb:)" << std::endl;
                std::cout << "        -u  Override calendar URI" << std::endl;
                std::cout << std::endl;
                return EXIT_SUCCESS;

            case 'u': // uri
                failed = false;
                uri = optarg;
                std::cout << "    Argument -u: " << uri << std::endl;
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
    cfg.setOption(libconfig::Config::OptionAutoConvert, true); // float to int and viceversa?
    try {
        std::cout << "    Reading from " << configfile << std::endl;
        cfg.readFile(configfile);
    }
    catch (const libconfig::FileIOException &fioex) {
        std::cerr << "    I/O error while reading" << std::endl;
        return EXIT_FAILURE;
    }
    catch (const libconfig::ParseException &pex) {
        std::cerr << "    Parse error at " << pex.getFile() << ":" << pex.getLine() << " - " << pex.getError() << std::endl;
    }

    // use macros to tidy up reading config options
    // first in caps config item (will be a string), second variable name
    if (!urioverride) {
        FAIL_CFG(URI, uri)
    }
    if (!portoverride) {
        FAIL_CFG(PORT, port)
    }
    NON_FAIL_CFG(DOHOTSYNC, dohotsync)
    NON_FAIL_CFG(READONLY, readonly)
    NON_FAIL_CFG(TIMEZONE, timezone)
    NON_FAIL_CFG(FROMYEAR, fromyear)
    NON_FAIL_CFG(OVERWRITE, overwrite)
    NON_FAIL_CFG(ONLYNEW, onlynew)
    NON_FAIL_CFG(DOALARMS, doalarms)
    NON_FAIL_CFG(INSECURE, insecure)
    std::cout << std::endl << std::flush;


    /** read in calendar data using libcurl **/

    std::cout << "    ==> Downloading calendar <==" << std::endl << std::flush;

    // the downloaded ical data
    std::string icaldata;

    failed = false;
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, uri.c_str());

        // disable some SSL checks, reduced security
        if (insecure) {
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
            curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);
        }

        // cache the CA cert bundle in memory for a week
        curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);

        // tell curl to write to a std::string
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &icaldata);

        // perform the request, res will get the return code
        res = curl_easy_perform(curl);

        // check for errors
        if (res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            failed = true;
        }
        else {
            long http_code = 0;
            curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
            if (http_code != 200) {
                std::cerr << "    Error fetching URI, http response code " << http_code << std::endl;
                failed = true;
            }
        }

        // always cleanup!
        curl_easy_cleanup(curl);
    }
    else {
        std::cerr << "    Error initialising curl" << std::endl;
    }

    curl_global_cleanup();
 
    if (failed) {
        // something went wrong along the way, exit
        std::cerr << "    Exiting after error" << std::endl;
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

    // store all of the calendar events packed ready for copying to the palm
    std::vector<Appointment> Appointments;
    std::vector<std::string> uids; // for detecting & merging duplicate ical entries
    std::vector<bool> docopy; // actually copy to the palm?
    
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


            /* create Appointment if it doesn't already exist */

            // use event UID to detect duplicates & merge (Google seems to split things out sometimes?)
            icalproperty *uidp = icalcomponent_get_first_property(c, ICAL_UID_PROPERTY);

            std::string uid("");
            if (uidp != nullptr) {
                uid = icalproperty_get_uid(uidp);
                std::cout << "    UID: " << uid << std::endl;
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

            if (uidmatched != -1) {
                // if this uid exists twice, assume the later one in the file is newer and overwrite any properties specified
                appointment = Appointments[uidmatched];
            }


            /* date time description essentials */

            // convert dates and times to tm for transfer to palm
            icaltimetype start = icalcomponent_get_dtstart(c);
            time_t start_time_t = icaltime_as_timet_with_zone(start, icaltime_get_timezone(start));
            appointment.begin = *gmtime(&start_time_t); // dereference to avoid subsequent calls overwriting
            timegm(&appointment.begin); // convert localtime to UTC
            std::cout << "    Start: " << asctime(&appointment.begin);

            icaltimetype end = icalcomponent_get_dtend(c);
            time_t end_time_t = icaltime_as_timet_with_zone(end, icaltime_get_timezone(end));
            appointment.end = *gmtime(&end_time_t);
            timegm(&appointment.end);
            std::cout << "    End: " << asctime(&appointment.end);

            // if it's just a date in ical the appointment is an all day event
            if (icaltime_is_date(start) && icaltime_is_date(end))
                appointment.event          = 1;
            else
                appointment.event          = 0;

            // there is some tedious conversion from char to const char to string and around things
            // (probably a side effect of mixing C and C++)
            std::string summary("");
            const char* summary_c = icalcomponent_get_summary(c);
            if (summary_c != nullptr) {

                summary = summary_c;
                std::cout << "    Summary: " << summary << std::endl;

                // an annoying round about route from const char* to std::string to char*
                // https://stackoverflow.com/questions/7352099/stdstring-to-char
                char *summary_c2 = new char[summary.length()+1];
                strcpy(summary_c2, summary.c_str());
                appointment.description        = summary_c2;
            }
            else if (uidmatched == -1) { // only apply default if it's not part of merging another appointment
                appointment.description        = nullptr;
            }

            // get the location and descriptions and merge for an event attached note
            std::string location("");
            const char* location_c = icalcomponent_get_location(c);
            if (location_c != nullptr) {
                location = location_c;
            }

            std::string description("");
            const char* description_c = icalcomponent_get_description(c);
            if (description_c != nullptr) {
                description = description_c;
            }

            // merge location and description into the note
            // (palmos5 has a location but pilot-link doesn't support it - different database format?)
            std::string note;
            if (location.length() > 0) {
                note = note + "Location: " + location;
            }
            if (description.length() > 0) {
                if (note.length() > 0) {
                    note = note + "\n\n";
                }
                note = note + description;
            }
            if (note.length() > 0) { // note might be empty

                std::cout << "    Note:\n        " << note << std::endl;

                char *note_c2 = new char[note.length()+1];
                strcpy(note_c2, note.c_str());
                appointment.note               = note_c2;
            }
            else if (uidmatched == -1) {
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
            else if (uidmatched == -1) {
                appointment.alarm              = 0;
                // we can't store the alarm if it's not enabled so just dummy values here
                appointment.advance            = 0;
                appointment.advanceUnits       = advMinutes;
            }


            /* repat stuff, repeat stuff */

            // RRULE repeating sets
            // EXDATE dates in the set skipped, stored as a tm struct with year/month/day only
            // RDATE one off of repeating events that were moved, they appear as a normal event so ignore

            // https://libical.github.io/libical/apidocs/icalrecur_8h.html
            // https://libical.github.io/libical/apidocs/structicalrecurrencetype.html
            // https://freetools.textmagic.com/rrule-generator

            // X INTERVAL => repeatFrequency
            // X UNTIL => repeatEnd, repeatForever
            // X COUNT => repeatEnd
            // X WKST => repeatWeekstart
            // X BYMONTHDAY => repeatMonthlyByDay
            // X BYDAY => repeatDays
            // X FREQ => repeatType, repeatDay (montly), repeatDays (weekly)
            // X EXDATE => exception, exceptions

            // TODO: final repetition checks, need to test COUNT, EXDATE, RDATE thoroughly

            // declare some saine defaults to start with
            if (uidmatched == -1) { // will already have been set if it's uidmatched
                appointment.repeatType         = repeatNone;
                appointment.repeatForever      = 0;
                appointment.repeatEnd.tm_year  = 0;
                appointment.repeatEnd.tm_mon   = 0;
                appointment.repeatEnd.tm_mday  = 0;
                appointment.repeatEnd.tm_wday  = 0;
                appointment.repeatFrequency    = 0;
                appointment.repeatWeekstart    = 1; // 0-6 Sunday to Saturday, ical default is Monday so 1
                for (int i = 0; i < 7; i++) appointment.repeatDays[i] = 0;
    //            appointment.repeatDay          = 0;
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
                    // we add count * freq, but plam os ends on the day specified
                    // end last moment of the day before (we'll have to subtract that day for them later...)
                    appointment.repeatEnd.tm_hour = 23;
                    appointment.repeatEnd.tm_min = 59;
                    appointment.repeatEnd.tm_sec = 59;
                }

                appointment.repeatFrequency = recur.interval < 1 ? 1 : recur.interval; // 1 or INTERVAL

                // palm looks a bit different than libical here with the 1 being monday as opposed to 2 (ICAL_MONDAY_WEEKDAY)
                appointment.repeatWeekstart = weekday2int(recur.week_start);

                icalrecurrencetype_frequency freq = recur.freq;
                if (freq == ICAL_NO_RECURRENCE || 
                        freq == ICAL_SECONDLY_RECURRENCE || 
                        freq == ICAL_MINUTELY_RECURRENCE || 
                        freq == ICAL_HOURLY_RECURRENCE) {

                    // palm doesn't support anything less than daily, so no repeating
                    appointment.repeatType = repeatNone;
                    std::cout << "    Unsupported frequency, repeating disabled!" << std::endl;
                }
                else if (freq == ICAL_DAILY_RECURRENCE) {

                    std::cout << "    Repeating daily" << std::endl;
                    appointment.repeatType = repeatDaily;

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

                    if (!appointment.repeatForever && recur.count != 0) {
                        appointment.repeatEnd.tm_mday += recur.count*7 - 1;
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
                    }
                    else { // BYDAY   
           
                        // should only ever be the first day as monthly things can't occur more than once a month
                        if (recur.by_day[0] != ICAL_RECURRENCE_ARRAY_MAX) {
                            int week = icalrecurrencetype_day_position(recur.by_day[0]);
                            int day = weekday2int(icalrecurrencetype_day_day_of_week(recur.by_day[0]));

                            // not ideal, but I don't expect the DayOfMonthType enum to change anytime soon
                            appointment.repeatDay = (DayOfMonthType)((week - 1)*7 + day);

                            std::cout << "        Repeating the " << day << " of week " << week <<
                                " (enum " << appointment.repeatDay << " " << DayOfMonthString[appointment.repeatDay] << ")" << std::endl;

                            appointment.repeatType = repeatMonthlyByDay; 
                        }
                        else {
                            std::cout << "        Unexpected repeat???" << std::endl;
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


            /* phew, done with this event */

            // store the Appointment, either overwriting itself in the list of appointments or adding a new one
            if (uidmatched != -1) {
                Appointments[uidmatched] = appointment;
            }
            else {
                Appointments.push_back(appointment);
                uids.push_back(uid);
                docopy.push_back(true);
            }
            std::cout << "    Stored for sync" << std::endl << std::endl;

        } // for c
//        if (c != nullptr) icalcomponent_free(c);
        icalcomponent_free(components); // already not null by definition inside this if statement
    } // if components

    if (!dohotsync) {
        return EXIT_SUCCESS;
    }


    /** palm pilot communication **/

    std::cout << "    ==> Downloading to Palm <==" << std::endl << std::flush;

    // a lot of this comes from pilot-link userland.c, pilot-install-datebook.c, or pilot-read-ical.c

    int sd = -1; // socket descriptor (like an fid)
    if ((sd = pi_socket(PI_AF_PILOT, PI_SOCK_STREAM, PI_PF_DLP)) < 0) {
        std::cout << "    ERROR unable to create socket '" << port << "'" << std::endl;
        return EXIT_FAILURE;
    }

    if (pi_bind(sd, port.c_str()) < 0) {
        std::cout << "    ERROR unable to bind to port: " << port << std::endl;
        return EXIT_FAILURE;
    }

    std::cout << "    Listening for incoming connection on " << port << "... " << std::flush;

    if (pi_listen(sd, 1) < 0) {
        std::cout << std::endl << "    ERROR listening on " << port << std::endl;
        pi_close_fixed(sd);
        return EXIT_FAILURE;
    }

    sd = pi_accept_to(sd, 0, 0, 0); // last argument is a timeout in seconds - 0 is wait forever?
    if (sd < 0) {
        std::cout << "    ERROR accepting data on " << port << std::endl;
        pi_close_fixed(sd);
        return EXIT_FAILURE;
    }

    std::cout << "connected!" << std::endl;

    SysInfo sys_info;
    if (dlp_ReadSysInfo(sd, &sys_info) < 0) {
        std::cout << "    ERROR reading system info on " << port << std::endl;
        pi_close_fixed(sd);
        return EXIT_FAILURE;
    }

    PilotUser User;
    dlp_ReadUserInfo(sd, &User);

    // tell the palm we're going to be communicating
    if (dlp_OpenConduit(sd) < 0) {
        std::cout << "    ERROR opening conduit with Palm" << std::endl;
        pi_close_fixed(sd);
        return EXIT_FAILURE;
    }

    // open the datebook and store a handle to it in db
    int db;
    if (dlp_OpenDB(sd, 0, 0x80 | 0x40, "DatebookDB", &db) < 0) {
        std::cout << "    ERROR unable to open DatebookDB on Palm" << std::endl;
        // (char*) is a little unsafe, but function does not edit the string
        dlp_AddSyncLogEntry(sd, (char*)"Unable to open DatebookDB.\n"); // log on palm
        pi_close_fixed(sd);
        return EXIT_FAILURE;
    }
    else {
        std::cout << "    DatebookDB opened." << std::endl << std::flush;
    }

    if (overwrite && !readonly) {
        // delete ALL records
        std::cout << "    Deleting existing Palm datebook..." << std::flush;
        if (dlp_DeleteRecord(sd, db, 1, 0) < 0) {
            std::cout << std::endl << "    ERROR unable to delete DatebookDB records on Palm" << std::endl;
            // (char*) is a little unsafe, but function does not edit the string
            dlp_AddSyncLogEntry(sd, (char*)"Unable to delete DatebookDB records.\n"); // log on palm
            pi_close_fixed(sd);
            return EXIT_FAILURE;
        }
        std::cout << " done!" << std::endl << std::flush;
    }

    // TODO: timezone conversion to localtime










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

//            recordid_t recid; // for deleting specific records

            pi_buffer_t *Appointment_buf = pi_buffer_new(0xffff); // store the read record
//            int len = dlp_ReadRecordByIndex(sd, db, i, Appointment_buf, &recid, &attr, 0);
            dlp_ReadRecordById(sd, db, recids[i], Appointment_buf, 0, &attr, 0);

            // we've reached the end of the records
//            if (len < 0)
//                break;

            // records marked for deletion or archival are no longer on the palm after sync so skip as if they don't exist
            if ((attr & dlpRecAttrDeleted) || (attr & dlpRecAttrArchived)) {
                pi_buffer_free(Appointment_buf);
                continue;
            }

            // convert the packed data to something we can manipulate
            struct Appointment appointment;
            unpack_Appointment(&appointment, Appointment_buf, datebook_v1);

//            std::cout << appointment.description << std::endl;
//std::cout << asctime(&appointment.end) << std::endl;

bool matched = false;

            // compare against the created appointments
            time_t starttime = timegm(&appointment.begin);
            time_t endtime = timegm(&appointment.end);




//std::cout << asctime(&appointment.begin);

            for (int j = 0; j < Appointments.size(); j++) {
                if (strcmp(appointment.description, Appointments[j].description) == 0 &&
                        difftime(starttime, timegm(&Appointments[j].begin)) == 0  &&
                        // palm issues end time = start time for all day events, so just check if it's an all day instead
                        (difftime(endtime, timegm(&Appointments[j].end)) == 0 || appointment.event == 1 && Appointments[j].event == 1)) {

//std::cout << "    has a match with " << Appointments[j].description << std::endl;
//std::cout << asctime(&Appointments[j].begin);
//std::cout << difftime(starttime, timegm(&Appointments[j].begin)) << std::endl;
//std::cout << difftime(endtime, timegm(&Appointments[j].end)) << std::endl;
matched = true;

                    if (onlynew) {
                        // if the event already exists then we shouldn't copy a new one if only new events are to be copied
                        docopy[j] = false;
//                        std::cout << "do copy set false" << std::endl;
                    }
                    else {
                        // if we're OK with copying existing events, we don't want loads of them to show up so delete the existing one
                        if (!readonly) {
//                            dlp_DeleteRecord(sd, db, 0, recid);
                            dlp_DeleteRecord(sd, db, 0, recids[i]);

//dlp_ResetDBIndex(sd, db);
//std::cout << "    dropping record" << std::endl;
                        }
                    }

//                    break; // we expect there to be only one matching event by summary, date & time, break out
                }

//else if (strcmp(appointment.description, Appointments[j].description) == 0) {
//            std::cout << "MATCHCHHH" << Appointments[j].description << std::endl;
//std::cout << asctime(&Appointments[j].begin) << std::endl;
//std::cout << asctime(&Appointments[j].end) << std::endl;
//}


            } // for Appointments

//std::cout << std::endl;

//if (!matched) std::cout << "NOT MATCHED" << std::endl;
            
            // free up used resources
            free_Appointment(&appointment);
            pi_buffer_free(Appointment_buf);
        }
        std::cout << "done!" << std::endl << std::flush;
    }


//dlp_CleanUpDatabase(sd, db);








    // TODO: read existing calendar events off of palm pilot add add ONLYNEW events
    // store UID in note and then grab it out UID for updating? or is datetime + name good enough? (config option?)

    // TODO: only copy if the appointment doesn't already exist

    // send the appointments across one by one
    if (!readonly) {
//    if (0) {
        std::cout << "    Writing calendar appointments... " << std::flush;
        for (int i = 0; i < Appointments.size(); i++) {

            // skip records not marked for transfer
            if (docopy[i] == false) {
                continue;
            }

            // skip if it's older than FROMYEAR and repeating until now
            // move this logic earlier and store in an array of toskip?
            if ((Appointments[i].begin.tm_year + 1900) < fromyear && 
                    !Appointments[i].repeatForever && 
                    !((Appointments[i].repeatEnd.tm_year + 1900) >= fromyear)) {
//                if (Appointments[i].description != nullptr) {
//                    std::cout << "        Skipping " << Appointments[i].description << std::endl;
//                }
                continue;
            }

            // pack the appointment struct for copying to the palm
            pi_buffer_t *Appointment_buf = pi_buffer_new(0xffff);
            pack_Appointment(&Appointments[i], Appointment_buf, datebook_v1);
            // could free here? should be fine to delete some things after the appointment is packed

//std::cout << "copying " << Appointments[i].description << std::endl;

            // send to the palm, this will return < 0 if there's an error - might want to do check that?
            dlp_WriteRecord(sd, db, 0, 0, 0, Appointment_buf->data, Appointment_buf->used, 0);

            // free up memory
            pi_buffer_free(Appointment_buf); // might as well free each buffer after it's written
//            free_Appointment(&Appointments[i]); // also frees string pointers (or just let these live until quitting hopefully destroys all)

        }
        std::cout << "done!" << std::endl << std::flush;
    }

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
    if (pi_close_fixed(sd) < 0) {
        return EXIT_FAILURE;
    }

    return EXIT_SUCCESS;
}
