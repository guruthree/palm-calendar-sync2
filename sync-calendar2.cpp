// TODO: copyright

#include <cstring>
//#include <cstdlib>
//#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

#include <libconfig.h++>
#include <curl/curl.h>
#include <libical/ical.h>

#include <libpisock/pi-datebook.h>
#include <libpisock/pi-dlp.h>
#include <libpisock/pi-socket.h>

// sync-calendar attempt to read ical file and write to palm pilot
// if we're really good also try to not repeat any event that already exists

using namespace libconfig;

// callback for having curl store output in a std::string
// https://stackoverflow.com/questions/2329571/c-libcurl-get-output-into-a-string
size_t CurlWrite_CallbackFunc_StdString(void *contents, size_t size, size_t nmemb, std::string *s)
{
    size_t newLength = size*nmemb;
    try
    {
        s->append((char*)contents, newLength);
    }
    catch(std::bad_alloc &e)
    {
        //handle memory problem
        return 0;
    }
    return newLength;
}


// TODO: we'll need to implement argument parsing for specifying:
// * how to reach the palm pilot
// * source ical file/url
// * option to disable ssl cert checks
// * check datebookcfg from previous python project
int main(int argc, char **argv) {


    /** variables **/

    // use to mark exiting on an error
    bool failed = false;

    // configuration settings
    std::string uri, timezone, port;
    int fromyear;

    // the downloaded ical data
    std::string icaldata;


    /** read in configuration settings using libconfig **/
    Config cfg;
    try
    {
        cfg.readFile("datebook.cfg");
    }
    catch(const FileIOException &fioex)
    {
        std::cerr << "I/O error while reading file." << std::endl;
        return(EXIT_FAILURE);
    }
    catch(const ParseException &pex)
    {
        std::cerr << "Parse error at " << pex.getFile() << ":" << pex.getLine() << " - " << pex.getError() << std::endl;
    }

    // there's probably a nicer way to duplicate this code with templates?
    if (!cfg.lookupValue("URI", uri)) {
        std::cerr << "No 'URI' setting in configuration file, failing." << std::endl;
        return(EXIT_FAILURE);
    }
    if (!cfg.lookupValue("TIMEZONE", timezone)) {
        std::cerr << "No 'TIMEZONE' setting, assuming UTC." << std::endl;
        timezone = "UTC";
    }
    if (!cfg.lookupValue("FROMYEAR", fromyear)) {
        std::cerr << "No 'FROMYEAR' setting, assuming all." << std::endl;
        fromyear = 0;
    }
    if (!cfg.lookupValue("PORT", port)) {
        std::cerr << "No 'PORT' setting in configuration file, failing." << std::endl;
        return(EXIT_FAILURE);
    }

    std::cout << "uri: " << uri << std::endl;
    std::cout << "timezone: " << timezone << std::endl;
    std::cout << "fromyear: " << fromyear << std::endl;
    std::cout << "port: " << port << std::endl << std::endl;


    /** read in calendar data using libcurl **/
    CURL *curl;
    CURLcode res;
    curl_global_init(CURL_GLOBAL_DEFAULT);
    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, uri.c_str());

        // disable some SSL checks, reduced security
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        // cache the CA cert bundle in memory for a week
        curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);

        // tell curl to write to a std::string
        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &icaldata);

        // perform the request, res will get the return code
        res = curl_easy_perform(curl);

        // check for errors
        if(res != CURLE_OK) {
            std::cerr << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            failed = true;
            // TODO: handle this nicely
        }

        long http_code = 0;
        curl_easy_getinfo (curl, CURLINFO_RESPONSE_CODE, &http_code);
        if (http_code != 200) {
            std::cerr << "error fetching URI, http response code " << http_code << std::endl;
            failed = true;
        }

        // always cleanup!
        curl_easy_cleanup(curl);
    }
    else {
        std::cerr << "error initialising curl" << std::endl;
    }

    curl_global_cleanup();
 
    if (failed) {
        // something went wrong along the way, exist
        std::cerr << "exiting after error" << std::endl;
        return 0;
    }


    /** parse calendar using libical **/

    // ical specifies components, properties, values, and parameters and I think there's
    // a bit of a metaphor relating that to HTML, with COMPONENTS corresponding to an
    // opening BEGIN tag which contains PROPERTY tags that then have VALUES (the main
    // thing in that tag, and then PARAMETERS which are kind of like HTML attributes,
    // adding extra information about the PARAMETER
    
    // parse the string into a series of components to iterate through
    icalcomponent* components = icalparser_parse_string(icaldata.c_str());

    // remove errors (which also includes empty descriptions, locations, and the like) 
    icalcomponent_strip_errors(components);

