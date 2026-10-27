/* SPDX-License-Identifier: GPL-2.0-only
 * Exercise the production handoff with simulated UEFI services, never hardware.
 */
#include "../port.h"
#include "../ownership.h"
#include "../checks.h"
#include "../selector_check.h"
#include <Protocol/GraphicsOutput.h>
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

GU_PLATFORM Gu;
GU_RUNTIME_CONFIG GuCfg;
static EFI_GUID PciGuid=EFI_PCI_IO_PROTOCOL_GUID,PathGuid=EFI_DEVICE_PATH_PROTOCOL_GUID,
  GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID;
static UINT32 InitialConfig[64];
static BOOLEAN GuScreenDetached,ExpectDetached;
static struct { BOOLEAN valid; UINT32 mode; UINT8 path_hash[32],remaining[16]; UINTN remaining_bytes; } SavedDisplay;
static EFI_BOOT_SERVICES services;
static int connects,closes,refresh_error,close_error,bad_bar,bme,bad_selectors,late_selectors,log_fault,fail_direct_log;
static char report[8192];
VOID GuLogText(const char *text) {
  assert(strlen(report)+strlen(text)<sizeof(report));strcat(report,text);
  if(fail_direct_log && strstr(text,"handoff=direct-windows-v1"))log_fault=1;
}
VOID GuLog(const char *fmt,...) {
  char text[1024];va_list ap;va_start(ap,fmt);vsnprintf(text,sizeof(text),fmt,ap);va_end(ap);GuLogText(text);
}
static BOOLEAN GuLogReady(VOID) { return !log_fault; }
BOOLEAN GuSafeToReturn(VOID) { return !Gu.fault && !Gu.retained && !Gu.dma_count && !Gu.allocations; }
UINT32 GuRead32(UINT32 off) {
  if(off==0x823804)return 0xffffffff;
  if(off==0x82381c)return bad_selectors?0:0x88888888;
  if(off==0x823820)return 8;
  return 0;
}
EFI_STATUS GuRefreshTargetPci(VOID) { return refresh_error?EFI_NOT_FOUND:EFI_SUCCESS; }
EFI_STATUS GuConfigRead(EFI_PCI_IO_PROTOCOL_WIDTH width,UINT32 off,VOID *value) {
  if(width==EfiPciIoWidthUint16)*(UINT16*)value=bme?7:3;
  else *(UINT32*)value=InitialConfig[off/4]^(bad_bar?0x10:0);
  if(late_selectors)bad_selectors=1;
  return EFI_SUCCESS;
}
VOID *GuAlloc(size_t n) { return calloc(1,n); }
VOID GuFree(VOID *p) { free(p); }
static EFI_HANDLE GuBootVolumeHandle(VOID) { return NULL; }
BOOLEAN GuWindowsEspPathEx(const EFI_DEVICE_PATH_PROTOCOL *p,UINTN n,BOOLEAN a) { return FALSE; }
static BOOLEAN DisplayRoute(EFI_DEVICE_PATH_PROTOCOL *p,UINT8 h[32],UINT8 *s,UINTN *n) { return FALSE; }
static BOOLEAN DisplayCheck(VOID) { return TRUE; }
static EFI_STATUS EFIAPI close_protocol(EFI_HANDLE h,EFI_GUID *g,EFI_HANDLE a,EFI_HANDLE c) {
  closes++;return close_error?EFI_ACCESS_DENIED:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI connect_controller(EFI_HANDLE h,EFI_HANDLE *d,EFI_DEVICE_PATH_PROTOCOL *p,BOOLEAN r) {
  connects++;return EFI_DEVICE_ERROR;
}
#include "../handoff.inc"

static void setup(void) {
  memset(&Gu,0,sizeof(Gu));memset(&GuCfg,0,sizeof(GuCfg));memset(&services,0,sizeof(services));
  memset(&SavedDisplay,0,sizeof(SavedDisplay));memset(InitialConfig,0,sizeof(InitialConfig));
  connects=closes=refresh_error=close_error=bad_bar=bme=bad_selectors=late_selectors=log_fault=fail_direct_log=0;
  report[0]=0;GuScreenDetached=ExpectDetached=FALSE;
  services.CloseProtocol=close_protocol;services.ConnectController=connect_controller;
  Gu.bs=&services;Gu.handle=(VOID*)1;Gu.image=(VOID*)2;WindowsImage=(VOID*)3;
  Gu.quiesced=TRUE;Gu.owned=TRUE;Gu.flrs=1;Gu.target_board_id=0x248910de;Gu.target_arch=GU_ARCH_AMPERE;
  InitialConfig[1]=3;InitialConfig[4]=0xa2000000;
  GuCfg.skip_gop_reconnect=TRUE;GuCfg.allow_headless_handoff=TRUE;
  SavedDisplay.valid=TRUE;SavedDisplay.remaining_bytes=4;
}

int main(void) {
  GU_RELEASE_OWNER owner={.agent=(VOID*)4};
  for(int arch=0;arch<2;arch++) {
    setup();Gu.target_arch=arch?GU_ARCH_ADA:GU_ARCH_AMPERE;
    assert(RestoreDisplayForWindows(&owner)==EFI_SUCCESS);
    assert(!connects && closes==1 && !Gu.owned && GuScreenDetached && ExpectDetached);
    assert(strstr(report,"PRE-WINDOWS-DIRECT") && strstr(report,"stage=before_StartImage"));
  }
  for(int c=0;c<13;c++) {
    setup();
    switch(c) {
      case 0:WindowsImage=NULL;break;
      case 1:Gu.fault=TRUE;break;
      case 2:Gu.dma_count=1;break;
      case 3:Gu.flrs=0;break;
      case 4:Gu.quiesced=FALSE;break;
      case 5:GuCfg.allow_headless_handoff=FALSE;break;
      case 6:close_error=1;break;
      case 7:refresh_error=1;break;
      case 8:bad_bar=1;break;
      case 9:bme=1;break;
      case 10:bad_selectors=1;break;
      case 11:late_selectors=1;break;
      case 12:fail_direct_log=1;break;
    }
    assert(EFI_ERROR(RestoreDisplayForWindows(&owner)));
    assert(!connects && !strstr(report,"stage=before_StartImage"));
  }
  setup();GuCfg.skip_gop_reconnect=FALSE;
  assert(RestoreDisplayForWindows(&owner)==EFI_SUCCESS && connects==1);
  assert(!strstr(report,"handoff=direct-windows-v1"));
  puts("PASS: 2 direct handoff architectures, 13 refusal cases, legacy GOP path (UEFI simulated)");
  return 0;
}
