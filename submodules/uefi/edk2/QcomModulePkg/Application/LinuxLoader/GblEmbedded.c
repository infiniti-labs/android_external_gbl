#include <Uefi.h>
#include <Library/UefiBootServicesTableLib.h>
#include "SuperFbLaunchPolicy.h"
#include "Hook/HookCommon.h"
#include <GblEmbedded.h>

EFI_STATUS GblLaunchEmbedded (VOID) {
  SFB_MODE2_PROFILE Profile;
  SFB_TZ_MAP Map;
  EFI_HANDLE Child = NULL;
  EFI_STATUS Status;

  if (!SfbProfileParse (GblProfile, GblProfile_len, &Profile) ||
      !SfbTzMapParse (GblTzMap, GblTzMap_len, &Map))
    return EFI_COMPROMISED_DATA;
  SfbBypassSecurity ();
  Status = gBS->LoadImage (FALSE, gImageHandle, NULL,
                          GblLoader, GblLoader_len, &Child);
  SfbRestoreSecurity ();
  if (EFI_ERROR (Status)) return Status;
  Status = SfbPrepareManagedAblHooks (SfbBootModeKmProfile, &Profile, &Map,
                                     SfbConfigLockNever);
  if (!EFI_ERROR (Status)) Status = gBS->StartImage (Child, NULL, NULL);
  SfbDisarmManagedAblHooks ();
  gBS->UnloadImage (Child);
  return EFI_ERROR (Status) ? Status : EFI_ABORTED;
}
