// TODO: copyright

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

// TODO: adding only new events, ONLYNEW=true
// TODO: quit nicely catching errors and whatnot (try catch finally?)
// log4cplus for logging?
int main(int argc, char **argv) {

    // configuration settings & defaults
    std::string configfile("datebook.cfg");
    std::string uri, port, timezone("UTC");
    int fromyear = 0;
    bool dohotsync = true, readonly = false, doalarms = false, overwrite = true, insecure = false;
    bool portoverride = false, urioverride = false; // command line argument overrides config file argument


    /** read in command line arguments **/

    // based on https://www.gnu.org/software/libc/manual/html_node/Example-of-Getopt.html
    for (int c; (c = getopt(argc, argv, "hp:u:c:")) != -1; ) { // man 3 getopt
        switch (c) {
            case 'h': // port
                std::cout << "sync-calendar2, a tool for copying an ical to palm" << std::endl << std::endl;
                std::cout << "Usage: sync-calendar2 [options]" << std::endl << std::endl;
                std::cout << "Options:" << std::endl << std::endl;
                std::cout << "    -c  Specify config file (default datebook.cfg)" << std::endl;
                std::cout << "    -h  Print this help message and quit" << std::endl;
                std::cout << "    -p  Override config file port (e.g., /dev/ttyS0, net:any, usb:)" << std::endl;
                std::cout << "    -u  Override calendar URI" << std::endl;
                std::cout << std::endl;
                return EXIT_SUCCESS;

            case 'u': // uri
                uri = optarg;
                std::cout << "Argument -u: " << uri << std::endl;
                urioverride = true;
                break;

            case 'p': // port
                port = optarg;
                std::cout << "Argument -p: " << port << std::endl;
                portoverride = true;
                break;

            case 'c': // config file
                configfile = optarg;
                break;

            case '?':
                return EXIT_FAILURE;

            default:
                break;
        }
    }


    /** read in configuration settings using libconfig **/

    libconfig::Config cfg;
    cfg.setOption(libconfig::Config::OptionAutoConvert, true); // float to int and viceversa?
    try {
        std::cout << "Reading from " << configfile << std::endl;
        cfg.readFile(configfile);
    }
    catch (const libconfig::FileIOException &fioex) {
        std::cerr << "I/O error while reading" << std::endl;
        return EXIT_FAILURE;
    }
    catch (const libconfig::ParseException &pex) {
        std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine() << " - " << pex.getError() << std::endl;
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
    NON_FAIL_CFG(DOALARMS, doalarms)
    NON_FAIL_CFG(INSECURE, insecure)
    std::cout << std::endl;


    /** read in calendar data using libcurl **/

    // use to mark exiting on an error
    bool failed = false;
    // the downloaded ical data
    std::string icaldata;

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
                std::cerr << "error fetching URI, http response code " << http_code << std::endl;
                failed = true;
            }
        }

        // always cleanup!
        curl_easy_cleanup(curl);
    }
    else {
        std::cerr << "error initialising curl" << std::endl;
    }

    curl_global_cleanup();
 
    if (failed) {
        // something went wrong along the way, exit
        std::cerr << "exiting after error" << std::endl;
        return EXIT_FAILURE;
    }


    /** parse calendar using libical **/

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
    std::vector<std::string> uids; // for detecting & merging duplicates
    
    // parse the string into a series of components to iterate through
    icalcomponent* components = icalparser_parse_string(icaldata.c_str());

    // remove errors (which also includes empty descriptions, locations, and the like) 
    icalcomponent_strip_errors(components);

    // if there is some valid ical data to work through
    if (components != nullptr) {

        // the specific component we're on in our iteration
        icalcomponent *c;

        // we're only interested in calendar events, iterate through them
        for(c = icalcomponent_get_first_component(components, ICAL_VEVENT_COMPONENT); c != 0;
                c = icalcomponent_get_next_component(components, ICAL_VEVENT_COMPONENT)) {

            // palm only has start time, end time, alarm, repeat, description, and note
            // so we only need to extract those things from the component if they're there
            // every event should have a DTSTART, DTEND, and DTSUMMARY (which is palm description)
            // palm won't take an event with a description

            std::cout << "Processing event" << std::endl;


            /* date time description essentials */

            // convert dates and times to start_tm for transfer to palm
            icaltimetype start = icalcomponent_get_dtstart(c);
            time_t start_time_t = icaltime_as_timet_with_zone(start, icaltime_get_timezone(start));
            tm start_tm = *gmtime(&start_time_t); // dereference to avoid subsequent calls overwriting
            timegm(&start_tm); // convert localtime to UTC
            std::cout << "    Start: " << asctime(&start_tm);

            icaltimetype end = icalcomponent_get_dtend(c);
            time_t end_time_t = icaltime_as_timet_with_zone(end, icaltime_get_timezone(end));
            tm end_tm = *gmtime(&end_time_t);
            timegm(&end_tm);
            std::cout << "    End: " << asctime(&end_tm);

            // there is some tedious conversion from char to const char to string and around things
            // (probably a side effect of mixing C and C++)
            std::string summary("");
            const char* summary_c = icalcomponent_get_summary(c);
            if (summary_c != nullptr) {
                summary = summary_c;
            }
            std::cout << "    Summary: " << summary << std::endl;

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
            if (note.length() > 0) {
                std::cout << "    Note:\n        " << note << std::endl;
            }


// TODO: create Appointment here


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
            } // numalarms

            if (shortest_alarm != 9999989) {
                std::cout << "    Alarm: " << shortest_alarm << " minutes before" << std::endl;
            }
            else {
                shortest_alarm = 0;
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

// TODO: operate directly on Appointment instead of these temporary values
            tm repeatEnd;
            repeatEnd.tm_year   = 0;
            repeatEnd.tm_mon   = 0;
            repeatEnd.tm_mday  = 0;
            repeatEnd.tm_wday  = 0;
            int repeatForever = 0;
            int repeatFrequency = 0;
            repeatTypes repeatType = repeatNone; // default no repeat
            int repeatWeekstart = 1; // 0-6 Sunday to Saturday, ical default is Monday so 1
            DayOfMonthType repeatDay;
            int repeatDays[7]; for (int i = 0; i < 7; i++) repeatDays[i] = 0;
            int exceptions = 0;
            tm *exception = nullptr; // this is an array, yikes

            // this assumes there's only ever one RRULE property (palm can only support one anyway)
            icalproperty *rrule = icalcomponent_get_first_property(c, ICAL_RRULE_PROPERTY);
            if (rrule != nullptr) {

                icalrecurrencetype recur = icalproperty_get_rrule(rrule);
                std::cout << "    Recurrence: " << icalrecurrencetype_as_string(&recur) << std::endl;

                // we're assuming UNTIL and COUNT are mutually exclusive
                if (recur.until.year != 0) {

                    // there's an until date, use that
                    time_t until_time_t = icaltime_as_timet_with_zone(recur.until, icaltime_get_timezone(recur.until));
                    repeatEnd = *gmtime(&until_time_t); // dereference to avoid subsequent calls overwriting
                    timegm(&end_tm);
                }
                else if (recur.count == 0) {

                    // no until date, for the moment assume repeating forever
                    repeatForever = 1;
                }
                else {

                    //  we'll have to figure out what repeatEnd should be based on count, but this depends on frequency...
                    repeatEnd = start_tm; // this should already be UTC
                    // we add count * freq, but plam os ends on the day specified
                    // end last moment of the day before (we'll have to subtract that day for them later...)
                    repeatEnd.tm_hour = 23;
                    repeatEnd.tm_min = 59;
                    repeatEnd.tm_sec = 59;
                }

                repeatFrequency = recur.interval < 1 ? 1 : recur.interval; // 1 or INTERVAL

                // palm looks a bit different than libical here with the 1 being monday as opposed to 2 (ICAL_MONDAY_WEEKDAY)
                repeatWeekstart = weekday2int(recur.week_start);

                icalrecurrencetype_frequency freq = recur.freq;
                if (freq == ICAL_NO_RECURRENCE || 
                        freq == ICAL_SECONDLY_RECURRENCE || 
                        freq == ICAL_MINUTELY_RECURRENCE || 
                        freq == ICAL_HOURLY_RECURRENCE) {

                    // palm doesn't support anything less than daily, so no repeating
                    repeatType = repeatNone;
                    std::cout << "    Unsupported frequency, repeating disabled!" << std::endl;
                }
                else if (freq == ICAL_DAILY_RECURRENCE) {

                    std::cout << "    Repeating daily" << std::endl;
                    repeatType = repeatDaily;

                    if (!repeatForever && recur.count != 0) {
                        repeatEnd.tm_mday += recur.count - 1;
                        timegm(&repeatEnd);
                    }
                }
                else if (freq == ICAL_WEEKLY_RECURRENCE) {

                    std::cout << "    Repeating weekly" << std::endl;
                    repeatType = repeatWeekly; // repeatDays from BYDAY

                    // need to loop as there might be more than one day..?
                    for (int i = 0; recur.by_day[i] != ICAL_RECURRENCE_ARRAY_MAX; i++) {
                        int day = weekday2int(icalrecurrencetype_day_day_of_week(recur.by_day[i]));
                        repeatDays[day] = 1;
                        std::cout << "        Repeating day " << day << std::endl;
                    }

                    if (!repeatForever && recur.count != 0) {
                        repeatEnd.tm_mday += recur.count*7 - 1;
                        timegm(&repeatEnd);
                    }
                }
                else if (freq == ICAL_MONTHLY_RECURRENCE) {

                    std::cout << "    Repeating montly" << std::endl;
                    // events usually only repeat on one day of the month, so just check the 0th index
                    if (recur.by_month_day[0] != ICAL_RECURRENCE_ARRAY_MAX) { // BYMONTHDAY

                        // nothing extra to set, palm will just assume it's the date of the start
                        repeatType = repeatMonthlyByDate;
                        // day of the month in by day repeat - this is done in pilot-datebook, but doesn't seem needed based on pi-datebook.h
                        repeatDay = (DayOfMonthType)recur.by_month_day[0]; 
                        std::cout << "        Repeating on " << start_tm.tm_mday << std::endl;
                    }
                    else { // BYDAY   
           
                        // should only ever be the first day as monthly things can't occur more than once a month
                        if (recur.by_day[0] != ICAL_RECURRENCE_ARRAY_MAX) {
                            int week = icalrecurrencetype_day_position(recur.by_day[0]);
                            int day = weekday2int(icalrecurrencetype_day_day_of_week(recur.by_day[0]));

                            // not ideal, but I don't expect the DayOfMonthType enum to change anytime soon
                            repeatDay = (DayOfMonthType)((week - 1)*7 + day);

                            std::cout << "        Repeating the " << day << " of week " << week <<
                                " (enum " << repeatDay << " " << DayOfMonthString[repeatDay] << ")" << std::endl;

                            repeatType = repeatMonthlyByDay; 
                        }
                        else {
                            std::cout << "        Unexpected repeat???" << std::endl;
                        }
                    }

                    if (!repeatForever && recur.count != 0) {
                        repeatEnd.tm_mon += recur.count;
                        repeatEnd.tm_mday--;
                        timegm(&repeatEnd);
                    }
                }
                else if (freq == ICAL_YEARLY_RECURRENCE) {
                    std::cout << "    Repeating yearly" << std::endl;
                    repeatType = repeatYearly;

                    if (!repeatForever && recur.count != 0) {
                        repeatEnd.tm_year += recur.count;
                        repeatEnd.tm_mday--;
                        timegm(&repeatEnd);
                    }
                }
                else {
                    std::cout << "    Unknown repeat frequency" << std::endl;
                }
                if (!repeatForever) {
                    std::cout << "        Until " << asctime(&repeatEnd);
                }


                /* argh exceptions */

                exceptions = icalcomponent_count_properties(c, ICAL_EXDATE_PROPERTY);
                if (exceptions != 0) {
                    std::cout << "    There are " << exceptions << " exceptions" << std::endl;
                }

                exception = (tm*)malloc(exceptions * sizeof(tm)); // this should be freed by free_Appointment later

                int exceptionat = 0;
                for(icalproperty *exdatep = icalcomponent_get_first_property(c, ICAL_EXDATE_PROPERTY); exdatep != 0;
                        exdatep = icalcomponent_get_next_property(c, ICAL_EXDATE_PROPERTY), exceptionat++) {

                    icaltimetype exdate = icalproperty_get_exdate(exdatep);
                    time_t exdate_time_t = icaltime_as_timet_with_zone(exdate, icaltime_get_timezone(exdate));

                    exception[exceptionat] = *gmtime(&exdate_time_t); // dereference to avoid subsequent calls overwriting
                    timegm(&exception[exceptionat]); // localtime to UTC
                    std::cout << "        Excluding " << asctime(&exception[exceptionat]);
                } // for exdatep
            } // rrule


// TODO: move this appointment creation earlier so that we directly access it instead of using temporary initialisers

// use event UID to detect duplicates & merge (Google seems to split things out sometimes)
icalproperty *uidp = icalcomponent_get_first_property(c, ICAL_UID_PROPERTY);

std::string uid("");
if (uidp != nullptr) {
    uid = icalproperty_get_uid(uidp);
    std::cout << "    UID: " << uid << std::endl;
}

int uidmatched = -1;
for (int i = 0; i < uids.size(); i++) {
    if (uids[i] == uid && uid != "") {
        std::cout << "matching previous appt" << std::endl;
        uidmatched = i;
        break;
    }
}


            // an annoying round about route from const char* to std::string to char*
            // https://stackoverflow.com/questions/7352099/stdstring-to-char
            char *summary_c2 = new char[summary.length()+1];
            strcpy(summary_c2, summary.c_str());
            char *note_c = nullptr;
            if (note.length() > 0) { // note might be empty
                note_c = new char[note.length()+1];
                strcpy(note_c, note.c_str());
            }

            // store information about this event in the pilot-link struct
            // see pi-datebook.h for details of format

            Appointment appointment;
if (uidmatched != -1) {
    appointment = Appointments[uidmatched];
//std::cout << appointment.description << std::endl;
// if this uid exists twice, assume the later one in the file is newer and overwrite

}
//else {
//}

            // TODO: only copy if the appointment doesn't already exist

            if (icaltime_is_date(start) && icaltime_is_date(end))
                appointment.event          = 1;
            else
                appointment.event          = 0;
            appointment.begin              = start_tm;
            appointment.end                = end_tm;
            if (doalarms == true && shortest_alarm != 0) {
                appointment.alarm              = 1;
                appointment.advance            = shortest_alarm;
            }
            else {
                appointment.alarm              = 0;
                appointment.advance            = 0;
                // setting advance here doesn't work, we can't store the alarm if it's not enabled
            }
            appointment.advanceUnits       = advMinutes;
            appointment.repeatType         = repeatType;
            appointment.repeatForever      = repeatForever;
            appointment.repeatEnd.tm_year  = repeatEnd.tm_year;
            appointment.repeatEnd.tm_mon   = repeatEnd.tm_mon;
            appointment.repeatEnd.tm_mday  = repeatEnd.tm_mday;
            appointment.repeatEnd.tm_wday  = repeatEnd.tm_wday;
            appointment.repeatFrequency    = repeatFrequency;
            appointment.repeatDay          = repeatDay;
            for (int i = 0; i < 7; i++) appointment.repeatDays[i] = repeatDays[i];
            appointment.repeatWeekstart    = repeatWeekstart;
            appointment.exceptions         = exceptions;
            appointment.exception          = exception;
            appointment.description        = summary_c2; // just don't set these if null?
            appointment.note               = note_c;

            // TODO: keep a list of appointments instead of appointment bufs
            //       and keep a list of UIDs so that we can merge by them

//            Appointment_bufs.push_back(pi_buffer_new (0xffff));
//            pack_Appointment(&appointment, Appointment_bufs.back(), datebook_v1);


if (uidmatched != -1) {
    Appointments[uidmatched] = appointment;
}
else {
            Appointments.push_back(appointment);
    uids.push_back(uid);
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

    // TODO: separate out into its own file/function? (should make reading easier?)
    // TODO: check what all the pilot-link functions return and what are fatal and we need to exit on etc

    int db; // handle to the database
    int sd = -1; // socket descriptor (like fid?)
    int result;

    PilotUser User;

    int plu_quiet = 0; // don't surpress some output
    int plu_timeout = 0; // "Use timeout <timeout> seconds", "<timeout>" - wait forwever with 0?

    // from pilot-link userland.c
    SysInfo sys_info;

    if ((sd = pi_socket(PI_AF_PILOT, PI_SOCK_STREAM, PI_PF_DLP)) < 0) {
        fprintf(stderr, "\n    Unable to create socket '%s'\n", port.c_str());
        return EXIT_FAILURE;
    }

    if (pi_bind(sd, port.c_str()) < 0) {
        fprintf(stderr, "    Unable to bind to port: %s\n", port.c_str());
        return EXIT_FAILURE;
    }

    if (!plu_quiet && isatty(fileno(stdout))) {
        printf("\n    Listening for incoming connection on %s... ", port.c_str());
        fflush(stdout);
    }

    if (pi_listen(sd, 1) < 0) {
        fprintf(stderr, "\n    Error listening on %s\n", port.c_str());
        pi_close(sd);
        return EXIT_FAILURE;
    }

    sd = pi_accept_to(sd, 0, 0, plu_timeout);
    if (sd < 0) {
        fprintf(stderr, "\n    Error accepting data on %s\n", port.c_str());
        pi_close(sd);
        return -1;
    }

    if (!plu_quiet && isatty(fileno(stdout))) {
        printf("connected!\n\n");
    }

    if (dlp_ReadSysInfo(sd, &sys_info) < 0) {
        fprintf(stderr, "\n    Error read system info on %s\n", port.c_str());
        pi_close(sd);
        return -1;
    }

    dlp_ReadUserInfo(sd, &User);

    if (dlp_OpenConduit(sd) < 0) {
        pi_close(sd);
        return EXIT_FAILURE;
    }

    /* Open the Datebook's database, store access handle in db */
    if (dlp_OpenDB(sd, 0, 0x80 | 0x40, "DatebookDB", &db) < 0) {
        fprintf(stderr,"    ERROR: Unable to open DatebookDB on Palm\n");
        dlp_AddSyncLogEntry(sd, (char*)"Unable to open DatebookDB.\n");
        // (char*) is a little unsafe, but function does not edit the string
        pi_close(sd);
        return 0;
    }
    else {
        std::cout << "    Datebook opened." << std::endl;
    }

    if (overwrite && !readonly) {
        // delete ALL records
        std::cout << "    Deleting existing Palm calendar...";
        result = dlp_DeleteRecord(sd, db, 1, 0);
        std::cout << " done!" << std::endl << std::endl;
    }



    // TODO: timezone conversion to localtime

    // TODO: read existing calendar events off of palm pilot add add ONLYNEW events
    // store UID in note and then grab it out UID for updating? or is datetime + name good enough? (config option?)



    // send the appointments across one by one
    if (!readonly) {
        std::cout << "    Writing calendar appointments...";
        for (int i = 0; i < Appointments.size(); i++) {


            // TODO: skip if it's older than FROMYEAR and repeating until now
//            if (start.year < fromyear) {
//                std::cout << "    Skipping" << std::endl;
//                continue;
//            }


            // pack the appointment struct for copying to the palm
	        pi_buffer_t *Appointment_buf = pi_buffer_new(0xffff);
	        pack_Appointment(&Appointments[i], Appointment_buf, datebook_v1);
            // could free here? should be fine to delete some things after the appointment is packed

            // send to the palm
            dlp_WriteRecord(sd, db, 0, 0, 0, Appointment_buf->data, Appointment_buf->used, 0);

            // free up memory
            pi_buffer_free(Appointment_buf); // might as well free each buffer after it's written
//            free_Appointment(&Appointments[i]); // also frees string pointers (or just let these live until quitting hopefully destroys all)

        }
        std::cout << " done!" << std::endl << std::endl;
    }

    /* Close the database */
    dlp_CloseDB(sd, db);
    std::cout << "    Datebook closed." << std::endl;

    /* Tell the user who it is, with a different PC id. */
    User.lastSyncPC     = 0x00010000;
    User.successfulSyncDate = time(NULL);
    User.lastSyncDate     = User.successfulSyncDate;
    dlp_WriteUserInfo(sd, &User);

    // (char*) is a little unsafe, but function does not edit the string
    if (dlp_AddSyncLogEntry(sd, (char*)"Successfully wrote Appointment to Palm.\n") < 0) {
        pi_close(sd);
        return 0;
    }    

    if (dlp_EndOfSync(sd, 0) < 0)  {
        pi_close(sd);
        return 0;
    }

    // work around for hanging on close due (probably) a race condition closing out libusb
    failed = false;
    pi_socket_t *ps = find_pi_socket(sd);
    if (ps) {
        pi_usb_data_t *data = (pi_usb_data_t *)ps->device->data;
        usb_dev_handle *dev = (usb_dev_handle*)data->ref;
        libusb_device_handle *dev_handle = dev->handle;
        libusb_context *ctx = HANDLE_CTX(dev_handle);
        if (ctx) {
            libusb_unlock_events(ctx);
        }
        else {
            failed = true;
        }
    }
    else {
        failed = true;
    }
    if (failed) {
        std::cout << "    Probably hanging on a libusb race condition now..." << std::endl << std::endl;
    }
    // the glorious one line version without error handling
    // libusb_unlock_events((((usb_dev_handle*)((pi_usb_data_t *)find_pi_socket(sd)->device->data)->ref)->handle)->dev->ctx);

    if(pi_close(sd) < 0) {
        std::cout << "    Error closing socket to plam pilot" << std::endl << std::endl;
    }

    return 0;
}
