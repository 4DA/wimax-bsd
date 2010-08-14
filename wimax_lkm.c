#include <sys/param.h>
#include <sys/systm.h>
#include <sys/kernel.h>

#include <sys/lkm.h>
#include <sys/device.h>
#include <sys/malloc.h>
#include <net/if.h>
#include <net/if_dl.h>
#include <net/if_media.h>
#include <net/if_ether.h>

#ifdef INET
#include <netinet/in.h>
#include <netinet/if_ether.h>
#endif

#include <dev/usb/usb.h>
#include <dev/usb/usb_port.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdi_util.h>
#include <dev/usb/usbdevs.h>


#include <dev/usb/usb.h>
#include <dev/usb/usbhid.h>
#include <dev/usb/usbdi.h>
#include <dev/usb/usbdevs.h>

#include <dev/mii/mii.h>
#include <dev/mii/miivar.h>


#include "wimax.h"

#ifdef WIMAX_DEBUG
#define DPRINTF(x)  do { if (ehcidebug) printf x; } while(0)
#define DPRINTFN(n,x)   do { if (ehcidebug>(n)) printf x; } while (0)
int ehcidebug = 0;
#ifndef __NetBSD__
#define bitmask_snprintf(q,f,b,l) snprintf((b), (l), "%b", (q), (f))
#endif
#else
#define DPRINTF(x)
#define DPRINTFN(n,x)
#endif

CFDRIVER_DECL(wimax, DV_DULL, NULL);
USB_DECLARE_DRIVER(wimax);

struct wimax_softc *dev;

void wimax_start(struct ifnet *);
int wimax_init(struct ifnet *);
void wimax_stop(struct ifnet *, int);
void wimax_watchdog(struct ifnet *);
int wimax_ioctl(struct ifnet *ifp, u_long command, void *data);

usbd_status wimax_async_read(struct wimax_softc *sc);
usbd_status wimax_usb_write(struct wimax_softc *sc, u_int16_t length, u_int8_t *data);


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

static void fill_C_req(unsigned char *buf, int len)
{
     len -= 6;
     buf[0x00] = 0x57;
     buf[0x01] = 0x43;
     buf[0x02] = len & 0xff;
     buf[0x03] = (len >> 8) & 0xff;
     memset(buf + 6, 0, 12);
}

static int fill_normal_C_req(unsigned char *buf, unsigned short type_a, unsigned short type_b, unsigned short param_len, unsigned char *param)
{
     int len = 0x1a + param_len;
     fill_C_req(buf, len);
     buf[0x04] = 0x15;
     buf[0x05] = 0x00;
     buf[0x12] = 0x15;
     buf[0x13] = 0x00;
     buf[0x14] = type_a >> 8;
     buf[0x15] = type_a & 0xff;
     buf[0x16] = type_b >> 8;
     buf[0x17] = type_b & 0xff;
     buf[0x18] = param_len >> 8;
     buf[0x19] = param_len & 0xff;
     memcpy(buf + 0x1a, param, param_len);
     return len;
}

int fill_string_info_req(unsigned char *buf)
{
     return fill_normal_C_req(buf, 0x8, 0x1, 0x0, NULL);
}
int fill_init1_req(unsigned char *buf)
{
     unsigned char param[0x2] = {0x0, 0x1};
     return fill_normal_C_req(buf, 0x30, 0x1, sizeof(param), param);
}

int fill_mac_req(unsigned char *buf)
{
     return fill_normal_C_req(buf, 0x3, 0x1, 0x0, NULL);
}

int fill_init2_req(unsigned char *buf)
{
     return fill_normal_C_req(buf, 0x20, 0x8, 0x0, NULL);
}

int fill_init3_req(unsigned char *buf)
{
     return fill_normal_C_req(buf, 0x20, 0xc, 0x0, NULL);
}
int fill_authorization_data_req(unsigned char *buf)
{
     unsigned char param[0xd] = {0x00, 0x10, 0x00, 0x09, 0x40, 0x79, 0x6f, 0x74, 0x61, 0x2e, 0x72, 0x75, 0x00};
     return fill_normal_C_req(buf, 0x20, 0x20, sizeof(param), param);
}

