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
        curl_easy_setopt(curl, CURLOPT_URL, "https://localhost/basic.ics");
//        curl_easy_setopt(curl, CURLOPT_URL, "https://localhost/holidays.ics");

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
            fprintf(stderr, "curl_easy_perform() failed: %s\n", curl_easy_strerror(res));
            // TODO: handle this nicely
        }

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
        // start via X-LIC-LOCATION if we accept any any component
        for(c = icalcomponent_get_first_component(components, ICAL_ANY_COMPONENT); c != 0;
                c = icalcomponent_get_next_component(components, ICAL_ANY_COMPONENT)) {

            std::cout << "new component" << std::endl;

            icalproperty *p; // there will definetely be properties

            for(p = icalcomponent_get_first_property(c, ICAL_ANY_PROPERTY); p != 0;
                    p = icalcomponent_get_next_property(c, ICAL_ANY_PROPERTY)) {


                std::cout << "  " << icalproperty_get_property_name(p) << std::endl;
                std::cout << "  " << icalproperty_get_value_as_string(p) << std::endl;

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
                        std::cout << "    " << val << std::endl;
                    }

                } // for p2
                if (p2 != nullptr) icalparameter_free(p2);

            } // for p
            if (p != nullptr) icalproperty_free(p);

            std::cout << "end component" << std::endl;

        } // for c
        if (c != nullptr) icalcomponent_free(c);

        icalcomponent_free(components); // already not null by definition inside this loop

    } // if components

    return 0;
}
