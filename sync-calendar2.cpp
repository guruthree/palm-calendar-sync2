// TODO: copyright

#include <cstring>
#include <iostream>
#include <string>

#include <libical/ical.h>
#include <curl/curl.h>
#include <libpisock/pi-datebook.h>

// sync-calendar attempt to read ical file and write to palm pilot
// if we're really good also try to not repeat any event that already exists


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
int main() {

    std::string icaldata; // the downloaded ical data

    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    if(curl) {
//        curl_easy_setopt(curl, CURLOPT_URL, "https://localhost/basic.ics");
//        curl_easy_setopt(curl, CURLOPT_URL, "https://localhost/holidays.ics");
        curl_easy_setopt(curl, CURLOPT_URL, "https://localhost/oneevent.ics");

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
            std::cout << "curl_easy_perform() failed: " << curl_easy_strerror(res) << std::endl;
            // TODO: handle this nicely
        }
std::cout << res;
        // always cleanup!
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
 

    // ical specifies components, properties, values, and parameters and I think there's
    // a bit of a metaphor relating that to HTML, with COMPONENTS corresponding to an
    // opening BEGIN tag which contains PROPERTY tags that then have VALUES (the main
    // thing in that tag, and then PARAMETERS which are kind of like HTML attributes,
    // adding extra information about the PARAMETER
    
    // parse the string into a series of components to iterate through
    icalcomponent* components = icalparser_parse_string(icaldata.c_str());

    // remove errors (which also includes empty descriptions, locations, and the like) 
    icalcomponent_strip_errors(components);

    // if there is some valid ical data to work through
    if (components != nullptr) {

        // the specific component we're on in our iteration
        icalcomponent *c;

        // TODO: google calendar output might have some time-zone definitions at the
        // start via X-LIC-LOCATION if we accept ICAL_ANY_COMPONENT
        for(c = icalcomponent_get_first_component(components, ICAL_VEVENT_COMPONENT); c != 0;
                c = icalcomponent_get_next_component(components, ICAL_VEVENT_COMPONENT)) {


//                if (icalcomponent_isa(c) == ICAL_VEVENT_COMPONENT) {

            // TODO: extract alarm information with icalcomponent_get_first_component and ICAL_VALARM_COMPONENT

            std::cout << "new component" << std::endl;


            struct     Appointment appointment;

//            icalproperty *p; // there will definetely be properties

// palm only has start time, end time, alarm, repeat, description, and note
// so we only need to extract those things from the component if they're there
// every event should have a DTSTART, DTEND, and DTSUMMARY (which is palm description)
// palm won't take an event with a description
// merge location and description into the note (palmos5 has a location but pilot-link doesn't support it)


            std::string summary(icalcomponent_get_summary(c));
            std::cout << "    Summary: " << summary << std::endl;

//            const char* summary = icalcomponent_get_summary(c);
//            if (summary != nullptr)
//                std::cout << "    Summary: " << summary << std::endl;
//            else
//                continue; // no summary, no go

//            const char* description = icalcomponent_get_description(c);
//            if (description != nullptr)
//                std::cout << "    Description: " << description << std::endl;

//            const char* location = icalcomponent_get_location(c);
//            if (location != nullptr)
//                std::cout << "    Location: " << location << std::endl;

            char buf[200];
            char notebuf[2000];

            icaltimetype start = icalcomponent_get_dtstart(c);
            time_t start_time_t = icaltime_as_timet_with_zone(start, icaltime_get_timezone(start));
            tm *start_tm = gmtime(&start_time_t);
            if (icaltime_is_date(start)) { // no time, so whole day
                strftime(buf, sizeof buf, "%FT", start_tm);
            }
            else {
                strftime(buf, sizeof buf, "%FT%TZ", start_tm);
            }
            std::cout << "    Start:" << buf << std::endl;

            icaltimetype end = icalcomponent_get_dtend(c);
            time_t end_time_t = icaltime_as_timet_with_zone(end, icaltime_get_timezone(end));
            tm *end_tm = gmtime(&end_time_t);
            if (icaltime_is_date(end)) {
                strftime(buf, sizeof buf, "%FT", end_tm);
            }
            else {
                strftime(buf, sizeof buf, "%FT%TZ", end_tm);
            }
            std::cout << "    End:" << buf << std::endl;

if (icaltime_is_date(start) && icaltime_is_date(end))
    appointment.event = 1;
else
    appointment.event = 0;

appointment.begin = *start_tm;
appointment.end = *end_tm;

//strcpy(buf, summary);
char buf2[summary.length()];
strcpy(buf2, summary.c_str());

appointment.alarm         = 0;
appointment.advance       = 0;
appointment.advanceUnits  = 0;
appointment.repeatType         = repeatNone;
appointment.repeatForever      = 0;
appointment.repeatEnd.tm_mday  = 0;
appointment.repeatEnd.tm_mon   = 0;
appointment.repeatEnd.tm_wday  = 0;
appointment.repeatFrequency    = 0;
appointment.repeatWeekstart    = 0;
appointment.exceptions         = 0;
appointment.exception          = NULL;
appointment.description        = buf;
appointment.note               = NULL;

/*            for(p = icalcomponent_get_first_property(c, ICAL_ANY_PROPERTY); p != 0;
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

            std::cout << "end component" << std::endl;
//} // kind
        } // for c
        if (c != nullptr) icalcomponent_free(c);

        icalcomponent_free(components); // already not null by definition inside this loop

    } // if components

    return 0;
}