int fill_outgoing_packet_header(unsigned char *buf, int len, int body_len)
{
     buf[0x00] = 0x57;
     buf[0x01] = 0x44;
     buf[0x02] = body_len & 0xff;
     buf[0x03] = (body_len >> 8) & 0xff;
     return body_len + 6;
}

inline void fill_config_req(unsigned char *buf, int body_len)
{
     fill_outgoing_packet_header(buf, 0x43, body_len);
     memset(buf + HEADER_LENGTH, 0, 12);
}

static int fill_normal_config_req(unsigned char *buf, unsigned short type_a, unsigned short type_b, unsigned short param_len, unsigned char *param) {
     int body_len = 0x14 + param_len;
     fill_config_req(buf, body_len);
     buf[0x04] = 0x15;
     buf[0x05] = 0x00;
     buf += HEADER_LENGTH;
     buf[0x0c] = 0x15;
     buf[0x0d] = 0x00;
     buf[0x0e] = type_a >> 8;
     buf[0x0f] = type_a & 0xff;
     buf[0x10] = type_b >> 8;
     buf[0x11] = type_b & 0xff;
     buf[0x12] = param_len >> 8;
     buf[0x13] = param_len & 0xff;
     memcpy(buf + 0x14, param, param_len);
     return HEADER_LENGTH + body_len;
}
	
int fill_init_cmd(unsigned char *buf)
{
     fill_config_req(buf, 0x12);
     buf[0x04] = 0x15;
     buf[0x05] = 0x04;
     buf += HEADER_LENGTH;
     buf[0x0c] = 0x15;
     buf[0x0d] = 0x04;
     buf[0x0e] = 0x50;
     buf[0x0f] = 0x04;
     return HEADER_LENGTH + 0x12;
}
		


int fill_diode_control_cmd(unsigned char *buf, int turn_on)
{
     unsigned char param[0x2] = {0x0, turn_on ? 0x1 : 0x0};
     return fill_normal_config_req(buf, 0x30, 0x1, sizeof(param), param);
}


int fill_protocol_info_req(unsigned char *buf, int len, unsigned char flags)
{
     buf[0x00] = 0x57;
     buf[0x01] = 0x45;
     buf[0x02] = 0x04;
     buf[0x03] = 0x00;
     buf[0x04] = 0x00;
     buf[0x05] = 0x02;
     buf[0x06] = 0x00;
     buf[0x07] = flags;
     return 8;
}

int fill_mac_lowlevel_req(unsigned char *buf)
{
     fill_outgoing_packet_header(buf, 0x50, 0x14);
     buf += HEADER_LENGTH_LOWLEVEL;
     memset(buf, 0, 0x14);
     buf[0x0c] = 0x15;
     buf[0x0d] = 0x0a;
     return HEADER_LENGTH_LOWLEVEL + 0x14;
}

int fill_find_network_req(unsigned char *buf, unsigned short level)
{
     unsigned char param[0x2] = {level >> 8, level & 0xff};
     return fill_normal_C_req(buf, 0x1, 0x1, sizeof(param), param);
}
int fill_state_req(unsigned char *buf)
{
     return fill_normal_C_req(buf, 0x1, 0xb, 0x0, NULL);
}
int fill_connection_params2_req(unsigned char *buf)
{
     unsigned char param[0x2] = {0x00, 0x00};
     return fill_normal_C_req(buf, 0x1, 0x9, sizeof(param), param);
}

int update_network(struct wimax_softc* sc)
{
     char buf[2000];
     int ret,len;

     //   static int code = 0;
//     code = ! code;

     printf("-->update_network\n");

     if(!sc->net_found)
     {
	  len = fill_find_network_req(buf,2);
	  
	  if (len < 0)
	       return len;
	  
	  ret = wimax_usb_write(sc, len, buf);

//	  len = fill_diode_control_cmd(buf, code);
//	  wimax_usb_write(sc, len, buf);

//	  len=fill_mac_req(buf);
//	  wimax_usb_write(sc, len, buf);

	  if (ret < 0)
	       printf("wimax:update_network:can`t write message %i\n",ret);
     }
     
     else
     {
	  len = fill_connection_params2_req(buf);
	  
	  if (len < 0)
	       return len;
	  
	  ret = wimax_usb_write(sc, len, buf);

	  if (ret < 0)
	       printf("wimax:update_network:can`t write message %i\n",ret);
	  
	  len = fill_state_req(buf);
	  
	  if (len < 0)
 	       return len;
	  
	  ret = wimax_usb_write(sc, len, buf);
	  
	  if (ret < 0)
	       printf("wimax:update_network:can`t write message %i\n",ret);
     }
 
     return ret;
}


