#include "app_config.h"
#include "usb/device/usb_stack.h"
/* Private bench identity, inherited Jieli SDK VID/PID pair, NOT a newly
 * assigned/public VID. No chip/flash serial or key is exposed. */
const u8 fm1_usb_device_descriptor[]={18,1,0,2,2,2,1,64,0x54,0x36,0x55,0x51,0,2,1,2,0,1};
const u8 fm1_usb_config_descriptor[]={9,2,0,0,0,1,0,0x80,50};
static const u8 lang[]={4,3,9,4};
static void string_desc(u8 *p,const char *s) {
    unsigned n=strlen(s),i; p[0]=2+2*n;p[1]=3;
    for(i=0;i<n;i++){p[2+2*i]=s[i];p[3+2*i]=0;}
}
void get_device_descriptor(u8 *p,usb_dev id){(void)id;memcpy(p,fm1_usb_device_descriptor,18);}
void get_language_str(u8 *p){memcpy(p,lang,sizeof(lang));}
void get_manufacture_str(u8 *p){string_desc(p,"FM1 local diagnostic");}
void get_product_str(u8 *p){string_desc(p,"FM1 USB Diagnostic");}
void get_iserialnumber_str(u8 *p){string_desc(p,"");}
const u8 *usb_get_config_desc(void){return fm1_usb_config_descriptor;}
const u8 *usb_get_string_desc(u32 id){(void)id;return NULL;}
