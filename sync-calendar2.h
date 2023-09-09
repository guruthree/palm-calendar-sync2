// TODO: license

#include <string>

#include <libical/ical.h>

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
//#define TIME_STRING(VAR) if (icaltime_is_date(VAR)) { strftime(buf, sizeof buf, "%FT", &VAR ## _tm); } else { \
//    strftime(buf, sizeof buf, "%FT%TZ", &VAR ## _tm); }

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

// convert a icalrecurrencetype_weekday to int for pilot-link
int weekday2int(icalrecurrencetype_weekday day) {
    switch (day) {
        case ICAL_SUNDAY_WEEKDAY:
            return 0;
        case ICAL_MONDAY_WEEKDAY:
            return 1;
        case ICAL_TUESDAY_WEEKDAY:
            return 2;
        case ICAL_WEDNESDAY_WEEKDAY:
            return 3;
        case ICAL_THURSDAY_WEEKDAY:
            return 4;
        case ICAL_FRIDAY_WEEKDAY:
            return 5;
        case ICAL_SATURDAY_WEEKDAY :
            return 6;
        default:
            return 1;
    }
}

// pilot-link DayOfMonthType converted to string for checking output
const char* DayOfMonthString[35] = {
	"dom1stSun", "dom1stMon", "dom1stTue", "dom1stWen", "dom1stThu",
	"dom1stFri",
	"dom1stSat",
	"dom2ndSun", "dom2ndMon", "dom2ndTue", "dom2ndWen", "dom2ndThu",
	"dom2ndFri",
	"dom2ndSat",
	"dom3rdSun", "dom3rdMon", "dom3rdTue", "dom3rdWen", "dom3rdThu",
	"dom3rdFri",
	"dom3rdSat",
	"dom4thSun", "dom4thMon", "dom4thTue", "dom4thWen", "dom4thThu",
	"dom4thFri",
	"dom4thSat",
	"domLastSun", "domLastMon", "domLastTue", "domLastWen", "domLastThu",
	"domLastFri",
	"domLastSat"
};
