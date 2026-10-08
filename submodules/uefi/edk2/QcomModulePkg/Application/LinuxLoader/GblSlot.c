#include <Uefi.h>
#include <Library/BaseLib.h>
#include <Library/BaseMemoryLib.h>
#define AVB_COMPILATION
#include "../../Library/FastbootLib/AvbSha/avb_sha.h"
#undef AVB_COMPILATION
#include <Library/MemoryAllocationLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/LinuxLoaderLib.h>
#include "SuperFbSlots.h"
#include "SuperFbLaunchPolicy.h"
#include "Hook/HookCommon.h"

static UINT32 Read32 (CONST UINT8 *Data) {
  return (UINT32)Data[0] | ((UINT32)Data[1] << 8) |
         ((UINT32)Data[2] << 16) | ((UINT32)Data[3] << 24);
}

static BOOLEAN Hex32 (CONST UINT8 *Data, UINT32 *Value) {
  UINTN Index;
  UINT32 Digit;
  *Value = 0;
  for (Index = 0; Index < 8; Index++) {
    if (Data[Index] >= '0' && Data[Index] <= '9') Digit = Data[Index] - '0';
    else if (Data[Index] >= 'a' && Data[Index] <= 'f') Digit = Data[Index] - 'a' + 10;
    else if (Data[Index] >= 'A' && Data[Index] <= 'F') Digit = Data[Index] - 'A' + 10;
    else return FALSE;
    *Value = (*Value << 4) | Digit;
  }
  return TRUE;
}

static EFI_STATUS LaunchPayload (UINT8 *Archive, UINTN Size) {
  UINT32 Length, NameLength, Sizes[3] = {0};
  UINTN Start, Index, Offset = 0;
  UINT8 *Parts[3];
  UINT8 *Payload = NULL;
  UINT32 PayloadSize = 0;
  BOOLEAN Trailer = FALSE;
  AvbSHA256Ctx Hash;
  SFB_MODE2_PROFILE Profile;
  SFB_TZ_MAP Map;
  EFI_HANDLE Child = NULL;
  EFI_STATUS Status;
  while (Offset <= Size && Size - Offset >= 110) {
    UINT8 *Header = Archive + Offset;
    UINTN NameStart = Offset + 110;
    if (CompareMem (Header, "070701", 6) != 0 ||
        !Hex32 (Header + 54, &Length) || !Hex32 (Header + 94, &NameLength) ||
        NameLength == 0 || NameLength > 64 || NameLength > Size - NameStart ||
        Archive[NameStart + NameLength - 1] != 0)
      return EFI_COMPROMISED_DATA;
    Start = (NameStart + NameLength + 3) & ~(UINTN)3;
    if (Start > Size || Length > Size - Start) return EFI_COMPROMISED_DATA;
    if (NameLength == sizeof ("TRAILER!!!") &&
        CompareMem (Archive + NameStart, "TRAILER!!!", NameLength) == 0) {
      if (Length != 0) return EFI_COMPROMISED_DATA;
      Trailer = TRUE;
      Offset = Start;
      break;
    }
    if (NameLength != sizeof ("gbl/abl-chainload.bin") ||
        CompareMem (Archive + NameStart, "gbl/abl-chainload.bin", NameLength) != 0 ||
        Payload != NULL) return EFI_COMPROMISED_DATA;
    Payload = Archive + Start;
    PayloadSize = Length;
    Offset = (Start + Length + 3) & ~(UINTN)3;
  }
  if (!Trailer) return EFI_COMPROMISED_DATA;
  while (Offset < Size) if (Archive[Offset++] != 0) return EFI_COMPROMISED_DATA;
  if (Payload == NULL || PayloadSize < 120 ||
      CompareMem (Payload, "GBLSLOT1", 8) != 0 || Read32 (Payload + 8) != 1)
    return EFI_COMPROMISED_DATA;
  for (Index = 0; Index < 3; Index++) Sizes[Index] = Read32 (Payload + 12 + Index * 4);
  if (Sizes[0] < 64 || Sizes[0] > 16 * 1024 * 1024 || Sizes[1] != 120 ||
      Sizes[2] != 256 || PayloadSize != 120 + Sizes[0] + 376)
    return EFI_COMPROMISED_DATA;
  Offset = 120;
  for (Index = 0; Index < 3; Index++) {
    Parts[Index] = Payload + Offset;
    avb_sha256_init (&Hash);
    avb_sha256_update (&Hash, Parts[Index], Sizes[Index]);
    if (CompareMem (avb_sha256_final (&Hash), Payload + 24 + Index * 32, 32) != 0)
      return EFI_COMPROMISED_DATA;
    Offset += Sizes[Index];
  }
  if (!SfbProfileParse (Parts[1], 120, &Profile) ||
      !SfbTzMapParse (Parts[2], 256, &Map))
    return EFI_COMPROMISED_DATA;
  SfbBypassSecurity ();
  Status = gBS->LoadImage (FALSE, gImageHandle, NULL, Parts[0], Sizes[0], &Child);
  SfbRestoreSecurity ();
  if (EFI_ERROR (Status)) return Status;
  Status = SfbPrepareManagedAblHooks (SfbBootModeKmProfile, &Profile, &Map, SfbConfigLockNever);
  if (!EFI_ERROR (Status)) Status = gBS->StartImage (Child, NULL, NULL);
  SfbDisarmManagedAblHooks ();
  gBS->UnloadImage (Child);
  return EFI_ERROR (Status) ? Status : EFI_ABORTED;
}