struct Appointment appointment;

    // if there is some valid ical data to work through
    if (components != nullptr) {

        // the specific component we're on in our iteration
        icalcomponent *c;

        // TODO: google calendar output might have some time-zone definitions at the
        // start via X-LIC-LOCATION if we accept ICAL_ANY_COMPONENT
        for(c = icalcomponent_get_first_component(components, ICAL_VEVENT_COMPONENT); c != 0;
                c = icalcomponent_get_next_component(components, ICAL_VEVENT_COMPONENT)) {


            // palm only has start time, end time, alarm, repeat, description, and note
            // so we only need to extract those things from the component if they're there
            // every event should have a DTSTART, DTEND, and DTSUMMARY (which is palm description)
            // palm won't take an event with a description

            std::cout << "Processing event" << std::endl;

            std::string summary(icalcomponent_get_summary(c));
            std::cout << "    Summary: " << summary << std::endl;

            // skip older entries
            icaltimetype start = icalcomponent_get_dtstart(c);
            if (start.year < fromyear) {
                std::cout << "    Skipping" << std::endl;
                continue;
            }

            // convert dates and times to start_tm for transfer to palm
            char buf[200];
            time_t start_time_t = icaltime_as_timet_with_zone(start, icaltime_get_timezone(start));
            tm *start_tm = gmtime(&start_time_t);
            if (icaltime_is_date(start)) { // no time, so whole day
                strftime(buf, sizeof buf, "%FT", start_tm);
            }
            else {
                strftime(buf, sizeof buf, "%FT%TZ", start_tm);
            }
            std::cout << "    Start: " << buf << std::endl;

            icaltimetype end = icalcomponent_get_dtend(c);
            time_t end_time_t = icaltime_as_timet_with_zone(end, icaltime_get_timezone(end));
            tm *end_tm = gmtime(&end_time_t);
            if (icaltime_is_date(end)) {
                strftime(buf, sizeof buf, "%FT", end_tm);
            }
            else {
                strftime(buf, sizeof buf, "%FT%TZ", end_tm);
            }
            std::cout << "    End: " << buf << std::endl;

            std::string location;
            const char* location_c = icalcomponent_get_location(c);
            if (location_c != nullptr) {
                location = location_c;
            }
            else {
                location = "None";
//                location = "";
            }

            std::string description;
            const char* description_c = icalcomponent_get_description(c);
            if (description_c != nullptr) {
                description = description_c;
            }
            else {
                description = "";
            }

            // merge location and description into the note (palmos5 has a location but pilot-link doesn't support it)
            // TODO: smarter here, don't add location bit if no location, and nothing at all if both blank
            std::string note = "Location: " + location;
            if (description.length() > 0) {
                note = note + "\n\n" + description;
            }
            std::cout << "    Note: [[" << note << "]]" << std::endl;

            // an annoying round about route from const char* to std::string to char*
            // https://stackoverflow.com/questions/7352099/stdstring-to-char
            char *summary_c = new char[summary.length()+1];
            strcpy(summary_c, summary.c_str());
            char *note_c = new char[note.length()+1];
            strcpy(note_c, note.c_str());

            // TODO: delete [] summary_c;
            // TODO: delete [] note_c;

            // store information about this event in the pilot-link struct
            // see pi-datebook.h for details of format
//            struct Appointment appointment;

            // TODO: only copy if the appointment doesn't already exist

            // TODO: delete all existing appointments

            if (icaltime_is_date(start) && icaltime_is_date(end))
                appointment.event          = 1;
            else
                appointment.event          = 0;
            appointment.begin              = *start_tm; // de-reference?
            appointment.end                = *end_tm;
appointment.end.tm_hour++; // ?????
            appointment.alarm              = 0;
            appointment.advance            = 0;
            appointment.advanceUnits       = 0;
            appointment.repeatType         = repeatNone;
            appointment.repeatForever      = 0;
            appointment.repeatEnd.tm_mday  = 0;
            appointment.repeatEnd.tm_mon   = 0;
            appointment.repeatEnd.tm_wday  = 0;
            appointment.repeatFrequency    = 0;
            appointment.repeatWeekstart    = 0;
            appointment.exceptions         = 0;
            appointment.exception          = NULL;
            appointment.description        = summary_c;
            appointment.note               = note_c;


/*            icalproperty *p; // there will definetely be properties

            for(p = icalcomponent_get_first_property(c, ICAL_ANY_PROPERTY); p != 0;
                    p = icalcomponent_get_next_property(c, ICAL_ANY_PROPERTY)) {

                // use the get functions for these
//                if (icalproperty_isa(p) == ICAL_DTSTART_PROPERTY)
//                    continue;
//                if (icalproperty_isa(p) == ICAL_DTEND_PROPERTY)
//                    continue;
//                if (icalproperty_isa(p) == ICAL_SUMMARY_PROPERTY)
//                    continue;

                std::cout << "  " << icalproperty_get_property_name(p) << std::endl;
                std::cout << " \\" << icalproperty_get_value_as_string(p) << std::endl;

                icalparameter *p2; // there might not be parameters

                for(p2 = icalproperty_get_first_parameter(p, ICAL_ANY_PARAMETER); p2 != 0;
                        p2 = icalproperty_get_next_parameter(p, ICAL_ANY_PARAMETER)) {



                    //std::cout << "p2" << icalparameter_as_ical_string(p2);
                    const char* propname = nullptr;
                    const char* val = nullptr;
                    icalparameter_kind paramkind = icalparameter_isa(p2);

                    if (paramkind == ICAL_X_PARAMETER) {
                        propname = icalparameter_get_xname(p2);
                        val = icalparameter_get_xvalue(p2);
                    }
                    else if (paramkind == ICAL_IANA_PARAMETER) {
                        propname = icalparameter_get_iana_name(p2);
                        val = icalparameter_get_iana_value(p2);
                    }
                    else if (paramkind != ICAL_NO_PARAMETER) {
                        propname = icalparameter_kind_to_string(paramkind);
                        val = icalproperty_get_parameter_as_string(p, propname);
                    }


                    std::cout << "    " << propname << std::endl;
                    if (val != nullptr) {
                        std::cout << "   \\" << val << std::endl;
                    }

                } // for p2
                if (p2 != nullptr) icalparameter_free(p2);

            } // for p
            if (p != nullptr) icalproperty_free(p); */

            std::cout << "End of event" << std::endl;

        } // for c
        if (c != nullptr) icalcomponent_free(c);

        icalcomponent_free(components); // already not null by definition inside this loop

    } // if components