Static int process_normal_C_response(struct wimax_softc *dev, const unsigned char *buf, size_t len)
{
     short type_a = (buf[0x14] << 8) + buf[0x15];
     short type_b = (buf[0x16] << 8) + buf[0x17];
     short param_len = (buf[0x18] << 8) + buf[0x19];

     if (type_a == 0x8 && type_b == 0x2) {
	  if (param_len != 0x80) {
	       printf("U200:process_normal_C_response:bad param_len\n");
	       return -EIO;

	  }
	  memcpy(dev->chip, buf + 0x1a, 0x40);
	  memcpy(dev->firmware, buf + 0x5a, 0x40);
	  printf("chip=%s,firmware=%s\n",dev->chip,dev->firmware);
	  return 0;
     }

     if (type_a == 0x3 && type_b == 0x2) {
	  if (param_len != 0x6) {
	       printf("U200:process_normal_C_response:bad param_len\n");
	       return -EIO;
	  }
	  
	  memcpy(dev->mac,buf+0x1a,0x6);
	  printf("device MAC = %s\n", ether_sprintf(dev->mac));
	  //  printf("U200:registering network device\n");
	  /*
	    if(register_netdev(dev->net)!=0)
	    {
	    printf("error registering netdev \n");
	    return -EIO;
	    }
	  */

	  //		printf("net->dev_addr=%x:%x:%x:%x:%x:%x\n",dev->net->dev_addr[0],dev->net->dev_addr[1],dev->net->dev_addr[2],dev->net->dev_addr[3],dev->net->dev_addr[4],dev->net->dev_addr[5]);
	  return 0;
     }							

     if (type_a == 0x1 && type_b == 0x2) {
	  if (param_len != 0x2) {
	       printf( "U200:process_normal_C_response:bad param_len\n");
	       return -EIO;
	  }
	  dev->net_found = (buf[0x1a] << 8) + buf[0x1b];
	  printf("U200:%i nets found\n",dev->net_found);
	  return 0;
     }
       
     if (type_a == 0x1 && type_b == 0x3) {
	  if (param_len != 0x4) {
	       printf( "U200:process_normal_C_response:bad param_len\n");
	       return -EIO;
	  }
	  dev->net_found = 0;
	  printf("U200:net lost\n");
	  return 0;
     }
     
     if (type_a == 0x1 && type_b == 0xa) {
	  if (param_len != 0x16) {
	       printf("U200:process_normal_C_response:bad param_len\n");
	       return -EIO;
	  }

	  int qual, level, noise, updated, txpwr, freq;

	  /*
	    dev->iwstat.qual.qual=92+buf[0x1b];
	    dev->iwstat.qual.level =buf[0x1b];
	    dev->iwstat.qual.noise  = buf[0x1b]-buf[0x1d];
	    dev->iwstat.qual.updated=IW_QUAL_DBM|IW_QUAL_ALL_UPDATED ;
	    memcpy(dev->bsid, buf + 0x1e, 0x6);
	    dev->txpwr = (buf[0x26] << 8) + buf[0x27];
	    dev->freq = (buf[0x28] << 24) + (buf[0x29] << 16) + (buf[0x2a] << 8) + buf[0x2b];
	  */
	  
	  qual=92+buf[0x1b];
	  level =buf[0x1b];
	  dev->cinr = (float)((short)((buf[0x1c] << 8) + buf[0x1d])) / 8;
	  noise  = buf[0x1b]-buf[0x1d];
	  //updated=IW_QUAL_DBM|IW_QUAL_ALL_UPDATED ;
	  memcpy(dev->bsid, buf + 0x1e, 0x6);
	  dev->txpwr = (buf[0x26] << 8) + buf[0x27];
	  dev->freq = (buf[0x28] << 24) + (buf[0x29] << 16) + (buf[0x2a] << 8) + buf[0x2b];

	  printf("U200: rssi:%i cinr:%i bsid:%d txpwr:%i freq:%i\n",dev->rssi,(int)dev->cinr,dev->bsid,dev->txpwr,dev->freq);
	  return 0;
     }
     
     if (type_a == 0x1 && type_b == 0xc) {
	  int oldstate;
	  if (param_len != 0x2) {
	       printf("U200:process_normal_C_response:bad param_len\n");
	       return -1;
	  }
	  oldstate=dev->state;
	  dev->state = (buf[0x1a] << 8) + buf[0x1b];
	  if(oldstate!=dev->state) {

	       printf("U200:state changed to %s(%i)\n",wimax_states[dev->state],dev->state);
	       if(dev->state==3) {
		    //union iwreq_data iwrd;
		    printf("u200:We connected!\n");
		    /*

		      netif_carrier_on(dev->net);
		      dev->net->operstate=IF_OPER_UP;
		      u200_get_wap(dev->net,NULL,(struct sockaddr *)&iwrd,NULL);
		      wireless_send_event(dev->net,SIOCGIWAP,&iwrd,NULL);
		    */
	       }
	  }
	  return 0;
     }

     printf("U200:process_normal_C_response:unknown C responce(type_a=%x type_b=%x param_len=%x)\n",type_a,type_b,param_len);
     //printf("U200:process_normal_C_response:other respone\n");
     return 0;
}

