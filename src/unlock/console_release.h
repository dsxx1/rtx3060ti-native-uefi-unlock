/* SPDX-License-Identifier: GPL-2.0-only */
#pragma once
#include "ownership.h"
#include <Protocol/SimpleTextOut.h>
#include <Protocol/SimpleTextIn.h>
typedef struct {
  EFI_HANDLE child,gfx,splitter,console,input,error;
  EFI_SIMPLE_TEXT_OUTPUT_PROTOCOL *text,*stderr_text;
  EFI_SIMPLE_TEXT_INPUT_PROTOCOL *keys;
  EFI_DRIVER_BINDING_PROTOCOL gfx_binding,splitter_binding;
  VOID *gfx_base,*splitter_base;
} GU_CONSOLE_PLAN;
EFI_STATUS GuPlanConsoleRelease(EFI_SYSTEM_TABLE *Table,const GU_RELEASE_OWNER *Owner,GU_CONSOLE_PLAN *Plan);
EFI_STATUS GuReleaseConsoleConsumers(EFI_SYSTEM_TABLE *Table,const GU_RELEASE_OWNER *Owner,const GU_CONSOLE_PLAN *Plan);