//return 0;



    int db; 
	int sd = -1;
	int result;

	pi_buffer_t *Appointment_buf;
	Appointment_buf 	= pi_buffer_new (0xffff);


	struct 	PilotUser User;

int plu_quiet = 0; // don't surpress some output
int plu_timeout = 0; // "Use timeout <timeout> seconds", "<timeout>" - wait forwever with 0?

// from pilot-link userland.c
	struct  SysInfo sys_info;

	if ((sd = pi_socket(PI_AF_PILOT,
			PI_SOCK_STREAM, PI_PF_DLP)) < 0) {
		fprintf(stderr, "\n   Unable to create socket '%s'\n", port.c_str());
		return -1;
	}

	result = pi_bind(sd, port.c_str());

	if (result < 0) {
		fprintf(stderr, "   Unable to bind to port: %s\n"
				"   Please use --help for more information\n\n",
				port.c_str());
		return result;
	}

	if (!plu_quiet && isatty(fileno(stdout))) {
		printf("\n   Listening for incoming connection on %s... ",
			port.c_str());
		fflush(stdout);
	}

	if (pi_listen(sd, 1) < 0) {
		fprintf(stderr, "\n   Error listening on %s\n", port.c_str());
		pi_close(sd);
		return -1;
	}

	sd = pi_accept_to(sd, 0, 0, plu_timeout);
	if (sd < 0) {
		fprintf(stderr, "\n   Error accepting data on %s\n", port.c_str());
		pi_close(sd);
		return -1;
	}

	if (!plu_quiet && isatty(fileno(stdout))) {
		printf("connected!\n\n");
	}

	if (dlp_ReadSysInfo(sd, &sys_info) < 0) {
		fprintf(stderr, "\n   Error read system info on %s\n", port.c_str());
		pi_close(sd);
		return -1;
	}

	dlp_OpenConduit(sd);

    if (sd < 0)
		return 0;

	if (dlp_OpenConduit(sd) < 0) {
	    pi_close(sd);
        return 0;
    }

	dlp_ReadUserInfo(sd, &User);
	dlp_OpenConduit(sd);

	/* Open the Datebook's database, store access handle in db */
	if (dlp_OpenDB(sd, 0, 0x80 | 0x40, "DatebookDB", &db) < 0) {
		fprintf(stderr,"   ERROR: Unable to open DatebookDB on Palm\n");
		dlp_AddSyncLogEntry(sd, (char*)"Unable to open DatebookDB.\n");
        // (char*) is a little unsafe, but function does not edit the string
	    pi_close(sd);
        return 0;
	}


/// data transfer bit?
			pack_Appointment(&appointment, Appointment_buf, datebook_v1);

			dlp_WriteRecord(sd, db, 0, 0, 0,
					Appointment_buf->data,
					Appointment_buf->used, 0);



	/* Close the database */
	dlp_CloseDB(sd, db);

	/* Tell the user who it is, with a different PC id. */
	User.lastSyncPC 	= 0x00010000;
	User.successfulSyncDate = time(NULL);
	User.lastSyncDate 	= User.successfulSyncDate;
	dlp_WriteUserInfo(sd, &User);


	if (dlp_AddSyncLogEntry(sd, (char*)"Successfully wrote Appointment to Palm.\n" 
				"Thank you for using pilot-link.\n") < 0) {
                // (char*) is a little unsafe, but function does not edit the string
	    pi_close(sd);
        return 0;
    }
    

    if(dlp_EndOfSync(sd, 0) < 0)  {
	    pi_close(sd);
        return 0;
    }

std::cout << "probably just hanging in libusb now..." << std::endl;

	if(pi_close(sd) < 0) {
		std::cout << "error closing socket to plam pilot" << std::endl;
        // TODO: hangs on pi_close
    }

    return 0;
}
