#include <Uefi.h>
#include <PiDxe.h>
#include <Library/DxeServicesTableLib.h>
#include <Library/TimerLib.h>
#include <Library/UefiBootServicesTableLib.h>
#include <Library/UefiLib.h>
#include <Library/UefiRuntimeServicesTableLib.h>

EFI_STATUS EFIAPI LinuxLoaderEntry(EFI_HANDLE ImageHandle,
                                   EFI_SYSTEM_TABLE *SystemTable);
EFI_STATUS EFIAPI UefiBootServicesTableLibConstructor(
    EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);
EFI_STATUS EFIAPI UefiRuntimeServicesTableLibConstructor(
    EFI_HANDLE ImageHandle, EFI_SYSTEM_TABLE *SystemTable);
EFI_STATUS EFIAPI UefiLibConstructor(EFI_HANDLE ImageHandle,
                                     EFI_SYSTEM_TABLE *SystemTable);
EFI_STATUS EFIAPI DxeServicesTableLibConstructor(EFI_HANDLE ImageHandle,
                                                 EFI_SYSTEM_TABLE *SystemTable);
RETURN_STATUS EFIAPI TimerConstructor(VOID);

EFI_STATUS EFIAPI _ModuleEntryPoint(EFI_HANDLE ImageHandle,
                                    EFI_SYSTEM_TABLE *SystemTable) {
  EFI_STATUS Status =
      UefiBootServicesTableLibConstructor(ImageHandle, SystemTable);
  if (EFI_ERROR(Status))
    return Status;
  Status = UefiRuntimeServicesTableLibConstructor(ImageHandle, SystemTable);
  if (EFI_ERROR(Status))
    return Status;
  Status = UefiLibConstructor(ImageHandle, SystemTable);
  if (EFI_ERROR(Status))
    return Status;
  Status = DxeServicesTableLibConstructor(ImageHandle, SystemTable);
  if (EFI_ERROR(Status))
    return Status;
  Status = TimerConstructor();
  if (EFI_ERROR(Status))
    return Status;
  return LinuxLoaderEntry(ImageHandle, SystemTable);
}

VOID EFIAPI Exit(EFI_STATUS Status) {
  gBS->Exit(gImageHandle, Status, 0, NULL);
}