Static int process_debug_C_response(struct wimax_softc *dev, const unsigned char *buf, size_t len)
{
     printf("U200:process_debug_C_response\n");
     return 0;
}

static int process_C_response(struct wimax_softc *dev, const unsigned char *buf, size_t len)
{
     printf("U200:process_C_response\n");
	
     if (buf[0x12] != 0x15) {
	  printf( "U200:bad C response:bad format\n");
	  return -EIO;
     }
	
     switch (buf[0x13]) {
     case 0x00:
	  return process_normal_C_response(dev, buf, len);
     case 0x02:
	  return process_debug_C_response(dev, buf, len);
     case 0x04:
	  printf("U200:process_C_response:unknow type 0x04\n");
	  return 0;
     default:
	  printf( "U200:bad C response: bad format\n");
	  return -EIO;
     }

     return -EINVAL;
}


static int process_D_response(struct wimax_softc *dev, const unsigned char *buf, size_t len)
{
     printf("u200: process_D_response\n");
     /*
       struct sk_buff* skb;
       int retval=0;
       skb=dev_alloc_skb(len-6);
       if(!skb)
       {
       printf("u200:process_D_response:can`t alloc skb\n");
       return -ENOMEM;
       }
       skb_put(skb,len-6);
       memcpy(skb->data,buf+6,len-6);
       skb->protocol=eth_type_trans(skb,dev->net);
       retval=netif_rx(skb);
       if(retval!=0)
       {
       printf("u200:process_D_response:netif_rx returns=%i\n",retval);
       return retval;
       }
       dev->stats.rx_packets++;
       dev->stats.rx_bytes+=len-6;
     */
     return 0;
}

static int process_E_response(struct wimax_softc *dev, const unsigned char *buf, int len)
{
     printf("U200:process_E_response(len=%i)\n",len);
     return 0;
}

static int process_P_response(struct wimax_softc *dev, const unsigned char *buf, int len)
{
	
     printf("U200:process_P_response (len=%i)\n",len);
     return 0;
}




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

     printf("<--wimax_match\n");

     return (UMATCH_NONE);
}

usbd_status
wimax_usb_read(struct wimax_softc *sc, u_int16_t length, u_int8_t *data)
{
     usbd_xfer_handle	xfer;
     usbd_status		err;
     int			total_len = 0, s;

     s = splnet();

     xfer = usbd_alloc_xfer(sc->wimax_udev);
	
     usbd_setup_xfer(xfer, sc->rx_pipe, 0, data, length, USBD_SHORT_XFER_OK | USBD_SYNCHRONOUS,  1000, NULL); 

     err = usbd_sync_transfer(xfer);

     usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

//     printf("%s: transfered 0x%x bytes in\n",
//	    USBDEVNAME(sc->wimax_dev), total_len);

     usbd_free_xfer(xfer);

     splx(s);
     return(err);
}


