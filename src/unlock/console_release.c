/* SPDX-License-Identifier: GPL-2.0-only
 * MSI Z390M / 2489:3903 console shape observed in CAP00. Never guess drivers.
 */
#include "console_release.h"
#include "app_io.h"
#include "runtime.h"
#include <Protocol/LoadedImage.h>
#include <Protocol/GraphicsOutput.h>
static EFI_GUID TextGuid=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID,GopGuid=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,
  BindingGuid=EFI_DRIVER_BINDING_PROTOCOL_GUID,ImageGuid=EFI_LOADED_IMAGE_PROTOCOL_GUID;
static const UINT8 SplitterPath[]={0x04,0x06,0x14,0x00,0x7d,0x49,0x8a,0x62,0xf6,0x2b,0x64,0x42,0x87,0x41,0x06,0x9d,0xbd,0x33,0x99,0xd6,0x7f,0xff,0x04,0x00};
static const UINT8 GfxPath[]={0x04,0x06,0x14,0x00,0xdd,0xab,0xe7,0x43,0x52,0xe3,0xfb,0x4c,0xa2,0x30,0x4c,0xdc,0x1d,0x35,0x0e,0x5c,0x7f,0xff,0x04,0x00};
static BOOLEAN Passive(UINT32 a) {return a==1 || a==2 || a==4;}
static EFI_STATUS Edges(EFI_HANDLE child,EFI_HANDLE console,GU_CONSOLE_PLAN *p,UINTN stage) {
  EFI_GUID **protocols=NULL;UINTN count=0,gfx=0,split=0,virtual=0;
  EFI_STATUS s=Gu.bs->ProtocolsPerHandle(child,&protocols,&count);
  if(EFI_ERROR(s))return s;
  if(!protocols || !count || count>32){if(protocols)Gu.bs->FreePool(protocols);return EFI_UNSUPPORTED;}
  for(UINTN i=0;i<count && !EFI_ERROR(s);i++) {
    EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *e=NULL;UINTN n=0;
    if(!protocols[i]){s=EFI_COMPROMISED_DATA;break;}
    s=Gu.bs->OpenProtocolInformation(child,protocols[i],&e,&n);
    if(!EFI_ERROR(s) && (n>64 || (n && !e)))s=EFI_COMPROMISED_DATA;
    if(!EFI_ERROR(s))for(UINTN j=0;j<n;j++) {
      if(Passive(e[j].Attributes))continue;
      if(e[j].OpenCount!=1 || !e[j].AgentHandle){s=EFI_ACCESS_DENIED;break;}
      if(!memcmp(protocols[i],&GopGuid,sizeof(EFI_GUID)) && e[j].Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e[j].ControllerHandle==child) {
        if(p->gfx && p->gfx!=e[j].AgentHandle){s=EFI_ACCESS_DENIED;break;}
        p->gfx=e[j].AgentHandle;gfx++;
      } else if(!memcmp(protocols[i],&TextGuid,sizeof(EFI_GUID)) &&
          (e[j].Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER || e[j].Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER)) {
        if(p->splitter && p->splitter!=e[j].AgentHandle){s=EFI_ACCESS_DENIED;break;}
        p->splitter=e[j].AgentHandle;
        if(e[j].Attributes==EFI_OPEN_PROTOCOL_BY_DRIVER && e[j].ControllerHandle==child)split++;
        else if(e[j].Attributes==EFI_OPEN_PROTOCOL_BY_CHILD_CONTROLLER && e[j].ControllerHandle==console)virtual++;
        else {s=EFI_ACCESS_DENIED;break;}
      } else {s=EFI_ACCESS_DENIED;break;}
    }
    if(e && EFI_ERROR(Gu.bs->FreePool(e)))s=EFI_DEVICE_ERROR;
  }
  if(EFI_ERROR(Gu.bs->FreePool(protocols)))s=EFI_DEVICE_ERROR;
  if(!EFI_ERROR(s) && (gfx!=(stage<2?1U:0U) || split!=(stage==0?1U:0U) || virtual!=(stage==0?1U:0U)))s=EFI_ACCESS_DENIED;
  return s;
}
static EFI_STATUS Driver(EFI_HANDLE agent,const UINT8 *path,UINT64 bytes,EFI_DRIVER_BINDING_PROTOCOL *copy,VOID **base) {
  EFI_DRIVER_BINDING_PROTOCOL *d=NULL;EFI_LOADED_IMAGE_PROTOCOL *im=NULL;
  EFI_STATUS s=Gu.bs->HandleProtocol(agent,&BindingGuid,(VOID**)&d);
  if(EFI_ERROR(s)||!d || d->DriverBindingHandle!=agent || !d->ImageHandle || d->Version!=0x10 || !d->Stop || !d->Start || !d->Supported)return EFI_UNSUPPORTED;
  s=Gu.bs->HandleProtocol(d->ImageHandle,&ImageGuid,(VOID**)&im);
  if(EFI_ERROR(s)||!im || !im->ImageBase || im->ImageSize!=bytes || im->ImageCodeType!=EfiBootServicesCode || !im->FilePath)return EFI_UNSUPPORTED;
  const UINT8 *raw=(VOID*)im->FilePath;
  /* Validate the first node before reading its 24-byte FV path and end node. */
  if(raw[0]!=4 || raw[1]!=6 || raw[2]!=20 || raw[3]!=0 || memcmp(raw,path,24))return EFI_UNSUPPORTED;
  UINTN lo=(UINTN)im->ImageBase;
  if(lo>~(UINTN)0-bytes)return EFI_UNSUPPORTED;
  UINTN callbacks[]={(UINTN)d->Start,(UINTN)d->Stop,(UINTN)d->Supported};
  for(UINTN i=0;i<3;i++)if(callbacks[i]<lo || callbacks[i]>=lo+bytes)return EFI_UNSUPPORTED;
  *copy=*d;*base=im->ImageBase;return EFI_SUCCESS;
}
static BOOLEAN ConsoleStable(EFI_SYSTEM_TABLE *t,const GU_CONSOLE_PLAN *p) {
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *text=NULL;
  EFI_STATUS s=Gu.bs->HandleProtocol(p->console,&TextGuid,(VOID**)&text);
  return !EFI_ERROR(s) && text==p->text && t->ConsoleOutHandle==p->console && t->ConsoleInHandle==p->input &&
    t->StandardErrorHandle==p->error && t->ConOut==p->text && t->StdErr==p->stderr_text && t->ConIn==p->keys &&
    p->keys && p->keys->ReadKeyStroke && p->keys->WaitForKey;
}
#ifdef GU_NATIVE_CAP01_BUILD
/* CAP01: native GPU option-ROM driver, never the old FV CsmVideo wrapper. */
static EFI_STATUS NativeOwner(const GU_RELEASE_OWNER *o) {
  EFI_LOADED_IMAGE_PROTOCOL *im=NULL;EFI_DRIVER_BINDING_PROTOCOL *d=NULL;
  if(o->version!=0x6000a || o->size!=179776 || !o->base ||
     (UINTN)o->base>~(UINTN)0-179776 || (UINTN)o->stop!=(UINTN)o->base+0x698)return EFI_UNSUPPORTED;
  EFI_STATUS s=Gu.bs->HandleProtocol(o->image,&ImageGuid,(VOID**)&im);
  if(EFI_ERROR(s)||!im || im->ImageBase!=o->base || im->ImageSize!=o->size ||
     im->DeviceHandle!=Gu.handle || im->ImageCodeType!=EfiBootServicesCode || !im->FilePath)return EFI_UNSUPPORTED;
  const UINT8 *p=(VOID*)im->FilePath;const CHAR16 name[]=L"nvgop-ga1xx";
  if(p[0]!=4 || p[1]!=4 || p[2]!=28 || p[3] || memcmp(p+4,name,sizeof(name)) ||
     p[28]!=0x7f || p[29]!=0xff || p[30]!=4 || p[31])return EFI_UNSUPPORTED;
  s=Gu.bs->HandleProtocol(o->agent,&BindingGuid,(VOID**)&d);
  if(EFI_ERROR(s)||!d || d->Version!=o->version || d->ImageHandle!=o->image ||
     d->DriverBindingHandle!=o->agent || d->Stop!=o->stop ||
     (UINTN)d->Supported!=(UINTN)o->base+0x354 || (UINTN)d->Start!=(UINTN)o->base+0x774)return EFI_UNSUPPORTED;
  return EFI_SUCCESS;
}
#endif
EFI_STATUS GuPlanConsoleRelease(EFI_SYSTEM_TABLE *t,const GU_RELEASE_OWNER *o,GU_CONSOLE_PLAN *p) {
  memset(p,0,sizeof(*p));
  if(!t || !o || o->child_count!=1 || !o->children[0].gop || Gu.target_board_id!=0x248910de || Gu.target_subsystem!=0x39031462 ||
     t->FirmwareRevision!=0x5000d || t->Hdr.Revision!=0x20046 || !t->ConsoleOutHandle ||
     t->ConsoleOutHandle!=t->ConsoleInHandle || t->ConsoleOutHandle!=t->StandardErrorHandle)return EFI_UNSUPPORTED;
#ifdef GU_NATIVE_CAP01_BUILD
  EFI_STATUS native=NativeOwner(o);if(EFI_ERROR(native))return native;
#endif
  p->child=o->children[0].handle;p->console=t->ConsoleOutHandle;p->input=t->ConsoleInHandle;p->error=t->StandardErrorHandle;
  p->text=t->ConOut;p->stderr_text=t->StdErr;p->keys=t->ConIn;
  if(p->child==p->console || p->child==Gu.handle || !ConsoleStable(t,p))return EFI_UNSUPPORTED;
  EFI_STATUS s=Edges(p->child,p->console,p,0);if(EFI_ERROR(s))return s;
  if(!p->gfx || !p->splitter || p->gfx==p->splitter || p->gfx==o->agent || p->splitter==o->agent || p->gfx==Gu.image || p->splitter==Gu.image)return EFI_UNSUPPORTED;
  s=Driver(p->gfx,GfxPath,21696,&p->gfx_binding,&p->gfx_base);if(EFI_ERROR(s))return s;
  s=Driver(p->splitter,SplitterPath,25760,&p->splitter_binding,&p->splitter_base);if(EFI_ERROR(s))return s;
#ifdef GU_NATIVE_CAP01_BUILD
  GuLogText("console plan=native-cap01-v4 verified: nvgop-ga1xx 0x6000a, text splitter -> graphics console -> native NVIDIA\n");
#else
  GuLogText("console plan=msi-z390m-cap00-v1 verified: text splitter -> graphics console -> NVIDIA\n");
#endif
  return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
EFI_STATUS GuReleaseConsoleConsumers(EFI_SYSTEM_TABLE *t,const GU_RELEASE_OWNER *o,const GU_CONSOLE_PLAN *expected) {
  GU_CONSOLE_PLAN fresh;EFI_STATUS s=GuPlanConsoleRelease(t,o,&fresh);
  if(EFI_ERROR(s) || memcmp(&fresh,expected,sizeof(fresh)))return EFI_ERROR(s)?s:EFI_ABORTED;
  GuScreenDetached=TRUE;
  GuLogText("console stage=detach-text-splitter BEGIN\n");if(!GuLogReady())return EFI_DEVICE_ERROR;
  s=Gu.bs->DisconnectController(fresh.child,fresh.splitter,fresh.console);
  GuLog("console stage=detach-text-splitter RETURN status=%#llx\n",(unsigned long long)s);
  if(EFI_ERROR(s)||!GuLogReady()||!ConsoleStable(t,&fresh))return EFI_ERROR(s)?s:EFI_DEVICE_ERROR;
  s=Edges(fresh.child,fresh.console,&fresh,1);if(EFI_ERROR(s))return s;
  GuLogText("console stage=detach-graphics-console BEGIN\n");if(!GuLogReady())return EFI_DEVICE_ERROR;
  s=Gu.bs->DisconnectController(fresh.child,fresh.gfx,NULL);
  GuLog("console stage=detach-graphics-console RETURN status=%#llx\n",(unsigned long long)s);
  if(EFI_ERROR(s)||!GuLogReady()||!ConsoleStable(t,&fresh))return EFI_ERROR(s)?s:EFI_DEVICE_ERROR;
  s=Edges(fresh.child,fresh.console,&fresh,2);if(EFI_ERROR(s))return s;
  GuLogText("console consumers detached and verified; virtual console and keyboard retained\n");
  return GuLogReady()?EFI_SUCCESS:EFI_DEVICE_ERROR;
}
