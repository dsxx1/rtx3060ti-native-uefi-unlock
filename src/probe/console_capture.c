/* SPDX-License-Identifier: GPL-2.0-only
 * Observe protocol ownership and console metadata. No controller transitions,
 * hardware register access, driver callbacks, variable changes or OS chainload.
 */
#include "console_capture.h"
#include "format.h"
#include "runtime.h"
#include <Protocol/DevicePath.h>
#include <Protocol/GraphicsOutput.h>
#include <Protocol/DriverBinding.h>
#include <Protocol/LoadedImage.h>
#include <Protocol/SimpleTextOut.h>
#include <stdarg.h>

#define CAPACITY (4U*1024U*1024U)
static EFI_GUID Gop=EFI_GRAPHICS_OUTPUT_PROTOCOL_GUID,Path=EFI_DEVICE_PATH_PROTOCOL_GUID,
  Driver=EFI_DRIVER_BINDING_PROTOCOL_GUID,Loaded=EFI_LOADED_IMAGE_PROTOCOL_GUID,
  Pci=EFI_PCI_IO_PROTOCOL_GUID,TextOut=EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL_GUID;
typedef struct {char *Text;UINTN Used;BOOLEAN Full;} REPORT;
static VOID Add(REPORT *r,const char *fmt,...) {
  if(r->Full)return;
  va_list ap;va_start(ap,fmt);size_t n=GuFormatV(r->Text+r->Used,CAPACITY-r->Used,fmt,ap);va_end(ap);
  if(n>=CAPACITY-r->Used){r->Full=TRUE;return;}r->Used+=n;
}
static VOID Guid(REPORT *r,const EFI_GUID *g) {
  Add(r,"%08x-%04x-%04x-",g->Data1,(unsigned)g->Data2,(unsigned)g->Data3);
  for(UINTN i=0;i<8;i++){Add(r,"%02x",(unsigned)g->Data4[i]);if(i==1)Add(r,"-");}
}
static VOID DevicePath(REPORT *r,const EFI_DEVICE_PATH_PROTOCOL *path) {
  if(!path){Add(r,"null\n");return;}
  UINTN off=0;
  for(UINTN node=0;node<128 && off<=4092;node++) {
    const EFI_DEVICE_PATH_PROTOCOL *p=(const VOID*)((const UINT8*)path+off);
    UINTN n=p->Length[0]+((UINTN)p->Length[1]<<8);
    if(n<4 || n>4096-off){Add(r,"invalid_node offset=%zu length=%zu\n",(size_t)off,(size_t)n);return;}
    Add(r,"node type=%u subtype=%u bytes=%zu raw=",(unsigned)p->Type,(unsigned)p->SubType,(size_t)n);
    for(UINTN j=0;j<n;j++)Add(r,"%02x",(unsigned)((const UINT8*)p)[j]);Add(r,"\n");
    off+=n;
    if(p->Type==END_DEVICE_PATH_TYPE){Add(r,"end valid=%u\n",p->SubType==END_ENTIRE_DEVICE_PATH_SUBTYPE && n==4);return;}
  }
  Add(r,"path_limit_reached\n");
}
EFI_STATUS GuConsoleCapture(EFI_SYSTEM_TABLE *t,GU_CAPTURE_SAVE save,VOID *context) {
  if(!t || !t->BootServices || !save)return EFI_INVALID_PARAMETER;
  EFI_BOOT_SERVICES *bs=t->BootServices;
  if(!bs->LocateHandleBuffer || !bs->ProtocolsPerHandle || !bs->OpenProtocolInformation ||
     !bs->HandleProtocol || !bs->AllocatePool || !bs->FreePool)return EFI_INVALID_PARAMETER;
  REPORT r={0};EFI_HANDLE *handles=NULL;UINTN count=0;EFI_STATUS result=EFI_SUCCESS;
  EFI_STATUS s=bs->AllocatePool(EfiBootServicesData,CAPACITY,(VOID**)&r.Text);
  if(EFI_ERROR(s)||!r.Text)return EFI_OUT_OF_RESOURCES;
  Add(&r,"format=UEFI-CONSOLE-OBSERVE-V1\nunlock=0\nhardware_reads=0\nhardware_writes=0\ncontroller_changes=0\nfirmware_launches=0\n");
  Add(&r,"firmware_revision=%#x uefi_revision=%#x\n",t->FirmwareRevision,t->Hdr.Revision);
  Add(&r,"console_out=%#llx console_in=%#llx stderr=%#llx ConOut=%#llx ConIn=%#llx StdErr=%#llx\n",
      (unsigned long long)(UINTN)t->ConsoleOutHandle,(unsigned long long)(UINTN)t->ConsoleInHandle,
      (unsigned long long)(UINTN)t->StandardErrorHandle,(unsigned long long)(UINTN)t->ConOut,
      (unsigned long long)(UINTN)t->ConIn,(unsigned long long)(UINTN)t->StdErr);
  s=bs->LocateHandleBuffer(AllHandles,NULL,NULL,&count,&handles);
  Add(&r,"enumeration_status=%#llx handle_count=%zu\n",(unsigned long long)s,(size_t)count);
  if(EFI_ERROR(s)||count>4096 || (count && !handles)){result=EFI_ERROR(s)?s:EFI_BAD_BUFFER_SIZE;goto done;}
  UINTN edges=0,gops=0;
  for(UINTN i=0;i<count && !r.Full;i++) {
    EFI_GUID **protocols=NULL;UINTN n=0;
    Add(&r,"\nHANDLE index=%zu address=%#llx console_out=%u console_in=%u stderr=%u\n",(size_t)i,
        (unsigned long long)(UINTN)handles[i],handles[i]==t->ConsoleOutHandle,handles[i]==t->ConsoleInHandle,handles[i]==t->StandardErrorHandle);
    s=bs->ProtocolsPerHandle(handles[i],&protocols,&n);
    Add(&r,"protocols status=%#llx count=%zu\n",(unsigned long long)s,(size_t)n);
    if(EFI_ERROR(s)||n>256 || (n && !protocols)) {
      if(protocols)bs->FreePool(protocols);result=EFI_ERROR(s)?s:EFI_BAD_BUFFER_SIZE;break;
    }
    for(UINTN j=0;j<n && !r.Full;j++) {
      if(!protocols[j]){result=EFI_COMPROMISED_DATA;break;}
      EFI_GUID *g=protocols[j];VOID *interface=NULL;
      Add(&r,"PROTOCOL ");Guid(&r,g);
      EFI_STATUS hs=bs->HandleProtocol(handles[i],g,&interface);
      Add(&r," interface=%#llx status=%#llx\n",(unsigned long long)(UINTN)interface,(unsigned long long)hs);
      EFI_OPEN_PROTOCOL_INFORMATION_ENTRY *entries=NULL;UINTN en=0;
      EFI_STATUS es=bs->OpenProtocolInformation(handles[i],g,&entries,&en);
      Add(&r,"opens status=%#llx count=%zu\n",(unsigned long long)es,(size_t)en);
      if(EFI_ERROR(es)||en>1024 || (en && !entries)) {
        if(entries)bs->FreePool(entries);result=EFI_ERROR(es)?es:EFI_BAD_BUFFER_SIZE;break;
      }
      for(UINTN e=0;e<en;e++) {
        edges++;Add(&r,"EDGE agent=%#llx controller=%#llx attributes=%#x count=%u\n",
          (unsigned long long)(UINTN)entries[e].AgentHandle,(unsigned long long)(UINTN)entries[e].ControllerHandle,
          entries[e].Attributes,entries[e].OpenCount);
      }
      if(entries)bs->FreePool(entries);
      if(EFI_ERROR(hs)||!interface)continue;
      if(!memcmp(g,&Path,sizeof(*g))) {Add(&r,"DEVICE_PATH\n");DevicePath(&r,interface);}
      if(!memcmp(g,&Pci,sizeof(*g)))Add(&r,"PCI_IO present; no hardware methods called\n");
      if(!memcmp(g,&Gop,sizeof(*g))) {
        EFI_GRAPHICS_OUTPUT_PROTOCOL *p=interface;gops++;
        Add(&r,"GOP QueryMode=%#llx SetMode=%#llx Blt=%#llx Mode=%#llx\n",
          (unsigned long long)(UINTN)p->QueryMode,(unsigned long long)(UINTN)p->SetMode,
          (unsigned long long)(UINTN)p->Blt,(unsigned long long)(UINTN)p->Mode);
        if(p->Mode) {
          Add(&r,"GOP mode=%u max=%u framebuffer=%#llx bytes=%zu info_bytes=%zu\n",p->Mode->Mode,p->Mode->MaxMode,
            (unsigned long long)p->Mode->FrameBufferBase,(size_t)p->Mode->FrameBufferSize,(size_t)p->Mode->SizeOfInfo);
          if(p->Mode->Info && p->Mode->SizeOfInfo>=sizeof(*p->Mode->Info)) {
            EFI_GRAPHICS_OUTPUT_MODE_INFORMATION *in=p->Mode->Info;
            Add(&r,"GOP resolution=%ux%u stride=%u format=%u\n",in->HorizontalResolution,in->VerticalResolution,in->PixelsPerScanLine,(unsigned)in->PixelFormat);
          }
        }
      }
      if(!memcmp(g,&Driver,sizeof(*g))) {
        EFI_DRIVER_BINDING_PROTOCOL *p=interface;
        Add(&r,"DRIVER version=%#x image=%#llx binding=%#llx supported=%#llx start=%#llx stop=%#llx\n",p->Version,
          (unsigned long long)(UINTN)p->ImageHandle,(unsigned long long)(UINTN)p->DriverBindingHandle,
          (unsigned long long)(UINTN)p->Supported,(unsigned long long)(UINTN)p->Start,(unsigned long long)(UINTN)p->Stop);
      }
      if(!memcmp(g,&Loaded,sizeof(*g))) {
        EFI_LOADED_IMAGE_PROTOCOL *p=interface;
        Add(&r,"IMAGE base=%#llx bytes=%llu device=%#llx parent=%#llx code=%u data=%u\n",
          (unsigned long long)(UINTN)p->ImageBase,(unsigned long long)p->ImageSize,
          (unsigned long long)(UINTN)p->DeviceHandle,(unsigned long long)(UINTN)p->ParentHandle,(unsigned)p->ImageCodeType,(unsigned)p->ImageDataType);
        Add(&r,"IMAGE_PATH\n");DevicePath(&r,p->FilePath);
      }
      if(!memcmp(g,&TextOut,sizeof(*g))) {
        EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *p=interface;
        Add(&r,"TEXT_OUT output=%#llx mode=%#llx\n",(unsigned long long)(UINTN)p->OutputString,(unsigned long long)(UINTN)p->Mode);
      }
    }
    if(protocols)bs->FreePool(protocols);
    if(EFI_ERROR(result))break;
  }
  Add(&r,"\nSUMMARY handles=%zu edges=%zu gops=%zu complete=%u\n",(size_t)count,(size_t)edges,(size_t)gops,!EFI_ERROR(result) && !r.Full);
done:
  if(handles)bs->FreePool(handles);
  if(r.Full)result=EFI_BAD_BUFFER_SIZE;
  Add(&r,"capture_status=%#llx\n",(unsigned long long)result);
  s=save(L"CONSOLE.TXT",r.Text,r.Used,context);
  bs->FreePool(r.Text);
  return EFI_ERROR(s)?s:result;
}