usbd_status
wimax_usb_sync_write(struct wimax_softc *sc, u_int16_t length, u_int8_t *data)
{
     usbd_xfer_handle	xfer;
     usbd_status		err;
     int			total_len = 0, s;

     s = splnet();

     xfer = usbd_alloc_xfer(sc->wimax_udev);
	
     usbd_setup_xfer(xfer, sc->tx_pipe, 0, data, length, USBD_SYNCHRONOUS,  1000, NULL); 

     err = usbd_sync_transfer(xfer);

     usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

//     printf("%s: transfered 0x%x bytes out\n",
//	    USBDEVNAME(sc->wimax_dev), total_len);

     usbd_free_xfer(xfer);

     splx(s);
     return(err);
}

usbd_status
wimax_usb_write(struct wimax_softc *sc, u_int16_t length, u_int8_t *data)
{
     usbd_xfer_handle	xfer;
     usbd_status		err;
     int			total_len = 0, s;

     s = splnet();

     xfer = usbd_alloc_xfer(sc->wimax_udev);
	
     usbd_setup_xfer(xfer, sc->tx_pipe, 0, data, length, 0,  1000, NULL); 

     err = usbd_sync_transfer(xfer);

     usbd_get_xfer_status(xfer, NULL, NULL, &total_len, NULL);

//     printf("%s: transfered 0x%x bytes out\n",
//	    USBDEVNAME(sc->wimax_dev), total_len);

     usbd_free_xfer(xfer);

     splx(s);
     return(err);
}

void wimax_read_callback(usbd_xfer_handle xh, usbd_private_handle priv, usbd_status stat)
{
     char *buf;
     int count;
     struct u200_buf_head *head;

     usbd_status st;
     //printf("in callback!\n");

     if (stat == USBD_IOERROR) {
	  printf("status = IOERROR\n");
	  return;
     }

     usbd_get_xfer_status(xh, 0, (void *) &buf, &count, &st);
     //printf("count=%d\n",  count);

     head = (struct u200_buf_head *) buf;
		
     switch (head->type) {
     case 0x43:
	  process_C_response(dev, buf, count);
	  break;
     case 0x44:
	  process_D_response(dev, buf, count);
	  break;
     case 0x45:
	  process_E_response(dev, buf, count);
	  break;
     case 0x50:
	  process_P_response(dev, buf, count);
	  break;
     default:
	  printf("U200:process_response: bad response type: %02x\n", head->type);	
	  break;
     }

     wimax_async_read(dev);
}

usbd_status
wimax_async_read(struct wimax_softc *sc)
{
     usbd_xfer_handle	xfer;
     usbd_status		err;
     int			total_len = 0, s;

     s = splnet();
 
     xfer = usbd_alloc_xfer(sc->wimax_udev); 
     //printf("Async read: %d bytes requested bulk-in\n", sc->bulk_in_len);
 	
     usbd_setup_xfer(xfer, sc->rx_pipe, 0, sc->recv_buff, sc->bulk_in_len-1, 0,  5000, wimax_read_callback); 
     //usbd_bulk_transfer(xfer, sc->rx_pipe, 0, 5000, sc->recv_buff, sc->bulk_in_len, "BULK"); 

     err = usbd_transfer(xfer);

     splx(s);
     return(err);
}


void wimax_start(struct ifnet *ifp)
{
     printf("--> wimax_start\n");

     printf("<-- wimax_start\n");
}

int wimax_init(struct ifnet *ifp)
{
     printf("--> wimax_init\n");

     printf("<-- wimax_init\n");
}

void wimax_stop(struct ifnet *ifp, int a) 
{
     printf("--> wimax_stop\n");

     printf("<-- wimax_stop\n");
}

void wimax_watchdog(struct ifnet *ifp)
{
     printf("--> wimax_watchdog\n");

     printf("<-- wimax_watchdog\n");
}

