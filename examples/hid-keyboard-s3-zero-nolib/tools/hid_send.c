// cc -o hid_send tools/hid_send.c -framework IOKit -framework CoreFoundation
//
// Commands (Report ID 0x07):
//   led <R> <G> <B>       set LED color
//   type|default "<markup>"  type text / set BOOT text
//   boot                   simulate BOOT press
//   status                 get device status
//   flash                  reboot into firmware update
//   delay <base> [rand]    typing delay (ms)
//   raw <hex>              keyboard LED (0x02=Caps)
//
// Markup: <enter> <up> <down> <left> <right> <tab>
//         <wN> <sN> <ctrl>X <shift>X <option>X <command>X <<
//
// Legacy: ./hid_send 0x02  → keyboard LED

#include <IOKit/hid/IOHIDLib.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>

static void usage(const char *p) {
  fprintf(stderr,
    "Usage:\n"
    "  %s led <R> <G> <B>\n"
    "  %s type|default \"<markup>\"\n"
    "  %s boot|status|flash\n"
    "  %s delay <base> [rand]\n"
    "  %s raw <hex>  (%s <hex>)\n", p, p, p, p, p, p);
}

static volatile bool gotResponse = false;
static uint8_t responseData[8];

static void onInput(void *ctx, IOReturn res, void *sender,
                     IOHIDReportType type, uint32_t rid,
                     uint8_t *rpt, CFIndex rptLen) {
  (void)ctx; (void)res; (void)sender; (void)type;
  if (rid == 0x08 && rptLen >= 5) {
    const uint8_t *d = rpt; CFIndex dl = rptLen;
    if (dl > 0 && d[0] == rid) { d++; dl--; }
    CFIndex cp = (dl > 8) ? 8 : dl;
    memcpy(responseData, d, (size_t)cp);
    gotResponse = true;
    CFRunLoopStop(CFRunLoopGetCurrent());
  }
}

int main(int argc, char *argv[]) {
  if (argc < 2) { usage(argv[0]); return 1; }

  uint8_t rpt[65]; size_t tLen = 0;
  bool expectResp = false;
  memset(rpt, 0, sizeof(rpt));
  const char *c = argv[1];

  if ((c[0]=='0'&&(c[1]=='x'||c[1]=='X'))||(c[0]>='0'&&c[0]<='9')) {
    rpt[0]=0x01; rpt[1]=(uint8_t)strtol(c,NULL,0); tLen=2;
  } else if (!strcmp(c,"raw") && argc>=3) {
    rpt[0]=0x01; rpt[1]=(uint8_t)strtol(argv[2],NULL,0); tLen=2;
  } else if (!strcmp(c,"led") && argc>=5) {
    rpt[0]=0x07; rpt[1]=0x01; rpt[2]=(uint8_t)atoi(argv[2]);
    rpt[3]=(uint8_t)atoi(argv[3]); rpt[4]=(uint8_t)atoi(argv[4]); tLen=5;
  } else if (!strcmp(c,"type") && argc>=3) {
    size_t sl = strlen(argv[2]); if (sl>61) sl=61;
    rpt[0]=0x07; rpt[1]=0x02; memcpy(rpt+2,argv[2],sl); rpt[2+sl]=0; tLen=3+sl;
  } else if (!strcmp(c,"default") && argc>=3) {
    size_t sl = strlen(argv[2]); if (sl>61) sl=61;
    rpt[0]=0x07; rpt[1]=0x05; memcpy(rpt+2,argv[2],sl); rpt[2+sl]=0; tLen=3+sl;
  } else if (!strcmp(c,"boot")) {
    rpt[0]=0x07; rpt[1]=0x06; tLen=2;
  } else if (!strcmp(c,"status")) {
    rpt[0]=0x07; rpt[1]=0x07; tLen=2; expectResp=true;
  } else if (!strcmp(c,"flash")) {
    rpt[0]=0x07; rpt[1]=0x08; tLen=2;
  } else if (!strcmp(c,"delay") && argc>=3) {
    rpt[0]=0x07; rpt[1]=0x09; rpt[2]=(uint8_t)atoi(argv[2]);
    rpt[3]=(argc>=4)?(uint8_t)atoi(argv[3]):0; tLen=4;
  } else { fprintf(stderr,"Unknown: %s\n",c); usage(argv[0]); return 1; }

  CFMutableDictionaryRef match = CFDictionaryCreateMutable(
    kCFAllocatorDefault,0,&kCFTypeDictionaryKeyCallBacks,&kCFTypeDictionaryValueCallBacks);
  int32_t vid=0x303A,pid=0x1001;
  CFDictionarySetValue(match,CFSTR(kIOHIDVendorIDKey),
    CFNumberCreate(kCFAllocatorDefault,kCFNumberSInt32Type,&vid));
  CFDictionarySetValue(match,CFSTR(kIOHIDProductIDKey),
    CFNumberCreate(kCFAllocatorDefault,kCFNumberSInt32Type,&pid));

  IOHIDManagerRef mgr = IOHIDManagerCreate(kCFAllocatorDefault,kIOHIDOptionsTypeNone);
  IOHIDManagerSetDeviceMatching(mgr,match);
  IOHIDManagerOpen(mgr,kIOHIDOptionsTypeNone);

  CFSetRef devSet = IOHIDManagerCopyDevices(mgr);
  if (!devSet || CFSetGetCount(devSet)==0) { fprintf(stderr,"No device\n"); return 1; }
  CFIndex n = CFSetGetCount(devSet);
  IOHIDDeviceRef *devs = malloc((size_t)n * sizeof(IOHIDDeviceRef));
  CFSetGetValues(devSet,(const void**)devs);
  fprintf(stderr,"Found %ld interface(s)\n",n);

  IOHIDDeviceRef used = NULL;
  for (CFIndex i=0; i<n; i++) {
    IOReturn ret = IOHIDDeviceSetReport(devs[i],kIOHIDReportTypeOutput,0,rpt,tLen);
    if (ret==kIOReturnSuccess) { used=devs[i]; fprintf(stderr,"  used [%ld]\n",i); break; }
  }
  if (!used) { fprintf(stderr,"Send failed\n"); return 1; }

  if (expectResp) {
    static uint8_t rx[128];
    IOHIDDeviceRegisterInputReportCallback(used,rx,sizeof(rx),onInput,NULL);
    IOHIDDeviceScheduleWithRunLoop(used,CFRunLoopGetCurrent(),kCFRunLoopDefaultMode);
  }

  printf("Sent %s",c);
  if (!strcmp(c,"led"))     printf(" %d %d %d",rpt[2],rpt[3],rpt[4]);
  else if (!strcmp(c,"type")||!strcmp(c,"default")) printf(" \"%s\"",argv[2]);
  else if (!strcmp(c,"delay")) printf(" %d",rpt[2]);
  putchar(10);

  if (expectResp) {
    CFRunLoopRunInMode(kCFRunLoopDefaultMode,2.0,false);
    if (gotResponse) {
      printf("Status       : %s\n",responseData[0]?"typing":"idle");
      printf("Caps Lock    : %s\n",responseData[1]?"ON":"off");
      printf("LED          : R=%d G=%d B=%d\n",responseData[2],responseData[3],responseData[4]);
      printf("Default len  : %d\n",responseData[5]);
      printf("Delay        : base=%d rand=%d\n",responseData[6],responseData[7]);
    } else printf("(no response)\n");
  }

  free(devs); CFRelease(devSet); CFRelease(mgr); CFRelease(match);
  return 0;
}
