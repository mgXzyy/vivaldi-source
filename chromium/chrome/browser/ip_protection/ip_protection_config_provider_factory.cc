// Copyright 2023 The Chromium Authors
// Use of this source code is governed by a BSD-style license that can be
// found in the LICENSE file.

#include "chrome/browser/ip_protection/ip_protection_config_provider_factory.h"

#include "chrome/browser/ip_protection/ip_protection_config_provider.h"
#include "chrome/browser/profiles/profile.h"
#include "chrome/browser/profiles/profile_selections.h"
#include "chrome/browser/signin/identity_manager_factory.h"
#include "content/public/browser/browser_context.h"
#include "content/public/browser/storage_partition.h"

// static
IpProtectionConfigProvider* IpProtectionConfigProviderFactory::GetForProfile(
    Profile* profile) {
  return static_cast<IpProtectionConfigProvider*>(
      GetInstance()->GetServiceForBrowserContext(profile, /*create=*/true));
}

// static
IpProtectionConfigProviderFactory*
IpProtectionConfigProviderFactory::GetInstance() {
  static base::NoDestructor<IpProtectionConfigProviderFactory> instance;
  return instance.get();
}

// static
ProfileSelections IpProtectionConfigProviderFactory::CreateProfileSelections() {
  if (!base::FeatureList::IsEnabled(net::features::kEnableIpProtectionProxy)) {
    return ProfileSelections::BuildNoProfilesSelected();
  }
  // IP Protection usage requires that a Gaia account is available when
  // authenticating to the proxy (to prevent it from being abused). For
  // incognito mode, use the profile associated with the logged in user since
  // users will have a more private experience with IP Protection enabled.
  // Skip other profile types like Guest and System where no Gaia is available.
  return ProfileSelections::Builder()
      .WithRegular(ProfileSelection::kRedirectedToOriginal)
      .WithGuest(ProfileSelection::kNone)
      .WithSystem(ProfileSelection::kNone)
      .WithAshInternals(ProfileSelection::kNone)
      .Build();
}

IpProtectionConfigProviderFactory::IpProtectionConfigProviderFactory()
    : ProfileKeyedServiceFactory("IpProtectionConfigProviderFactory",
                                 CreateProfileSelections()) {
  DependsOn(IdentityManagerFactory::GetInstance());
}

IpProtectionConfigProviderFactory::~IpProtectionConfigProviderFactory() =
    default;

std::unique_ptr<KeyedService>
IpProtectionConfigProviderFactory::BuildServiceInstanceForBrowserContext(
    content::BrowserContext* context) const {
  Profile* profile = Profile::FromBrowserContext(context);
  return std::make_unique<IpProtectionConfigProvider>(
      IdentityManagerFactory::GetForProfile(profile),
      profile->GetDefaultStoragePartition()
          ->GetURLLoaderFactoryForBrowserProcess());
}

bool IpProtectionConfigProviderFactory::ServiceIsCreatedWithBrowserContext()
    const {
  // Auth tokens will be requested soon after `Profile()` creation (after the
  // per-profile `NetworkContext()` gets created) so instantiate the
  // `IpProtectionConfigProvider()` so that it already exists by the time
  // that request is made.
  return true;
}
