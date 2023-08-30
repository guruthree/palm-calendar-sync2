#include <iostream>
#include <string>

#include <libical/ical.h>
#include <curl/curl.h>
#include <libpisock/pi-datebook.h>

// sync-calendar attempt to read ical file and write to palm pilot
// if we're really good also try to not repeat any event that already exists


// have libcurl output stored in a std::string
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



int main() {

//    std::cout << "hellow rodl!\n";

    std::string s;

    CURL *curl;
    CURLcode res;

    curl_global_init(CURL_GLOBAL_DEFAULT);

    curl = curl_easy_init();
    if(curl) {
        curl_easy_setopt(curl, CURLOPT_URL, "https://localhost/basic.ics");
//        curl_easy_setopt(curl, CURLOPT_URL, "https://localhost/holidays.ics");

        // reduced security
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYPEER, 0L);
        curl_easy_setopt(curl, CURLOPT_SSL_VERIFYHOST, 0L);

        /* cache the CA cert bundle in memory for a week */
        curl_easy_setopt(curl, CURLOPT_CA_CACHE_TIMEOUT, 604800L);

        curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, CurlWrite_CallbackFunc_StdString);
        curl_easy_setopt(curl, CURLOPT_WRITEDATA, &s);

        /* Perform the request, res will get the return code */
        res = curl_easy_perform(curl);

        /* Check for errors */
        if(res != CURLE_OK)
          fprintf(stderr, "curl_easy_perform() failed: %s\n",
                  curl_easy_strerror(res));

        /* always cleanup */
        curl_easy_cleanup(curl);
    }

    curl_global_cleanup();
 

//    std::cout << s;
   
    icalcomponent* component;

    component = icalparser_parse_string(s.c_str());

//std::cout << component;

    if (component != 0) {
//        // print the parsed component
//        printf("%s", icalcomponent_as_ical_string(component));
//        icalcomponent_free(component);


        icalcomponent *c;

        for(c = icalcomponent_get_first_component(component, ICAL_ANY_COMPONENT);
            c != 0;
            c = icalcomponent_get_next_component(component, ICAL_ANY_COMPONENT))
        {

std::cout << "new component" << std::endl;

            icalproperty *p;
        icalparameter *p2;


            for(p = icalcomponent_get_first_property(c, ICAL_ANY_PROPERTY);
                p != 0;
                p = icalcomponent_get_next_property(c, ICAL_ANY_PROPERTY))
            {


std::cout  << "p" << icalproperty_get_property_name(p) << std::endl;
std::cout << icalproperty_get_value_as_string(p) << std::endl;


            for(p2 = icalproperty_get_first_parameter(p, ICAL_ANY_PARAMETER);
                p2 != 0;
                p2 = icalproperty_get_next_parameter(p, ICAL_ANY_PARAMETER))
            {


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
  


std::cout << "p2" << propname << std::endl;
if (val != nullptr)
    std::cout << val << std::endl;

//std::cout << "p2" << icalparameter_get_iana_name(p2) << std::endl;
//std::cout << icalparameter_get_iana_value(p2) << std::endl;
//std::cout << "p2" << icalparameter_get_xname(p2) << std::endl;
//std::cout << icalparameter_get_xvalue(p2) << std::endl;

//        icalparameter_free(p2);

            }


//        icalproperty_free(p);

std::cout << std::endl;

            }



//        icalcomponent_free(c);



std::cout << std::endl;

// also parameters to iterate through?

        } 


    }


/*    icalcomponent *component;
    icalparser *parser = icalparser_new();


    // associate the FILE with the parser so that read_stream
    // will have access to it
    icalparser_set_gen_data(parser, s);

    // parse the opened file
    component = icalparser_parse(parser, read_stream);

    if (component != 0) {
        // print the parsed component
        printf("%s", icalcomponent_as_ical_string(component));
        icalcomponent_free(component);
    }

    icalparser_free(parser); */


    return 0;
}