int wimax_ioctl(struct ifnet *ifp, u_long command, void *data)
{
     struct cue_softc    *sc = ifp->if_softc;
     struct ifaddr       *ifa = (struct ifaddr *)data;
     struct ifreq        *ifr = (struct ifreq *)data;
     int         s, error = 0;


     s = splnet();

     printf("--> wimax_ioctl command = 0x%x\n", command);

     switch (command) {
     case SIOCSIFADDR:

	  /*
	  if (p->if_flags |= IFF_UP);
	  wimax_init(ifp);
	  */

	  printf("SIOCSIFADDR: \n");

	  switch (ifa->ifa_addr->sa_family) {
	  case AF_INET:
	       arp_ifinit(ifp, ifa);
	       printf("AF_INET\n");
	       break;
	  }

	  break;

     case SIOCSIFMEDIA:
     case SIOCGIFMEDIA:
	  printf ("SIOCGIFMEDIA: \n");
	  break;

     case SIOCSIFMTU:                                                                                                                                     
	  if (ifr->ifr_mtu < ETHERMIN || ifr->ifr_mtu > ETHERMTU)                                                                                          
	       error = EINVAL;                                                                                                                              
	  else if ((error = ifioctl_common(ifp, command, data)) == ENETRESET)                                                                              
	       error = 0;                                                                                                                                   
	  break;           


     case SIOCSIFFLAGS:

	  printf("SIOCSIFFLAGS: \n");

	  break;

     }
     printf("<-- wimax_ioctl\n");

     splx(s);

     return (error);
}

//Static int wimax_eeprom_getword(struct wimax_softc *, int);
//Static void wimax_read_mac(struct wimax_softc *, u_char *);
Static int wimax_miibus_readreg(device_ptr_t dev, int phy, int reg) 
{

     return 0;
}

Static void wimax_miibus_writereg(device_ptr_t dev, int phy, int reg, int data)
{

}

Static void wimax_miibus_statchg(device_ptr_t dev)
{

}

Static void wimax_lock_mii(struct wimax_softc *sc)
{
     sc->wimax_refcnt++;
     mutex_enter(&sc->wimax_mii_lock);
}

Static void wimax_unlock_mii(struct wimax_softc *sc)
{
     mutex_exit(&sc->wimax_mii_lock);
     if (--sc->wimax_refcnt < 0)
	  usb_detach_wakeup(USBDEV(sc->wimax_dev));

}

/*
 * Set media options.
 */
Static int
wimax_ifmedia_upd(struct ifnet *ifp)
{
     struct wimax_softc    *sc = ifp->if_softc;
     struct mii_data     *mii = GET_MII(sc);
     int rc;

     printf("%s: %s: enter\n", USBDEVNAME(sc->wimax_dev), __func__);

     /*
       if (sc->wimax_dying)
       return (0);
     */

     sc->wimax_link = 0;

     if ((rc = mii_mediachg(mii)) == ENXIO)
	  return 0;
     return rc;
}
		
Static void wimax_tick(void *xsc)
{
     struct wimax_softc    *sc = xsc;
     usb_add_task(sc->wimax_udev, &sc->wimax_tick_task, USB_TASKQ_DRIVER);
}

Static void wimax_tick_task(void *xsc)
{
     struct wimax_softc    *sc = xsc;
     update_network(dev);
     usb_callout(sc->wimax_stat_ch, hz, wimax_tick, sc);
     //update_network(dev);
}

void wimax_init_device(struct wimax_softc *sc)
{
     unsigned char req_data[MAX_PACKET_LEN];
     int len;
     int r;
     int err;
     int i,s;

     usbd_status st;

     char status[256];
     char mac[256];

     bzero(mac, 20);

     memset(status, 0, sizeof(status));

     unsigned char init_data1[] = {0x57, 0x45, 0x04, 0x00, 0x00, 0x02, 0x00, 0x74};

     unsigned char init_data2[] = {
	  0x57, 0x50, 0x14, 0x00, 0x00, 0x00, 0x00, 0x00, 
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	  0x15, 0x0a, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00};

     unsigned char init_data3[] = {
	  0x57, 0x43, 0x12, 0x00, 0x15, 0x04, 0x00, 0x00,
	  0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 
	  0x00, 0x00, 0x15, 0x04, 0x50, 0x04, 0x00, 0x00};

     printf("Trying to init ...\n");	

     s = splnet();
     st = wimax_async_read(sc);
     if (st != USBD_IN_PROGRESS) {
	  printf("Cannot start async read: %s\n", usbd_errstr(st));
	  //return;
     }


     dev = (struct wimax_softc *) sc;

     st = wimax_usb_write(sc, sizeof(init_data1), init_data1);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot initialize device(1): %s\n", usbd_errstr(st));
	  return;
     }

     st = wimax_usb_write(sc, sizeof(init_data2), init_data2);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot initialize device(2): %s\n", usbd_errstr(st));
	  return;
     }

     st = wimax_usb_write(sc, sizeof(init_data3), init_data3);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot initialize device(3): %s\n", usbd_errstr(st));
	  return;
     }

     len=fill_string_info_req(req_data);

     st = wimax_usb_write(sc, len, req_data);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot send info request: %s\n", usbd_errstr(st));
	  return;
     }

     len = fill_init1_req(req_data);
     st = wimax_usb_write(sc, len, req_data);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot send init1 request: %s\n", usbd_errstr(st));
	  return;
     }

     len=fill_mac_lowlevel_req(req_data);
     st = wimax_usb_write(sc, len, req_data);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot send lowlev mac request: %s\n", usbd_errstr(st));
	  return;
     }
     
     len=fill_mac_req(req_data);
     st = wimax_usb_write(sc, len, req_data);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot send mac request: %s\n", usbd_errstr(st));
	  return;
     }

