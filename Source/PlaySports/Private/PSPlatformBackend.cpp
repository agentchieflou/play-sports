#include "PSPlatformBackend.h"
#include "PSPlatformServices.h"

void UPSPlatformBackend::Startup(UPSPlatformServices* InServices)
{
    Services = InServices;
}

void UPSPlatformBackend::Shutdown()
{
    Services.Reset();
}
