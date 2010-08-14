#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#include <sys/lkm.h>
#include <sys/device.h>

#include <dev/usb/usb.h>
#include <dev/usb/usb_port.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>
#include "wimax.h"

CFDRIVER_DECL(wimax, DV_DULL, NULL);
USB_DECLARE_DRIVER(wimax);

//extern struct cfdriver wimax_cd;
//extern struct cfattach wimax_ca;

/* locators: port, config, iface, vendor, product, release */
static int usbdevloc[] = { -1, -1, -1, -1, -1, -1 }; 


static struct cfparent uhubparent = {
	        "usbdevif", "uhub", DVUNIT_ANY
};

static struct cfdata wimax_cfdata[] = {
	        {"wimax", "wimax", 0, FSTATE_STAR, usbdevloc, 0, &uhubparent},
			{ 0 }
};

static struct cfdriver *wimax_cfdrivers[] = {
	        &wimax_cd,
			NULL
};
static struct cfattach *wimax_cfattachs[] = {
	        &wimax_ca,
			NULL
};

static const struct cfattachlkminit wimax_cfattachinit[] = {
	        { "wimax", wimax_cfattachs },
			{ NULL }
};

static struct wimax_type wimax_devs[] = {
	{ 0x04e8, 0x6761 },
	{ 0x04e9, 0x6761 },
};

int wimax_lkmentry(struct lkm_table *, int, int);
MOD_DRV("wimax", wimax_cfdrivers, wimax_cfattachinit, wimax_cfdata);

USB_MATCH(wimax)
{
	int i;

	printf("-->wimax_match\n");

	USB_MATCH_START(wimax, uaa);

	printf("passed vid = %x, pid = %x\n", uaa->vendor, uaa->product);

	for (i = 0; i < __arraycount(wimax_devs); i++) {
		struct wimax_type *t = &wimax_devs[i];

		if (uaa->vendor == t->wimax_vid &&
				uaa->product == t->wimax_pid) {
			printf("Device match!\n");
			return(UMATCH_VENDOR_PRODUCT);
		}       
	}
	return (UMATCH_NONE);
}

USB_ATTACH(wimax)
{
	USB_ATTACH_START(wimax, sc, uaa);
	char                *devinfop;
	usbd_status         err;
	usbd_device_handle      dev = uaa->device;
	u_int8_t            mode, channel;
	int i;

	printf("-->wimax_attach\n");

	sc->wimax_dev = self;
	sc->sc_state = WIMAX_S_UNCONFIG;

	devinfop = usbd_devinfo_alloc(dev, 0);
	USB_ATTACH_SETUP;
	aprint_normal_dev(self, "%s\n", devinfop);
	usbd_devinfo_free(devinfop);

	err = usbd_set_config_no(dev, WIMAX_CONFIG_NO, 1);
	if (err) {
		aprint_error_dev(self, "setting config no failed\n");
		USB_ATTACH_ERROR_RETURN;
	}

	err = usbd_device2interface_handle(dev, WIMAX_IFACE_IDX, &sc->wimax_iface);
	if (err) {
		aprint_error_dev(self, "getting interface handle failed\n");
		USB_ATTACH_ERROR_RETURN;
	}

	USB_ATTACH_SUCCESS_RETURN;
}

USB_DETACH(wimax)
{
	printf("-->wimax_detach\n");

	return (0);
}

int wimax_activate(device_ptr_t self, enum devact act)
{
	printf("-->wimax_activate\n");

	return (0);
}

int
wimax_lkmentry(struct lkm_table *lkmtp, int cmd, int ver)
{
	        DISPATCH(lkmtp, cmd, ver, lkm_nofunc, lkm_nofunc, lkm_nofunc);
}