/*     
     st = wimax_usb_read(sc, 256, mac);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot read MAC: %s\n", usbd_errstr(st));
	  return;
     }

     printf("device MAC = %s\n", ether_sprintf(mac+12));
     memcpy(sc->mac, mac+MAC_OFFSET, MAC_LEN);

     printf("device MAC = %s\n", ether_sprintf(sc->mac));
*/

     len=fill_string_info_req(req_data);
     st = wimax_usb_write(sc, len, req_data);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot send string info request: %s\n", usbd_errstr(st));
	  return;
     }
	
     len=fill_init2_req(req_data);
     st = wimax_usb_write(sc, len, req_data);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot send info2 request: %s\n", usbd_errstr(st));
	  return;
     }

     len=fill_init3_req(req_data);
     st = wimax_usb_write(sc, len, req_data);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot send info3 request: %s\n", usbd_errstr(st));
	  return;
     }

     len=fill_authorization_data_req(req_data);
     st = wimax_usb_write(sc, len, req_data);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot send auth_data request: %s\n", usbd_errstr(st));
	  return;
     }
     
     printf("Trying to control diode ...\n");	
     len = fill_diode_control_cmd(req_data, 0);
     st = wimax_usb_write(sc, len, req_data);

     if (st != USBD_NORMAL_COMPLETION) {
	  printf("Cannot send auth_data request: %s\n", usbd_errstr(st));
	  return;
     }

     /*
       st = wimax_async_read(sc);
       if (st != USBD_IN_PROGRESS) {
       printf("Cannot start async read: %s\n", usbd_errstr(st));
       //return;
       }
     */
     sc->net_found = 0;

     splx(s);

     usb_callout(sc->wimax_stat_ch, hz, wimax_tick, sc);
}



