/* SPDX-License-Identifier: GPL-2.0-only */
#include "../port.h"
#include <assert.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "../app_io.c"

GU_PLATFORM Gu;
GU_ASSET GuAssets[5];
VOID *GuAlloc(size_t n) { return calloc(1,n); }
VOID GuFree(VOID *p) { free(p); }
BOOLEAN GuSafeToReturn(VOID) { return !Gu.fault && !Gu.dma_count && !Gu.retained; }
static EFI_BOOT_SERVICES services;
static EFI_SYSTEM_TABLE table;
static EFI_SIMPLE_FILE_SYSTEM_PROTOCOL fs;
static EFI_FILE_PROTOCOL root,file;
static int fail_at,file_closed,root_closed,flushes,writes;
static char saved[4096];
static EFI_STATUS EFIAPI handle(EFI_HANDLE h,EFI_GUID *g,VOID **out) {
  if(fail_at==1)return EFI_NOT_FOUND;*out=&fs;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI volume(EFI_SIMPLE_FILE_SYSTEM_PROTOCOL *f,EFI_FILE_PROTOCOL **out) {
  if(fail_at==2)return EFI_DEVICE_ERROR;*out=&root;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI open_file(EFI_FILE_PROTOCOL *r,EFI_FILE_PROTOCOL **out,CHAR16 *path,UINT64 mode,UINT64 attrs) {
  assert(path==HandoffTracePath && mode==(EFI_FILE_MODE_READ|EFI_FILE_MODE_WRITE|EFI_FILE_MODE_CREATE));
  if(fail_at==3)return EFI_DEVICE_ERROR;*out=&file;return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI position(EFI_FILE_PROTOCOL *f,UINT64 pos) {
  assert(pos==0xffffffffffffffffULL);return fail_at==4?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI write_file(EFI_FILE_PROTOCOL *f,UINTN *n,VOID *data) {
  writes++;if(fail_at==5)return EFI_DEVICE_ERROR;
  if(fail_at==6){(*n)--;return EFI_SUCCESS;}
  assert(strlen(saved)+*n<sizeof(saved));strncat(saved,data,*n);return EFI_SUCCESS;
}
static EFI_STATUS EFIAPI flush(EFI_FILE_PROTOCOL *f) {
  flushes++;return fail_at==7?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static EFI_STATUS EFIAPI close_file(EFI_FILE_PROTOCOL *f) {
  if(f==&file){file_closed++;return fail_at==8?EFI_DEVICE_ERROR:EFI_SUCCESS;}
  assert(f==&root);root_closed++;return fail_at==9?EFI_DEVICE_ERROR:EFI_SUCCESS;
}
static void setup(void) {
  memset(&Gu,0,sizeof(Gu));memset(&services,0,sizeof(services));memset(&table,0,sizeof(table));
  memset(&fs,0,sizeof(fs));memset(&root,0,sizeof(root));memset(&file,0,sizeof(file));
  memset(HandoffTracePath,0,sizeof(HandoffTracePath));
  const CHAR16 path[]=L"\\RTX4090-PREP\\RUN04\\HANDOFF.TXT";
  memcpy(HandoffTracePath,path,sizeof(path));
  services.HandleProtocol=handle;table.BootServices=&services;Table=&table;
  BootDeviceHandle=(VOID*)1;fs.OpenVolume=volume;root.Open=open_file;root.Close=close_file;
  file.SetPosition=position;file.Write=write_file;file.Flush=flush;file.Close=close_file;
  fail_at=file_closed=root_closed=flushes=writes=0;saved[0]=0;
  Report=Run=Parent=Root=NULL;
}
int main(void) {
  setup();
  assert(GuTraceHandoff("usb_log_closed_before_StartImage",EFI_SUCCESS)==EFI_SUCCESS);
  assert(GuTraceHandoff("StartImage_RETURNED",EFI_UNSUPPORTED)==EFI_SUCCESS);
  assert(strstr(saved,"stage=usb_log_closed_before_StartImage status=0x0000000000000000"));
  assert(strstr(saved,"stage=StartImage_RETURNED status=0x8000000000000003"));
  assert(file_closed==2 && root_closed==2 && flushes==2 && writes==2);
  for(int c=1;c<=9;c++) {
    setup();fail_at=c;
    assert(EFI_ERROR(GuTraceHandoff("before_StartImage",EFI_SUCCESS)));
    assert(root_closed==(c>=3));assert(file_closed==(c>=4));
  }
  setup();Gu.fault=TRUE;assert(GuTraceHandoff("bad",0)==EFI_NOT_READY && !writes);
  setup();BootDeviceHandle=NULL;assert(GuTraceHandoff("bad",0)==EFI_NOT_READY && !writes);
  puts("PASS: closed-log handoff trace, exact returned EFI status, 9 I/O failures, unsafe/missing-device refusals (UEFI simulated)");
  return 0;
}
