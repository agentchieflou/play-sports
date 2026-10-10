#pragma once

#include "CoreMinimal.h"
#include "Modules/ModuleManager.h"

/**
 * Autonomix: in-editor agent actions (Core 25.1, 25.2), served as tools on AgenticLink's MCP
 * server (FAutonomixTools): import_t3d (FAutonomixT3D) with -AutonomixT3D, run_python
 * (FAutonomixPython) with -AutonomixPython. Without either switch, or without AgenticLink's
 * -AgenticLinkMcp, it only logs that it loaded.
 */
class FAutonomixModule : public IModuleInterface
{
public:
    virtual void StartupModule() override;
    virtual void ShutdownModule() override;

private:
    bool bProvidingTools = false;
};
