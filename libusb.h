#include <atomic>
#include <libusb-1.0/libusb.h>

// from libusb-compat usbi.h
struct usb_dev_handle {
	libusb_device_handle *handle;
	struct usb_device *device;
	int last_claimed_interface;
};

// from libusb thread_posix.h
typedef pthread_mutex_t usbi_mutex_t;
// from libusb libusbi.h
typedef std::atomic_long usbi_atomic_t;
struct list_head {
	struct list_head *prev, *next;
};
struct libusb_device {
	usbi_atomic_t refcnt;
	struct libusb_context *ctx;
	struct libusb_device *parent_dev;
	uint8_t bus_number;
	uint8_t port_number;
	uint8_t device_address;
	enum libusb_speed speed;
	struct list_head list;
	unsigned long session_data;
	struct libusb_device_descriptor device_descriptor;
	usbi_atomic_t attached;
};
struct libusb_device_handle {
	usbi_mutex_t lock;
	unsigned long claimed_interfaces;
	struct list_head list;
	struct libusb_device *dev;
	int auto_detach_kernel_driver;
};
#define DEVICE_CTX(dev)		((dev)->ctx)
#define HANDLE_CTX(handle)	((handle) ? DEVICE_CTX((handle)->dev) : NULL)
