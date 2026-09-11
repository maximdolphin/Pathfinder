// The ship panel: what the systems say, drawn over the view. T119, T122,
// T125 and T128 -- the power a shield takes from thrust, what is in the hold,
// every gauge through the one data path, and a fitting shown before it is
// made. ponytail: canvas text and boxes rather than widgets; T146 is where
// the real flight instruments arrive.

#pragma once

#include "CoreMinimal.h"
#include "GameFramework/HUD.h"
#include "LedgerHUD.generated.h"

UCLASS()
class LEDGERCLIENT_API ALedgerHUD : public AHUD
{
	GENERATED_BODY()

public:
	virtual void DrawHUD() override;
};
