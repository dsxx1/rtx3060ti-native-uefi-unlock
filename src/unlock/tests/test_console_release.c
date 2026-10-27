/* SPDX-License-Identifier: GPL-2.0-only -- CAP00-shaped synthetic UEFI graph */
#include "../console_release.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
GU_PLATFORM Gu;
BOOLEAN GuScreenDetached;
static EFI_SYSTEM_TABLE table;
static EFI_BOOT_SERVICES bs;
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL console;
static EFI_SIMPLE_TEXT_INPUT_PROTOCOL keys;
static EFI_DRIVER_BINDING_PROTOCOL gfx,splitter;
static EFI_LOADED_IMAGE_PROTOCOL gfx_image,splitter_image;
#ifdef GU_NATIVE_CAP01_BUILD
static EFI_DRIVER_BINDING_PROTOCOL nv;
static EFI_LOADED_IMAGE_PROTOCOL nv_image;
static UINT8 nv_path[32];
#endif
static EFI_HANDLE child=(VOID*)10,console_handle=(VOID*)11,gfx_agent=(VOID*)20,splitter_agent=(VOID*)21;
static int stage,calls,fault,live;
static char logs[8192];
VOID GuLogText(const char *s) {assert(strlen(logs)+strlen(s)<sizeof(logs));strcat(logs,s);}
VOID GuLog(const char *fmt,...) {GuLogText(fmt);}
BOOLEAN GuLogReady(VOID) {return fault!=14;}
static EFI_STATUS EFIAPI free_pool(VOID *p) {free(p);live--;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI protocols(EFI_HANDLE h,EFI_GUID ***p,UINTN *n) {
  assert(h==child);*n=2;*p=malloc(2*sizeof(**p));live++;(*p)[0]=&GopGuid;(*p)[1]=&TextGuid;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI opens(EFI_HANDLE h,EFI_GUID *g,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **p,UINTN *n) {
  assert(h==child);*p=NULL;*n=0;
  if(!memcmp(g,&GopGuid,sizeof(*g)) && stage<2) {
    *n=1;*p=calloc(1,sizeof(**p));live++;(*p)[0]=(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){.AgentHandle=gfx_agent,.ControllerHandle=child,.Attributes=0x10,.OpenCount=1};
    if(fault==1)(*p)[0].OpenCount=2;
    if(fault==2)(*p)[0].Attributes=0x20;
  }
  if(!memcmp(g,&TextGuid,sizeof(*g)) && stage==0) {
    *n=2;*p=calloc(2,sizeof(**p));live++;
    (*p)[0]=(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){.AgentHandle=splitter_agent,.ControllerHandle=child,.Attributes=0x10,.OpenCount=1};
    (*p)[1]=(EFI_OPEN_PROTOCOL_INFORMATION_ENTRY){.AgentHandle=splitter_agent,.ControllerHandle=console_handle,.Attributes=8,.OpenCount=1};
    if(fault==3)(*p)[1].ControllerHandle=(VOID*)12;
    if(fault==4)(*p)[1].AgentHandle=(VOID*)22;
  }
  return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI handle(EFI_HANDLE h,EFI_GUID *g,VOID **p) {
  *p=NULL;
  if(h==console_handle && !memcmp(g,&TextGuid,sizeof(*g)))*p=&console;
  if(h==gfx_agent && !memcmp(g,&BindingGuid,sizeof(*g)))*p=&gfx;
  if(h==splitter_agent && !memcmp(g,&BindingGuid,sizeof(*g)))*p=&splitter;
  if(h==(VOID*)30)*p=&gfx_image;
  if(h==(VOID*)31)*p=&splitter_image;
#ifdef GU_NATIVE_CAP01_BUILD
  if(h==(VOID*)60 && !memcmp(g,&ImageGuid,sizeof(*g)))*p=&nv_image;
  if(h==(VOID*)60 && !memcmp(g,&BindingGuid,sizeof(*g)))*p=&nv;
#endif
  return *p?EFI_SUCCESS:EFI_NOT_FOUND;
}
static EFI_STATUS EFIAPI disconnect(EFI_HANDLE h,EFI_HANDLE agent,EFI_HANDLE route) {
  assert(h==child && GuScreenDetached);calls++;
  if(calls==1) {
    assert(agent==splitter_agent && route==console_handle);
    if(fault==9)return EFI_DEVICE_ERROR;
    if(fault!=10)stage=1;
    if(fault==11)table.ConIn=NULL;
  } else {
    assert(calls==2 && agent==gfx_agent && !route && stage==1);
    if(fault==12)return EFI_DEVICE_ERROR;
    if(fault!=13)stage=2;
  }
  return EFI_SUCCESS;
}
static void setup(GU_RELEASE_OWNER *o) {
  memset(&Gu,0,sizeof(Gu));memset(&bs,0,sizeof(bs));memset(&table,0,sizeof(table));memset(o,0,sizeof(*o));
  memset(&keys,0,sizeof(keys));memset(&console,0,sizeof(console));
  memset(&gfx,0,sizeof(gfx));memset(&splitter,0,sizeof(splitter));memset(&gfx_image,0,sizeof(gfx_image));memset(&splitter_image,0,sizeof(splitter_image));
  stage=calls=live=0;logs[0]=0;GuScreenDetached=FALSE;
  bs.ProtocolsPerHandle=protocols;bs.OpenProtocolInformation=opens;bs.HandleProtocol=handle;bs.FreePool=free_pool;bs.DisconnectController=disconnect;
  Gu.bs=&bs;Gu.handle=(VOID*)40;Gu.image=(VOID*)41;Gu.target_board_id=0x248910de;Gu.target_subsystem=0x39031462;
  table.FirmwareRevision=0x5000d;table.Hdr.Revision=0x20046;
  table.ConsoleOutHandle=table.ConsoleInHandle=table.StandardErrorHandle=console_handle;
  table.ConOut=table.StdErr=&console;table.ConIn=&keys;keys.ReadKeyStroke=(EFI_INPUT_READ_KEY)(UINTN)0x300000;keys.WaitForKey=(VOID*)50;
  o->agent=(VOID*)60;o->child_count=1;o->children[0].handle=child;o->children[0].gop=(VOID*)61;
  gfx.DriverBindingHandle=gfx_agent;gfx.ImageHandle=(VOID*)30;gfx.Version=0x10;
  gfx.Supported=(EFI_DRIVER_BINDING_SUPPORTED)(UINTN)0x100100;gfx.Start=(EFI_DRIVER_BINDING_START)(UINTN)0x100200;gfx.Stop=(EFI_DRIVER_BINDING_STOP)(UINTN)0x100300;
  splitter.DriverBindingHandle=splitter_agent;splitter.ImageHandle=(VOID*)31;splitter.Version=0x10;
  splitter.Supported=(EFI_DRIVER_BINDING_SUPPORTED)(UINTN)0x200100;splitter.Start=(EFI_DRIVER_BINDING_START)(UINTN)0x200200;splitter.Stop=(EFI_DRIVER_BINDING_STOP)(UINTN)0x200300;
  gfx_image.ImageBase=(VOID*)0x100000;gfx_image.ImageSize=21696;gfx_image.ImageCodeType=EfiBootServicesCode;gfx_image.FilePath=(VOID*)GfxPath;
  splitter_image.ImageBase=(VOID*)0x200000;splitter_image.ImageSize=25760;splitter_image.ImageCodeType=EfiBootServicesCode;splitter_image.FilePath=(VOID*)SplitterPath;
#ifdef GU_NATIVE_CAP01_BUILD
  memset(&nv,0,sizeof(nv));memset(&nv_image,0,sizeof(nv_image));memset(nv_path,0,sizeof(nv_path));
  const CHAR16 name[]=L"nvgop-ga1xx";nv_path[0]=4;nv_path[1]=4;nv_path[2]=28;memcpy(nv_path+4,name,sizeof(name));
  nv_path[28]=0x7f;nv_path[29]=0xff;nv_path[30]=4;
  o->image=o->agent;o->version=0x6000a;o->size=179776;o->base=(VOID*)0x400000;o->stop=(EFI_DRIVER_BINDING_STOP)(UINTN)0x400698;
  nv.Version=o->version;nv.ImageHandle=o->image;nv.DriverBindingHandle=o->agent;nv.Stop=o->stop;
  nv.Supported=(EFI_DRIVER_BINDING_SUPPORTED)(UINTN)0x400354;nv.Start=(EFI_DRIVER_BINDING_START)(UINTN)0x400774;
  nv_image.ImageBase=o->base;nv_image.ImageSize=o->size;nv_image.DeviceHandle=Gu.handle;nv_image.ImageCodeType=EfiBootServicesCode;nv_image.FilePath=(VOID*)nv_path;
#endif
}
int main(void) {
  GU_RELEASE_OWNER o;GU_CONSOLE_PLAN plan;
  for(fault=0;fault<=14;fault++) {
    setup(&o);
    if(fault==5)gfx.Version++;
    if(fault==6)splitter_image.ImageSize++;
    if(fault==7)gfx.Stop=(EFI_DRIVER_BINDING_STOP)(UINTN)0x900000;
    if(fault==8)Gu.target_subsystem++;
    EFI_STATUS s=GuPlanConsoleRelease(&table,&o,&plan);
    if((fault>=1 && fault<=8) || fault==14)assert(EFI_ERROR(s) && calls==0);
    else {
      assert(s==EFI_SUCCESS);
      s=GuReleaseConsoleConsumers(&table,&o,&plan);
      if(!fault)assert(s==EFI_SUCCESS && calls==2 && stage==2 && table.ConIn==&keys && table.ConOut==&console);
      else assert(EFI_ERROR(s) && calls==(fault<=11?1:2));
    }
    assert(!live);
  }
  fault=0;setup(&o);assert(!EFI_ERROR(GuPlanConsoleRelease(&table,&o,&plan)));gfx_image.ImageBase=(VOID*)0x100001;
  assert(EFI_ERROR(GuReleaseConsoleConsumers(&table,&o,&plan)) && !calls && !live);
#ifdef GU_NATIVE_CAP01_BUILD
  for(int c=0;c<6;c++) {
    setup(&o);
    if(c==0)o.version=0x24;
    if(c==1)nv_path[1]=6;
    if(c==2)nv_image.DeviceHandle=(VOID*)999;
    if(c==3)nv_image.ImageSize++;
    if(c==4)nv.Start=(EFI_DRIVER_BINDING_START)(UINTN)0x400775;
    if(c==5)o.stop=(EFI_DRIVER_BINDING_STOP)(UINTN)0x400699;
    assert(EFI_ERROR(GuPlanConsoleRelease(&table,&o,&plan)) && !calls && !live);
  }
  puts("PASS native CAP01: CSM/version/path/device/size/callback mismatch rejected before detach");
#endif
  puts("PASS CAP00 console release: exact graph and firmware identities, ordered detach, 14 fault paths, changed-plan refusal, no GPU register callbacks");
  return 0;
}
