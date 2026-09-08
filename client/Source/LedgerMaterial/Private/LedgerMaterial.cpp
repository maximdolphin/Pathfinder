// Every Unreal module needs one of these. Without it the module loads and then
// fails to initialise, with an error that names the module and not the cause.

#include "Modules/ModuleManager.h"

IMPLEMENT_GAME_MODULE(FDefaultGameModuleImpl, LedgerMaterial);