EFI_STATUS GblLaunchSlot (VOID) {
  HandleInfo Handles[2];
  PartiSelectFilter Filter;
  UINT32 Count = 2, Block, Page, Ramdisk, Dtb, TableSize, Entries, Stride;
  UINTN Bytes, Table, Base, Index;
  EFI_BLOCK_IO_PROTOCOL *Io;
  UINT8 *Image;
  EFI_STATUS Status;
  SFB_SLOT Slot = SfbActiveSlot ();
  if (Slot == SfbSlotUnknown) return EFI_NOT_READY;
  ZeroMem (&Filter, sizeof (Filter));
  ZeroMem (Handles, sizeof (Handles));
  Filter.PartitionLabel = Slot == SfbSlotA ? L"vendor_boot_a" : L"vendor_boot_b";
  Status = GetBlkIOHandles (BLK_IO_SEL_PARTITIONED_GPT |
      BLK_IO_SEL_MEDIA_TYPE_NON_REMOVABLE | BLK_IO_SEL_MATCH_PARTITION_LABEL,
      &Filter, Handles, &Count);
  if (EFI_ERROR (Status) || Count != 1 || Handles[0].BlkIo == NULL)
    return EFI_NOT_READY;
  Io = Handles[0].BlkIo;
  if (Io->Media == NULL || !Io->Media->MediaPresent) return EFI_NO_MEDIA;
  Block = Io->Media->BlockSize;
  if (Block == 0 || Block > 65536 || Io->Media->LastBlock >= 128 * 1024 * 1024 / Block)
    return EFI_BAD_BUFFER_SIZE;
  Bytes = (UINTN)(Io->Media->LastBlock + 1) * Block;
  if (Bytes < 2128) return EFI_COMPROMISED_DATA;
  Image = AllocateAlignedPool (Bytes, Io->Media->IoAlign > 1 ? Io->Media->IoAlign : 8);
  if (Image == NULL) return EFI_OUT_OF_RESOURCES;
  Status = Io->ReadBlocks (Io, Io->Media->MediaId, 0, Bytes, Image);
  if (EFI_ERROR (Status)) goto Done;
  Status = EFI_COMPROMISED_DATA;
  if (CompareMem (Image, "VNDRBOOT", 8) != 0) goto Done;
  if (Read32 (Image + 8) == 3) { Status = EFI_NOT_FOUND; goto Done; }
  Page = Read32 (Image + 12);
  if (Read32 (Image + 8) != 4 || Page < 2128 || Page > 65536 ||
      (Page & (Page - 1)) != 0 || Read32 (Image + 2096) != 2128) goto Done;
  Ramdisk = Read32 (Image + 24);
  Dtb = Read32 (Image + 2100);
  TableSize = Read32 (Image + 2112);
  Entries = Read32 (Image + 2116);
  Stride = Read32 (Image + 2120);
  if (Stride != 108 || Entries > 1024 || TableSize != Entries * Stride) goto Done;
  Base = Page;
  Table = Base + (((UINT64)Ramdisk + Page - 1) & ~(UINT64)(Page - 1)) +
                 (((UINT64)Dtb + Page - 1) & ~(UINT64)(Page - 1));
  if (Table > Bytes || TableSize > Bytes - Table || Ramdisk > Bytes - Base) goto Done;
  Status = EFI_NOT_FOUND;
  for (Index = 0; Index < Entries; Index++) {
    UINT8 *Entry = Image + Table + Index * Stride;
    UINT32 Size = Read32 (Entry), Offset = Read32 (Entry + 4);
    if (Offset > Ramdisk || Size > Ramdisk - Offset) { Status = EFI_COMPROMISED_DATA; goto Done; }
    if (CompareMem (Entry + 12, "gbl\0", 4) == 0) {
      UINTN Other;
      for (Other = Index + 1; Other < Entries; Other++) {
        if (CompareMem (Image + Table + Other * Stride + 12, "gbl\0", 4) == 0) {
          Status = EFI_COMPROMISED_DATA; goto Done;
        }
      }
      if (Read32 (Entry + 8) != 1) { Status = EFI_COMPROMISED_DATA; goto Done; }
      Status = LaunchPayload (Image + Base + Offset, Size);
      goto Done;
    }
  }
Done:
  FreeAlignedPool (Image);
  return Status;
}
