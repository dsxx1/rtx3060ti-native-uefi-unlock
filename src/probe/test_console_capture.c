/* SPDX-License-Identifier: GPL-2.0-only */
#include "console_capture.c"
#include <assert.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
static int fault,live,saves;
static EFI_GUID *guids[]={&Gop,&Driver,&Loaded,&Path,&TextOut,&Pci};
static EFI_GRAPHICS_OUTPUT_MODE_INFORMATION info={.HorizontalResolution=1024,.VerticalResolution=768,.PixelsPerScanLine=1024};
static EFI_GRAPHICS_OUTPUT_PROTOCOL_MODE mode={.MaxMode=2,.Mode=1,.Info=&info,.SizeOfInfo=sizeof(info),.FrameBufferBase=0x90000000,.FrameBufferSize=3145728};
static EFI_GRAPHICS_OUTPUT_PROTOCOL gop={.Mode=&mode};
static EFI_DRIVER_BINDING_PROTOCOL binding={.Version=0x24,.ImageHandle=(VOID*)2,.DriverBindingHandle=(VOID*)1};
static EFI_DEVICE_PATH_PROTOCOL end={.Type=END_DEVICE_PATH_TYPE,.SubType=END_ENTIRE_DEVICE_PATH_SUBTYPE,.Length={4,0}};
static EFI_LOADED_IMAGE_PROTOCOL image={.ImageBase=(VOID*)0x1000,.ImageSize=4096,.FilePath=&end};
static EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL text;
static char captured[16384];
static EFI_STATUS EFIAPI alloc(EFI_MEMORY_TYPE t,UINTN n,VOID **p) {*p=calloc(1,n);live++;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI release(VOID *p) {free(p);live--;return EFI_SUCCESS;}
static EFI_STATUS EFIAPI handles(EFI_LOCATE_SEARCH_TYPE t,EFI_GUID *g,VOID *k,UINTN *n,EFI_HANDLE **p) {
  assert(t==AllHandles);*n=fault==1?4097:1;alloc(0,sizeof(**p),(VOID**)p);(*p)[0]=(VOID*)1;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI protocols(EFI_HANDLE h,EFI_GUID ***p,UINTN *n) {
  if(fault==2)return EFI_DEVICE_ERROR;
  *n=6;alloc(0,sizeof(guids),(VOID**)p);memcpy(*p,guids,sizeof(guids));return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI opens(EFI_HANDLE h,EFI_GUID *g,EFI_OPEN_PROTOCOL_INFORMATION_ENTRY **p,UINTN *n) {
  if(fault==3)return EFI_DEVICE_ERROR;
  *n=1;alloc(0,sizeof(**p),(VOID**)p);(*p)[0].AgentHandle=(VOID*)2;(*p)[0].ControllerHandle=(VOID*)1;
  (*p)[0].Attributes=EFI_OPEN_PROTOCOL_BY_DRIVER;(*p)[0].OpenCount=1;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI protocol(EFI_HANDLE h,EFI_GUID *g,VOID **p) {
  if(!memcmp(g,&Gop,sizeof(*g)))*p=&gop;
  else if(!memcmp(g,&Driver,sizeof(*g)))*p=&binding;
  else if(!memcmp(g,&Loaded,sizeof(*g)))*p=&image;
  else if(!memcmp(g,&Path,sizeof(*g)))*p=&end;
  else if(!memcmp(g,&TextOut,sizeof(*g)))*p=&text;
  else *p=&end;
  return EFI_SUCCESS;
}
static EFI_STATUS save(const CHAR16 *name,const VOID *data,UINTN n,VOID *context) {
  assert(name[0]=='C' && n<sizeof(captured));saves++;memcpy(captured,data,n);captured[n]=0;
  return fault==4?EFI_VOLUME_FULL:EFI_SUCCESS;
}
int main(void) {
  EFI_BOOT_SERVICES bs={.AllocatePool=alloc,.FreePool=release,.LocateHandleBuffer=handles,.ProtocolsPerHandle=protocols,.OpenProtocolInformation=opens,.HandleProtocol=protocol};
  EFI_SYSTEM_TABLE t={.BootServices=&bs,.ConsoleOutHandle=(VOID*)1};
  for(fault=0;fault<=4;fault++) {
    live=saves=0;captured[0]=0;
    EFI_STATUS s=GuConsoleCapture(&t,save,NULL);
    assert(!live && saves==1);
    if(!fault) {
      assert(s==EFI_SUCCESS && strstr(captured,"gops=1 complete=1"));
      assert(strstr(captured,"EDGE agent=0x2 controller=0x1 attributes=0x10 count=1"));
      assert(strstr(captured,"GOP resolution=1024x768") && strstr(captured,"DRIVER version=0x24"));
      assert(strstr(captured,"PCI_IO present; no hardware methods called"));
    } else assert(EFI_ERROR(s));
  }
  fault=0;end.Length[0]=0;assert(GuConsoleCapture(&t,save,NULL)==EFI_SUCCESS && strstr(captured,"invalid_node"));end.Length[0]=4;
  assert(GuConsoleCapture(NULL,save,NULL)==EFI_INVALID_PARAMETER);
  puts("PASS console graph: ownership edges, GOP/image/binding metadata, bounds and I/O failures, malformed path, zero live allocations; no hardware callbacks supplied");
  return 0;
}