USB_ATTACH(wimax)
{
     USB_ATTACH_START(wimax, sc, uaa);
     char                *devinfop;
     usbd_status         err;
     usbd_device_handle      dev = uaa->device;
     u_int8_t            mode, channel;
     int i,s;
     struct ifnet            *ifp = &sc->sc_if;
     struct mii_data 		*mii;
     usb_interface_descriptor_t  *id;
     usb_endpoint_descriptor_t   *ed;
     //usbd_status         err;
     usbd_pipe_handle rx_pipe, tx_pipe;

     printf("-->wimax_attach\n");

     sc->wimax_dev = self;
     sc->sc_state = WIMAX_S_UNCONFIG;

     devinfop = usbd_devinfo_alloc(dev, 0);
     USB_ATTACH_SETUP;
     aprint_normal_dev(self, "%s\n", devinfop);
     usbd_devinfo_free(devinfop);

//	s = splint();
	
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

     sc->wimax_unit = device_unit(self);
     sc->wimax_udev = dev;

     usb_init_task(&sc->wimax_tick_task, wimax_tick_task, sc);
     //usb_init_task(&sc->aue_stop_task, (void (*)(void *))aue_stop, sc);
     mutex_init(&sc->wimax_mii_lock, MUTEX_DEFAULT, IPL_NONE);

     id = usbd_get_interface_descriptor(sc->wimax_iface);

     for (i = 0; i < id->bNumEndpoints; i++) {                                                                                                          
	  ed = usbd_interface2endpoint_descriptor(sc->wimax_iface, i);                                                                                     
	  if (!ed) {
	       /* 
		  printf(("%s: num_endp:%d\n", USBDEVNAME(sc->wimax_dev), sc->wimax_iface->idesc->bNumEndpoints)); */
	       printf("%s: couldn't get ep %d\n",
		      (char *) USBDEVNAME(sc->wimax_dev), i);
	       return;
	  }

	  if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_IN &&
	      UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
	       sc->wimax_ed[WIMAX_ENDPT_RX] = ed->bEndpointAddress;
	       //sc->bulk_in_len = ed->bLength;
	       sc->bulk_in_len = RECV_BUFLEN;


	       printf("Found in endpoint: 0x%x\n", ed->bEndpointAddress);

	  } else if (UE_GET_DIR(ed->bEndpointAddress) == UE_DIR_OUT &&
		     UE_GET_XFERTYPE(ed->bmAttributes) == UE_BULK) {
	       sc->wimax_ed[WIMAX_ENDPT_TX] = ed->bEndpointAddress;
	       printf("Found out endpoint: 0x%x\n", ed->bEndpointAddress);
	  }

     }

     err = usbd_open_pipe(sc->wimax_iface, sc->wimax_ed[WIMAX_ENDPT_RX], USBD_EXCLUSIVE_USE, &sc->rx_pipe);
     if (err) {
	  printf("%s: rx fifo init failed\n", USBDEVNAME(sc->wimax_dev));
	  return; 
     }

     err = usbd_open_pipe(sc->wimax_iface, sc->wimax_ed[WIMAX_ENDPT_TX], 0, &sc->tx_pipe);
     if (err) {
	  printf("%s: tx fifo init failed\n", USBDEVNAME(sc->wimax_dev));
	  return; 
     }


     ifp->if_softc = sc;
     memcpy(ifp->if_xname, USBDEVNAME(sc->wimax_dev), IFNAMSIZ);
     ifp->if_flags =IFF_BROADCAST | IFF_SIMPLEX | IFF_MULTICAST; 
     ifp->if_init = wimax_init;
     ifp->if_stop = wimax_stop;
     ifp->if_start = wimax_start;
     ifp->if_ioctl = wimax_ioctl;
     ifp->if_watchdog = wimax_watchdog;
     ifp->if_mtu = WIMAX_DEFAULT_MTU;
     IFQ_SET_READY(&ifp->if_snd);

     mii = &sc->wimax_mii;
     mii->mii_ifp = ifp;
     mii->mii_readreg = wimax_miibus_readreg;
     mii->mii_writereg = wimax_miibus_writereg;
     mii->mii_statchg = wimax_miibus_statchg;
     mii->mii_flags = MIIF_AUTOTSLEEP;
     sc->sc_ec.ec_mii = mii;

     ifmedia_init(&mii->mii_media, 0, wimax_ifmedia_upd, ether_mediastatus);
     mii_attach(self, mii, 0xffffffff, MII_PHY_ANY, MII_OFFSET_ANY, 0);
     if (LIST_FIRST(&mii->mii_phys) == NULL) {
	  ifmedia_add(&mii->mii_media, IFM_ETHER | IFM_NONE, 0, NULL);
	  ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_NONE);
     } else
	  ifmedia_set(&mii->mii_media, IFM_ETHER | IFM_AUTO);
	
     usb_callout_init(sc->wimax_stat_ch);

     if_attach(ifp);
     Ether_ifattach(ifp, sc->mac);

     usbd_add_drv_event(USB_EVENT_DRIVER_ATTACH, sc->wimax_udev, USBDEV(sc->wimax_dev));     

     wimax_init_device(sc);
	
//	splx(s);



     USB_ATTACH_SUCCESS_RETURN;

}

USB_DETACH(wimax)
{
     USB_DETACH_START(wimax, sc);
     printf("-->wimax_detach\n");

     struct ifnet  *ifp = &sc->sc_if;
     if_detach(ifp);

     usb_uncallout(sc->wimax_stat_ch, wimax_tick, sc);

     usb_rem_task(sc->wimax_udev, &sc->wimax_tick_task);
//	mii_detach(&sc->wimax_mii, MII_PHY_ANY, MII_OFFSET_ANY);                                                                                             
//	ifmedia_delete_instance(&sc->wimax_mii.mii_media, IFM_INST_ANY);                                                                                     
     //ether_ifdetach(ifp);

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

