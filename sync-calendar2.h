// TODO: license

#include <string>

// fail to exit if this config item can't be read
#define FAIL_CFG(LABEL, VAR) if (!cfg.lookupValue(#LABEL, VAR)) { \
    std::cerr << "No "#LABEL" setting in configuration file, failing." << std::endl; \
    return EXIT_FAILURE; } else { \
    std::cout << "Config "#LABEL": " << VAR << std::endl; }
// assume default value of this item can't be read
#define NON_FAIL_CFG(LABEL, VAR) if (!cfg.lookupValue(#LABEL, VAR)) \
    { std::cerr << "No "#LABEL" setting, assuming " << VAR << "." << std::endl; } else { \
    std::cout << "Config "#LABEL": " << VAR << std::endl; }

// convert a tm to a nice string
// where there's no time it's a whole day
#define TIME_STRING(VAR) if (icaltime_is_date(VAR)) { strftime(buf, sizeof buf, "%FT", &VAR ## _tm); } else { \
    strftime(buf, sizeof buf, "%FT%TZ", &VAR ## _tm); }

// callback for having curl store output in a std::string
// https://stackoverflow.com/questions/2329571/c-libcurl-get-output-into-a-string
size_t CurlWrite_CallbackFunc_StdString(void *contents, size_t size, size_t nmemb, std::string *s) {
    size_t newLength = size*nmemb;
    try {
        s->append((char*)contents, newLength);
    }
    catch (std::bad_alloc &e) {
        //handle memory problem
        return 0;
    }
    return newLength;
}

