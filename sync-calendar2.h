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

#include <string>

#include <libical/ical.h>

// fail to exit if this config item can't be read
#define FAIL_CFG(LABEL, VAR) if (!cfg.lookupValue(#LABEL, VAR)) { \
    std::cerr << "No "#LABEL" setting in configuration file, failing." << std::endl; \
    return EXIT_FAILURE; } else { \
    std::cout << "    Config "#LABEL": " << VAR << std::endl; }
// assume default value of this item can't be read
#define NON_FAIL_CFG(LABEL, VAR) if (!cfg.lookupValue(#LABEL, VAR)) \
    { std::cerr << "No "#LABEL" setting, assuming " << VAR << "." << std::endl; } else { \
    std::cout << "    Config "#LABEL": " << VAR << std::endl; }

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

// there's some bug with pilot-link and libusb now that presents as pilot-link hanging
// it looks like this might be a race condition with a mutex staying locked

int pi_close_fixed(int sd) {
    std::cout << "    Closing connection... " << std::flush;

    // close the palm's connection
    dlp_EndOfSync(sd, 0);

    std::cout << "disconnecting... " << std::flush;

    // work around for hanging on close due (probably) a race condition closing out libusb
    bool failed = false;
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
        std::cout << std::endl << "    WARNING probably hanging on a libusb race condition now..." << std::endl << std::flush;
    }
    // the glorious one line version without error handling
    // libusb_unlock_events((((usb_dev_handle*)((pi_usb_data_t *)find_pi_socket(sd)->device->data)->ref)->handle)->dev->ctx);

    // close the link to the palm
    if (pi_close(sd) < 0) {
        std::cout << std::endl << "    ERROR closing socket to plam pilot" << std::endl << std::flush;
        return -1;
    }

    std::cout << "done!" << std::endl << std::flush;

    return 0;
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

// there are a few times when BYDAY and BYMONTH aren't supported in the palm calendar
// provide a warning and mark to not copy
#define UNSUPPORTED_ICAL(VAR, LABEL) if (recur.VAR[0] != ICAL_RECURRENCE_ARRAY_MAX) { failed = true; std::cout << "        WARNING unsupported "#LABEL", won't copy!" << std::endl; }
// sometimes more than one value is suggested by the ical when palm only supports one
#define UNSUPPORTED_ICAL1(VAR, LABEL) if (recur.VAR[1] != ICAL_RECURRENCE_ARRAY_MAX) { failed = true; std::cout << "        WARNING unsupported "#LABEL", won't copy!" << std::endl; }
