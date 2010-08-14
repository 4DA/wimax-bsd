 
#ifdef _KERNEL

#define WIMAX_ENDPT_RX        0x0
#define WIMAX_ENDPT_TX        0x1
#define WIMAX_ENDPT_MAX 2

#define MAC_LEN 12					//MAC address length

struct wimax_softc {
	USBBASEDEVICE wimax_dev;
	#define WIMAX_S_OK        	1
	#define WIMAX_S_UNCONFIG      2
	#define MAC_OFFSET	12			//offset of MAC in packet

	struct ethercom sc_ec;
	struct mii_data wimax_mii;

	char sc_state;
	usbd_device_handle  wimax_udev;
	usbd_interface_handle   wimax_iface;
	int wimax_unit;

	kmutex_t wimax_mii_lock; //TODO
	u_int8_t        wimax_link;

	int wimax_refcnt;

	struct lwp      *wimax_thread;
	int         wimax_closing;
	kcondvar_t      wimax_domc;
	kcondvar_t      wimax_closemc;
	kmutex_t        wimax_mcmtx;

	usb_callout_t wimax_stat_ch;

	char net_found;

	struct usb_task wimax_tick_task;
	int wimax_ed[WIMAX_ENDPT_MAX];
	usbd_pipe_handle rx_pipe, tx_pipe;
	unsigned int bulk_in_len;

	#define RECV_BUFLEN 4000
	char recv_buff[RECV_BUFLEN];

	unsigned int info_updated;
	unsigned char proto_flags;
	char chip[0x40];
	char firmware[0x40];
	unsigned char mac[6];
	int link_status;
	short rssi;
	float cinr;
	unsigned char bsid[6];
	unsigned short txpwr;
	unsigned int freq;
	int state;
};

struct wimax_device_info {
	unsigned int info_updated;
	unsigned char proto_flags;
	char chip[0x40];
	char firmware[0x40];
	unsigned char mac[6];
	int link_status;
	short rssi;
	float cinr;
	unsigned char bsid[6];
	unsigned short txpwr;
	unsigned int freq;
	int state;
};

struct wimax_type {
	u_int16_t       wimax_vid;
	u_int16_t       wimax_pid;
};

struct u200_buf_head
{
	unsigned char  head;
	unsigned char  type;
	unsigned short length;
};

char *wimax_states[] = {"INIT", "SYNC", "NEGO", "NORMAL", "SLEEP", "IDLE", "HHO", "FBSS", "RESET", "RESERVED", "UNDEFINED", "BE", "NRTPS", "RTPS", "ERTPS", "UGS", "INITIAL_RNG", "BASIC", "PRIMARY", "SECONDARY", "MULTICAST", "NORMAL_MULTICAST", "SLEEP_MULTICAST", "IDLE_MULTICAST", "FRAG_BROADCAST", "BROADCAST", "MANAGEMENT", "TRANSPORT"};

#define sc_if   sc_ec.ec_if
#define GET_IFP(sc) (&(sc)->wimax_ec.ec_if)
#define GET_MII(sc) (&(sc)->wimax_mii)

#define RPLY_HEADER(_buf) (char) _buf[0]

#define WIMAX_CONFIG_NO       1
#define WIMAX_IFACE_IDX       0

#define USB_HOST_SUPPORT_EXTENDED_CMD			0x01
#define USB_HOST_SUPPORT_MULTIPLE_MAC_REQ		0x02
#define USB_HOST_SUPPORT_SELECTIVE_SUSPEND		0x04
#define USB_HOST_SUPPORT_TRUNCATE_ETHERNET_HEADER	0x08
#define USB_HOST_SUPPORT_DL_SIX_BYTES_HEADER		0x10
#define USB_HOST_SUPPORT_UL_SIX_BYTES_HEADER		0x20
#define USB_HOST_SUPPORT_DL_MULTI_PACKETS		0x40
#define USB_HOST_SUPPORT_UL_MULTI_PACKETS		0x80

#define MAX_PACKET_LEN		0x4000

/* up to  MAXWIMAXDEVS minor devices */
#define	MAXWIMAXDEVS	1

#define WIMAX_DEFAULT_MTU 1500
#define HEADER_LENGTH_LOWLEVEL  4
#define HEADER_LENGTH       6

#endif /* _KERNEL */
