#include "LedgerShipSystems.h"

#include "Dom/JsonObject.h"
#include "Misc/DefaultValueHelper.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace
{
	/// A number, read exactly. The engine's JSON reader does not promise the
	/// last bit of a double (LedgerBody.cpp has the story), so a saved ship
	/// writes its numbers as "%.17g" strings and they come back through
	/// FDefaultValueHelper, which round-trips them; a number written by hand
	/// as a plain JSON number is read as one.
	double SystemsValue(const TSharedPtr<FJsonValue>& Value)
	{
		double Out = 0.0;
		if (Value.IsValid())
		{
			if (Value->Type == EJson::String)
			{
				FDefaultValueHelper::ParseDouble(Value->AsString(), Out);
			}
			else if (Value->Type == EJson::Number)
			{
				Out = Value->AsNumber();
			}
		}
		return Out;
	}

	bool SystemsExact(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& Out)
	{
		const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Field) : nullptr;
		if (!Value.IsValid() || (Value->Type != EJson::String && Value->Type != EJson::Number))
		{
			return false;
		}
		Out = SystemsValue(Value);
		return true;
	}

	bool SystemsIsPlant(const FLedgerComponent& Component)
	{
		return Component.Type == TEXT("PowerPlant");
	}

	/// Carries power through itself: has a power input and a power output and
	/// makes none of its own. Buses and couplings.
	bool SystemsPassesPower(const FLedgerComponent& Component)
	{
		if (SystemsIsPlant(Component))
		{
			return false;
		}
		bool bIn = false;
		bool bOut = false;
		for (const FLedgerPort& Port : Component.Ports)
		{
			bIn |= Port.Kind == ELedgerPortKind::Power && !Port.bOutput;
			bOut |= Port.Kind == ELedgerPortKind::Power && Port.bOutput;
		}
		return bIn && bOut;
	}
}

double FLedgerThermal::HeatShare(const FLedgerComponent& Component)
{
	return Component.Param(TEXT("heatShare"), SystemsIsPlant(Component) ? 0.5 : 0.3);
}

double FLedgerThermal::Throttle(double TemperatureK)
{
	if (TemperatureK <= ThrottleK)
	{
		return 1.0;
	}
	return FMath::Clamp(1.0 - 0.8 * (TemperatureK - ThrottleK) / (FailK - ThrottleK), 0.2, 1.0);
}

FVector3d FLedgerMassProperties::Respond(const FVector3d& Torque) const
{
	// Solve I a = t by Cramer: three by three, symmetric and positive.
	auto Det3 = [](const double (&M)[3][3])
	{
		return M[0][0] * (M[1][1] * M[2][2] - M[1][2] * M[2][1])
			- M[0][1] * (M[1][0] * M[2][2] - M[1][2] * M[2][0])
			+ M[0][2] * (M[1][0] * M[2][1] - M[1][1] * M[2][0]);
	};
	const double Det = Det3(Inertia);
	if (FMath::Abs(Det) < 1.0e-12)
	{
		return FVector3d::ZeroVector;
	}
	FVector3d Out;
	for (int32 Axis = 0; Axis < 3; ++Axis)
	{
		double M[3][3];
		for (int32 Row = 0; Row < 3; ++Row)
		{
			for (int32 Column = 0; Column < 3; ++Column)
			{
				M[Row][Column] = Column == Axis ? Torque[Row] : Inertia[Row][Column];
			}
		}
		Out[Axis] = Det3(M) / Det;
	}
	return Out;
}

FLedgerShipState FLedgerShipState::For(const FLedgerShipDefinition& Ship)
{
	FLedgerShipState State;
	State.Components.SetNum(Ship.Components.Num());
	State.Compartments.SetNum(Ship.Compartments.Num());
	State.SparesKg = Ship.Hull.SparesKg;
	for (const FLedgerComponent& Component : Ship.Components)
	{
		State.OxygenReserveKg += Component.Param(TEXT("reserveO2Kg"));
	}
	// Tanks start full; holds start empty.
	for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
	{
		if (Ship.Components[Index].Type == TEXT("FuelTank"))
		{
			State.Components[Index].ContentsKg = Ship.Components[Index].Param(TEXT("capacityKg"));
		}
		// Shields start lowered: raising one is a decision with a cost.
		if (Ship.Components[Index].Type == TEXT("Shield"))
		{
			State.Components[Index].bOn = false;
		}
	}
	return State;
}

namespace LedgerShipSystems
{
	bool IsFed(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Component)
	{
		const FLedgerComponent& Definition = Ship.Components[Component];
		for (int32 Port = 0; Port < Definition.Ports.Num(); ++Port)
		{
			if (!Definition.Ports[Port].bRequired || Definition.Ports[Port].bOutput
				|| Definition.Ports[Port].Kind == ELedgerPortKind::Power)
			{
				continue;
			}
			bool bFed = false;
			for (const TPair<int32, int32>& Source : Ship.ConnectedTo(Component, Port))
			{
				const bool bTank = Ship.Components[Source.Key].Type == TEXT("FuelTank");
				bFed |= State.Components[Source.Key].IsWorking() && (!bTank || State.Components[Source.Key].ContentsKg > 0.0);
			}
			if (!bFed)
			{
				return false;
			}
		}
		return true;
	}

	FLedgerPowerReport SolvePower(const FLedgerShipDefinition& Ship, FLedgerShipState& State)
	{
		FLedgerPowerReport Report;
		const int32 Count = Ship.Components.Num();
		for (FLedgerComponentState& Component : State.Components)
		{
			Component.bPowered = false;
			Component.PowerKw = 0.0;
		}

		// Networks: everything a working plant reaches along power connections,
		// through working buses and couplings. Two plants reaching the same bus
		// are one network, so each component is labelled with the first it is
		// found in and a later search that meets a label joins the two.
		TArray<int32> Network;
		Network.Init(INDEX_NONE, Count);
		TArray<int32> Parent;
		auto Root = [&Parent](int32 Label)
		{
			while (Parent[Label] != Label)
			{
				Label = Parent[Label];
			}
			return Label;
		};
		for (int32 Plant = 0; Plant < Count; ++Plant)
		{
			const FLedgerComponent& Definition = Ship.Components[Plant];
			if (!SystemsIsPlant(Definition) || !State.Components[Plant].IsWorking() || !IsFed(Ship, State, Plant))
			{
				continue;
			}
			const int32 Label = Parent.Num();
			Parent.Add(Label);
			TArray<int32> Open = { Plant };
			Network[Plant] = Label;
			while (Open.Num() > 0)
			{
				const int32 At = Open.Pop();
				const FLedgerComponent& Here = Ship.Components[At];
				for (int32 Port = 0; Port < Here.Ports.Num(); ++Port)
				{
					if (Here.Ports[Port].Kind != ELedgerPortKind::Power || !Here.Ports[Port].bOutput)
					{
						continue;
					}
					for (const TPair<int32, int32>& Next : Ship.ConnectedTo(At, Port))
					{
						if (Network[Next.Key] != INDEX_NONE)
						{
							Parent[Root(Network[Next.Key])] = Root(Label);
							continue;
						}
						Network[Next.Key] = Label;
						if (SystemsPassesPower(Ship.Components[Next.Key]) && State.Components[Next.Key].IsWorking())
						{
							Open.Add(Next.Key);
						}
					}
				}
			}
		}

		// Each network: supply, then consumers in priority order, shed from the
		// back until what is left fits.
		TMap<int32, TArray<int32>> Consumers;
		TMap<int32, double> Supply;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Network[Index] == INDEX_NONE)
			{
				continue;
			}
			const int32 Label = Root(Network[Index]);
			const FLedgerComponent& Definition = Ship.Components[Index];
			if (SystemsIsPlant(Definition))
			{
				const double Output = Definition.Param(TEXT("outputKw")) * State.Components[Index].Condition
					* FLedgerThermal::Throttle(State.Components[Index].TemperatureK);
				State.Components[Index].PowerKw = Output;
				State.Components[Index].bPowered = true;
				Supply.FindOrAdd(Label) += Output;
				Report.SupplyKw += Output;
			}
			else if (DrawKw(Ship, State, Index) > 0.0 && State.Components[Index].IsWorking())
			{
				Consumers.FindOrAdd(Label).Add(Index);
			}
			else if ((SystemsPassesPower(Definition) || Definition.Type == TEXT("Actuator")) && State.Components[Index].IsWorking())
			{
				// Buses and couplings carry it; an idle actuator has it waiting.
				State.Components[Index].bPowered = true;
			}
		}
		for (TPair<int32, TArray<int32>>& Pair : Consumers)
		{
			TArray<int32>& List = Pair.Value;
			List.StableSort([&Ship](int32 A, int32 B)
			{
				return Ship.Components[A].Param(TEXT("priority")) < Ship.Components[B].Param(TEXT("priority"));
			});
			double Demand = 0.0;
			for (const int32 Index : List)
			{
				Demand += DrawKw(Ship, State, Index);
			}
			Report.DemandKw += Demand;
			const double Available = Supply.FindRef(Pair.Key);
			int32 Kept = List.Num();
			while (Kept > 0 && Demand > Available + 1.0e-9)
			{
				--Kept;
				Demand -= DrawKw(Ship, State, List[Kept]);
				Report.Shed.Add(List[Kept]);
			}
			for (int32 Position = 0; Position < Kept; ++Position)
			{
				FLedgerComponentState& Component = State.Components[List[Position]];
				Component.bPowered = true;
				Component.PowerKw = DrawKw(Ship, State, List[Position]);
				Report.DeliveredKw += Component.PowerKw;
			}
		}

		// **Load following.** A plant makes what is drawn from it, not its rating,
		// so its heat and its fuel go with what the ship is doing -- which is what
		// makes running quiet quiet. Each plant carries its share of what its
		// network delivered.
		TMap<int32, double> Delivered;
		for (const TPair<int32, TArray<int32>>& Pair : Consumers)
		{
			for (const int32 Index : Pair.Value)
			{
				if (State.Components[Index].bPowered)
				{
					Delivered.FindOrAdd(Pair.Key) += State.Components[Index].PowerKw;
				}
			}
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Network[Index] != INDEX_NONE && SystemsIsPlant(Ship.Components[Index]))
			{
				const int32 Label = Root(Network[Index]);
				const double Made = Supply.FindRef(Label);
				State.Components[Index].PowerKw *= Made > 0.0 ? FMath::Min(Delivered.FindRef(Label) / Made, 1.0) : 0.0;
			}
		}

		// Consumers no working plant reaches at all are unpowered and asked for
		// nothing they could get; they count in the demand, not in the shedding.
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Network[Index] == INDEX_NONE && DrawKw(Ship, State, Index) > 0.0
				&& State.Components[Index].IsWorking())
			{
				Report.DemandKw += DrawKw(Ship, State, Index);
			}
		}
		Report.ThrustHeadroomKw = Report.SupplyKw;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			if (Ship.Components[Index].Type != TEXT("Thruster") && !SystemsIsPlant(Ship.Components[Index]) && State.Components[Index].bPowered)
			{
				Report.ThrustHeadroomKw -= State.Components[Index].PowerKw;
			}
		}
		return Report;
	}

	void StepThermal(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double DeltaSeconds)
	{
		if (DeltaSeconds <= 0.0)
		{
			return;
		}
		const int32 Count = Ship.Components.Num();

		// What each makes this step, kilowatts.
		TArray<double> Heat;
		Heat.Init(0.0, Count);
		for (int32 Index = 0; Index < Count; ++Index)
		{
			Heat[Index] = State.Components[Index].PowerKw * FLedgerThermal::HeatShare(Ship.Components[Index]);
		}

		// Each radiator and the components its coolant reaches: take what it
		// can, shared by what each put in.
		TArray<double> Rejected;
		Rejected.Init(0.0, Count);
		for (int32 Radiator = 0; Radiator < Count; ++Radiator)
		{
			if (Ship.Components[Radiator].Type != TEXT("Radiator") || !State.Components[Radiator].IsWorking())
			{
				continue;
			}
			const double Rating = Ship.Components[Radiator].Param(TEXT("rejectKw")) * State.Components[Radiator].Condition
				* (State.Components[Radiator].bDeployed ? 1.0 : FLedgerThermal::RetractedShare);
			// T131: a radiator with a pump moves its coolant only while the pump has
			// power. Without it the loop stands still and rejects next to nothing.
			const bool bPumped = Ship.Components[Radiator].Param(TEXT("drawKw")) <= 0.0 || State.Components[Radiator].bPowered;
			const double Moving = bPumped ? 1.0 : FLedgerThermal::RetractedShare;
			TArray<int32> Loop;
			double LoopHeat = 0.0;
			const FLedgerComponent& Definition = Ship.Components[Radiator];
			for (int32 Port = 0; Port < Definition.Ports.Num(); ++Port)
			{
				if (Definition.Ports[Port].Kind != ELedgerPortKind::Coolant)
				{
					continue;
				}
				for (const TPair<int32, int32>& Cooled : Ship.ConnectedTo(Radiator, Port))
				{
					Loop.AddUnique(Cooled.Key);
				}
			}
			for (const int32 Index : Loop)
			{
				LoopHeat += Heat[Index];
			}
			const double Taken = FMath::Min(LoopHeat, Rating * Moving);
			for (const int32 Index : Loop)
			{
				Rejected[Index] += LoopHeat > 0.0 ? Taken * Heat[Index] / LoopHeat : 0.0;
			}
		}

		State.RejectedKw = 0.0;
		for (int32 Index = 0; Index < Count; ++Index)
		{
			State.RejectedKw += Rejected[Index];
			State.Components[Index].HeatKw = Heat[Index];
		}
		for (int32 Index = 0; Index < Count; ++Index)
		{
			FLedgerComponentState& Component = State.Components[Index];
			const double CapacityKjPerK = FMath::Max(Ship.Components[Index].MassKg, 1.0) * FLedgerThermal::KjPerKgK;
			const double Passive = FLedgerThermal::PassiveKwPerK * (Component.TemperatureK - FLedgerThermal::AmbientK);
			Component.TemperatureK += (Heat[Index] - Rejected[Index] - Passive) / CapacityKjPerK * DeltaSeconds;
			if (Component.TemperatureK >= FLedgerThermal::FailK && Component.Condition > 0.0)
			{
				Component.Condition = 0.0;
			}
		}
	}

	double Burn(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		int32 Thruster, double ThrustNewtons, double DeltaSeconds)
	{
		if (ThrustNewtons <= 0.0 || DeltaSeconds <= 0.0)
		{
			return 1.0;
		}
		if (!Ship.Components.IsValidIndex(Thruster) || !State.Components[Thruster].IsWorking())
		{
			return 0.0;
		}
		const FLedgerComponent& Definition = Ship.Components[Thruster];
		constexpr double StandardGravity = 9.80665;
		const double Exhaust = FMath::Max(Definition.Param(TEXT("ispSeconds"), 3000.0), 1.0) * StandardGravity;
		const double Wanted = ThrustNewtons / Exhaust * DeltaSeconds;

		TArray<int32> Tanks;
		double Available = 0.0;
		for (int32 Port = 0; Port < Definition.Ports.Num(); ++Port)
		{
			if (Definition.Ports[Port].Kind != ELedgerPortKind::Fuel || Definition.Ports[Port].bOutput)
			{
				continue;
			}
			for (const TPair<int32, int32>& Source : Ship.ConnectedTo(Thruster, Port))
			{
				const FLedgerComponent& Tank = Ship.Components[Source.Key];
				if (Tank.Type == TEXT("FuelTank") && Tank.Param(TEXT("kind")) == 0.0
					&& State.Components[Source.Key].IsWorking() && State.Components[Source.Key].ContentsKg > 0.0
					&& !Tanks.Contains(Source.Key))
				{
					Tanks.Add(Source.Key);
					Available += State.Components[Source.Key].ContentsKg;
				}
			}
		}
		if (Available <= 0.0)
		{
			return 0.0;
		}
		const double Drawn = FMath::Min(Wanted, Available);
		for (const int32 Tank : Tanks)
		{
			State.Components[Tank].ContentsKg = FMath::Max(
				State.Components[Tank].ContentsKg - Drawn * State.Components[Tank].ContentsKg / Available, 0.0);
		}
		return Drawn / Wanted;
	}

	void FuelReactors(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double DeltaSeconds)
	{
		for (int32 Plant = 0; Plant < Ship.Components.Num(); ++Plant)
		{
			const FLedgerComponent& Definition = Ship.Components[Plant];
			const double Rating = Definition.Param(TEXT("outputKw"));
			if (!SystemsIsPlant(Definition) || Rating <= 0.0 || State.Components[Plant].PowerKw <= 0.0)
			{
				continue;
			}
			const double Wanted = Definition.Param(TEXT("fuelKgPerHour")) / 3600.0
				* State.Components[Plant].PowerKw / Rating * DeltaSeconds;
			TArray<int32> Tanks;
			double Available = 0.0;
			for (int32 Port = 0; Port < Definition.Ports.Num(); ++Port)
			{
				if (Definition.Ports[Port].Kind != ELedgerPortKind::Fuel || Definition.Ports[Port].bOutput)
				{
					continue;
				}
				for (const TPair<int32, int32>& Source : Ship.ConnectedTo(Plant, Port))
				{
					if (Ship.Components[Source.Key].Type == TEXT("FuelTank") && State.Components[Source.Key].IsWorking()
						&& State.Components[Source.Key].ContentsKg > 0.0 && !Tanks.Contains(Source.Key))
					{
						Tanks.Add(Source.Key);
						Available += State.Components[Source.Key].ContentsKg;
					}
				}
			}
			if (Available <= 0.0)
			{
				continue;
			}
			const double Drawn = FMath::Min(Wanted, Available);
			for (const int32 Tank : Tanks)
			{
				State.Components[Tank].ContentsKg = FMath::Max(
					State.Components[Tank].ContentsKg - Drawn * State.Components[Tank].ContentsKg / Available, 0.0);
			}
		}
	}

	double FuelKg(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Kind)
	{
		double Total = 0.0;
		for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
		{
			if (Ship.Components[Index].Type == TEXT("FuelTank") && Ship.Components[Index].Param(TEXT("kind")) == Kind)
			{
				Total += State.Components[Index].ContentsKg;
			}
		}
		return Total;
	}

	FLedgerMassProperties MassProperties(const FLedgerShipDefinition& Ship, const FLedgerShipState& State)
	{
		FLedgerMassProperties Out;
		// The hull: a solid box at the origin. Everything else: a point.
		struct FPiece { double Mass; FVector3d At; };
		TArray<FPiece> Pieces;
		Pieces.Add({ Ship.Hull.MassKg, FVector3d::ZeroVector });
		for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
		{
			const double Contents = State.Components.IsValidIndex(Index) ? State.Components[Index].ContentsKg : 0.0;
			Pieces.Add({ Ship.Components[Index].MassKg + Contents, Ship.Components[Index].PositionMetres });
		}
		for (const FPiece& Piece : Pieces)
		{
			Out.MassKg += Piece.Mass;
			Out.CentreMetres += Piece.At * Piece.Mass;
		}
		if (Out.MassKg <= 0.0)
		{
			return Out;
		}
		Out.CentreMetres /= Out.MassKg;

		// The box about its own centre, then every piece carried to the centre
		// of mass by the parallel-axis theorem.
		const double L = Ship.Hull.LengthMetres;
		const double W = Ship.Hull.WidthMetres;
		const double H = Ship.Hull.HeightMetres;
		Out.Inertia[0][0] = Ship.Hull.MassKg / 12.0 * (W * W + H * H);
		Out.Inertia[1][1] = Ship.Hull.MassKg / 12.0 * (L * L + H * H);
		Out.Inertia[2][2] = Ship.Hull.MassKg / 12.0 * (L * L + W * W);
		for (const FPiece& Piece : Pieces)
		{
			const FVector3d R = Piece.At - Out.CentreMetres;
			const double Rr = R.SizeSquared();
			const double C[3] = { R.X, R.Y, R.Z };
			for (int32 Row = 0; Row < 3; ++Row)
			{
				for (int32 Column = 0; Column < 3; ++Column)
				{
					Out.Inertia[Row][Column] += Piece.Mass * ((Row == Column ? Rr : 0.0) - C[Row] * C[Column]);
				}
			}
		}
		return Out;
	}

	TArray<int32> DownstreamOf(const FLedgerShipDefinition& Ship, int32 Component, bool bPowerOnly)
	{
		TArray<int32> Found;
		TArray<int32> Open = { Component };
		while (Open.Num() > 0)
		{
			const int32 At = Open.Pop();
			for (const FLedgerConnection& Connection : Ship.Connections)
			{
				if (Connection.FromComponent == At && Connection.ToComponent != Component
					&& (!bPowerOnly || Connection.Kind == ELedgerPortKind::Power)
					&& !Found.Contains(Connection.ToComponent))
				{
					Found.Add(Connection.ToComponent);
					Open.Add(Connection.ToComponent);
				}
			}
		}
		return Found;
	}

	void Damage(const FLedgerShipDefinition& Ship, FLedgerShipState& State, int32 Component, double Amount)
	{
		if (!State.Components.IsValidIndex(Component) || Amount <= 0.0)
		{
			return;
		}
		FLedgerComponentState& Hit = State.Components[Component];
		const double Left = Hit.Condition;
		Hit.Condition = FMath::Max(Left - Amount, 0.0);
		const double Spill = Amount - Left;
		if (Spill <= 0.0 || Left <= 0.0)
		{
			return;
		}
		TArray<int32> Neighbours;
		for (const FLedgerConnection& Connection : Ship.Connections)
		{
			if (Connection.FromComponent == Component)
			{
				Neighbours.AddUnique(Connection.ToComponent);
			}
			else if (Connection.ToComponent == Component)
			{
				Neighbours.AddUnique(Connection.FromComponent);
			}
		}
		for (const int32 Neighbour : Neighbours)
		{
			Damage(Ship, State, Neighbour, Spill * 0.25);
		}
	}

	double VentSeconds(double VolumeM3, double HoleM2, double TemperatureK)
	{
		// Air: gamma 1.4, R 287. Choked flow leaves at the local speed of sound
		// times (2 / (gamma + 1)) ^ ((gamma + 1) / (2 (gamma - 1))), which for
		// air is 0.5787, through an orifice that passes six tenths of its area.
		constexpr double Gamma = 1.4;
		constexpr double GasConstant = 287.05;
		constexpr double Choke = 0.5787;
		constexpr double Discharge = 0.6;
		const double Speed = FMath::Sqrt(Gamma * GasConstant * TemperatureK) * Choke * Discharge;
		return HoleM2 > 0.0 ? VolumeM3 / (HoleM2 * Speed) : TNumericLimits<double>::Max();
	}

	FLedgerHullHit HitHull(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		const FVector3d& PointMetres, double EnergyJoules)
	{
		FLedgerHullHit Hit;
		// ponytail: a hundred megajoules unmakes the hull, linearly; a hole a
		// square metre per hundred megajoules past the armour. Both want a
		// material model when weapons (M08) need them to differ.
		constexpr double HullJoules = 1.0e8;
		State.HullIntegrity = FMath::Max(State.HullIntegrity - EnergyJoules / HullJoules, 0.0);
		const double Past = EnergyJoules - Ship.Hull.ArmourJoules;
		if (Past <= 0.0)
		{
			return Hit;
		}
		Hit.bPenetrated = true;
		Hit.HoleM2 = Past / HullJoules;
		Hit.Compartment = Ship.CompartmentAt(PointMetres);
		if (State.Compartments.IsValidIndex(Hit.Compartment))
		{
			State.Compartments[Hit.Compartment].BreachM2 += Hit.HoleM2;
		}
		return Hit;
	}

	void StepAtmosphere(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		double OutsidePa, double DeltaSeconds)
	{
		for (int32 Index = 0; Index < State.Compartments.Num() && Index < Ship.Compartments.Num(); ++Index)
		{
			FLedgerCompartmentState& Compartment = State.Compartments[Index];
			if (Compartment.BreachM2 <= 0.0 || Compartment.PressurePa <= OutsidePa)
			{
				continue;
			}
			// Exact across the step for the exponential it is while choked, so
			// the step size cannot change the answer; unchoked near the end it
			// is held from undershooting the outside pressure.
			const double Tau = VentSeconds(Ship.Compartments[Index].VolumeM3(), Compartment.BreachM2, Compartment.TemperatureK);
			Compartment.PressurePa = FMath::Max(Compartment.PressurePa * FMath::Exp(-DeltaSeconds / Tau), OutsidePa);
		}
	}

	void Wear(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double DeltaSeconds)
	{
		for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
		{
			FLedgerComponentState& Component = State.Components[Index];
			const FLedgerComponent& Definition = Ship.Components[Index];
			const double Rating = SystemsIsPlant(Definition) ? Definition.Param(TEXT("outputKw")) : Definition.Param(TEXT("drawKw"));
			if (!Component.IsWorking() || Component.PowerKw <= 0.0 || Rating <= 0.0)
			{
				continue;
			}
			const double Hours = DeltaSeconds / 3600.0;
			const double Load = FMath::Clamp(Component.PowerKw / Rating, 0.0, 2.0);
			// Abuse: every hundred kelvin over 350 quadruples the wear.
			const double Heat = 1.0 + 4.0 * FMath::Max(0.0, (Component.TemperatureK - 350.0) / 100.0);
			Component.OperatingHours += Hours;
			Component.Condition = FMath::Max(Component.Condition
				- Definition.Param(TEXT("wearPerHour"), 0.002) * Load * Heat * Hours, 0.0);
		}
	}

	double ThrustShare(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Thruster)
	{
		if (!State.Components.IsValidIndex(Thruster) || !State.Components[Thruster].IsWorking()
			|| !State.Components[Thruster].bPowered)
		{
			return 0.0;
		}
		return State.Components[Thruster].Condition;
	}

	FLedgerRepair Repair(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		int32 Component, bool bWorkshop, double* WorkshopPartsKg)
	{
		FLedgerRepair Out;
		if (!State.Components.IsValidIndex(Component))
		{
			Out.Why = TEXT("no such component");
			return Out;
		}
		FLedgerComponentState& Target = State.Components[Component];
		const double Mass = FMath::Max(Ship.Components[Component].MassKg, 1.0);
		Out.ConditionBefore = Out.ConditionAfter = Target.Condition;
		if (!bWorkshop && Target.Condition <= 0.0)
		{
			Out.Why = TEXT("destroyed: it needs replacing, and that is a workshop's job");
			return Out;
		}
		const double Ceiling = bWorkshop ? 1.0 : FieldCeiling;
		if (Target.Condition >= Ceiling)
		{
			Out.Why = TEXT("already as good as this repair can make it");
			return Out;
		}
		double& Stock = bWorkshop ? *WorkshopPartsKg : State.SparesKg;
		if (bWorkshop && WorkshopPartsKg == nullptr)
		{
			Out.Why = TEXT("a workshop repair needs a workshop");
			return Out;
		}
		// Replacing costs half the mass; mending a twentieth per whole condition.
		const bool bReplace = Target.Condition <= 0.0;
		const double Wanted = bReplace ? Mass * 0.5 : Mass * 0.05 * (Ceiling - Target.Condition);
		const double Used = FMath::Min(Wanted, Stock);
		if (Used <= 0.0)
		{
			Out.Why = TEXT("no parts");
			return Out;
		}
		if (bReplace)
		{
			if (Used < Wanted)
			{
				Out.Why = TEXT("not enough parts for a replacement");
				return Out;
			}
			Target.Condition = 1.0;
		}
		else
		{
			Target.Condition += (Ceiling - Target.Condition) * Used / Wanted;
		}
		Stock -= Used;
		Out.bDone = true;
		Out.PartsKg = Used;
		Out.ConditionAfter = Target.Condition;
		return Out;
	}

	ELedgerAirWarning StepLifeSupport(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		double OutsidePa, double DeltaSeconds)
	{
		ELedgerAirWarning Warnings = ELedgerAirWarning::None;
		if (State.Compartments.Num() == 0 || DeltaSeconds <= 0.0)
		{
			return Warnings;
		}
		constexpr double GasConstant = 8.314462618;
		constexpr double OxygenKgPerMole = 0.032;
		constexpr double DayPerSecond = 1.0 / 86400.0;
		constexpr double NominalPa = 101325.0;
		constexpr double NominalOxygenPa = 21200.0;

		// Is anything keeping the air? A life support that is on, whole and
		// powered, with scrubbing capacity for the crew.
		double Capacity = 0.0;
		for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
		{
			if (Ship.Components[Index].Type == TEXT("LifeSupport") && State.Components[Index].IsWorking()
				&& State.Components[Index].bPowered)
			{
				Capacity += Ship.Components[Index].Param(TEXT("crewCapacity"), 2.0) * State.Components[Index].Condition;
			}
		}

		for (int32 Index = 0; Index < State.Compartments.Num(); ++Index)
		{
			FLedgerCompartmentState& Air = State.Compartments[Index];
			const double Volume = Ship.Compartments[Index].VolumeM3();
			const double PaPerMole = GasConstant * Air.TemperatureK / FMath::Max(Volume, 0.01);

			// The seals: every gas leaks towards the outside, in proportion.
			if (Air.PressurePa > OutsidePa)
			{
				const double Keep = FMath::Exp(-DeltaSeconds / (LeakHours * 3600.0));
				const double Before = Air.PressurePa;
				Air.PressurePa = OutsidePa + (Air.PressurePa - OutsidePa) * Keep;
				const double Share = Air.PressurePa / Before;
				Air.OxygenPa *= Share;
				Air.CarbonDioxidePa *= Share;
			}

			// The crew, in the first compartment: oxygen in, the same moles of
			// carbon dioxide out.
			if (Index == 0 && Ship.Hull.Crew > 0)
			{
				const double Moles = Ship.Hull.Crew * OxygenKgPerPersonDay * DayPerSecond / OxygenKgPerMole * DeltaSeconds;
				const double Taken = FMath::Min(Moles * PaPerMole, Air.OxygenPa);
				Air.OxygenPa -= Taken;
				Air.CarbonDioxidePa += Taken;
			}

			// Life support: scrub, then restore oxygen and pressure from the
			// reserve, for as many people as it has capacity for.
			if (Capacity > 0.0)
			{
				const double Coverage = FMath::Min(Capacity / FMath::Max(Ship.Hull.Crew, 1), 1.0);
				Air.CarbonDioxidePa -= (Air.CarbonDioxidePa - 40.0) * Coverage * FMath::Min(DeltaSeconds / 60.0, 1.0);
				const double MissingOxygenPa = FMath::Max(NominalOxygenPa - Air.OxygenPa, 0.0) * Coverage;
				const double MissingKg = MissingOxygenPa / PaPerMole * OxygenKgPerMole;
				const double FromReserve = FMath::Min(MissingKg, State.OxygenReserveKg);
				State.OxygenReserveKg -= FromReserve;
				const double Restored = FromReserve / OxygenKgPerMole * PaPerMole;
				Air.OxygenPa += Restored;
				Air.PressurePa = FMath::Min(Air.PressurePa + Restored, NominalPa);
			}

			if (Air.PressurePa < LowPressurePa)
			{
				Warnings |= ELedgerAirWarning::LowPressure;
			}
			if (Air.OxygenPa < LowOxygenPa)
			{
				Warnings |= ELedgerAirWarning::LowOxygen;
			}
			if (Air.CarbonDioxidePa > HighCarbonDioxidePa)
			{
				Warnings |= ELedgerAirWarning::HighCarbonDioxide;
			}
		}
		return Warnings;
	}

	void StepShields(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double DeltaSeconds)
	{
		for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
		{
			if (Ship.Components[Index].Type != TEXT("Shield"))
			{
				continue;
			}
			FLedgerComponentState& Shield = State.Components[Index];
			const double PerSector = Ship.Components[Index].Param(TEXT("capacityKj")) * Shield.Condition / 4.0;
			for (double& Sector : Shield.SectorKj)
			{
				if (Shield.IsWorking() && Shield.bPowered)
				{
					Sector = FMath::Min(Sector + Shield.PowerKw * DeltaSeconds / 4.0, PerSector);
				}
				else
				{
					Sector *= FMath::Exp(-DeltaSeconds / 3.0);
				}
			}
		}
	}

	double AbsorbHit(const FLedgerShipDefinition& Ship, FLedgerShipState& State,
		const FVector3d& FromDirection, double EnergyJoules)
	{
		const int32 Sector = FMath::Abs(FromDirection.X) >= FMath::Abs(FromDirection.Y)
			? (FromDirection.X >= 0.0 ? 0 : 1)
			: (FromDirection.Y < 0.0 ? 2 : 3);
		double LeftKj = EnergyJoules / 1000.0;
		for (int32 Index = 0; Index < Ship.Components.Num() && LeftKj > 0.0; ++Index)
		{
			if (Ship.Components[Index].Type != TEXT("Shield"))
			{
				continue;
			}
			double& Stored = State.Components[Index].SectorKj[Sector];
			const double Taken = FMath::Min(Stored, LeftKj);
			Stored -= Taken;
			LeftKj -= Taken;
		}
		return LeftKj * 1000.0;
	}

	FLedgerSignature Signature(const FLedgerShipDefinition& Ship, const FLedgerShipState& State)
	{
		FLedgerSignature Out;
		Out.ThermalKw = State.RejectedKw;
		Out.CrossSectionM2 = Ship.Hull.WidthMetres * Ship.Hull.HeightMetres;
		for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
		{
			const FLedgerComponent& Definition = Ship.Components[Index];
			const FLedgerComponentState& Component = State.Components[Index];
			Out.ThermalKw += FLedgerThermal::PassiveKwPerK * FMath::Max(Component.TemperatureK - FLedgerThermal::AmbientK, 0.0);
			// Emission: what a powered shield, drive or sensor radiates, as a
			// share of what it draws.
			const double Share = Definition.Param(TEXT("emissionShare"),
				Definition.Type == TEXT("Shield") ? 1.0
				: Definition.Type == TEXT("Thruster") ? 0.5
				: Definition.Type == TEXT("Computer") ? 0.2 : 0.02);
			if (Component.bPowered && !SystemsIsPlant(Definition))
			{
				Out.EmissionKw += Component.PowerKw * Share;
			}
			if (Definition.Type == TEXT("Radiator") && Component.bDeployed)
			{
				Out.CrossSectionM2 += Definition.Param(TEXT("areaM2"));
			}
		}
		// ponytail: range as the square root of what is radiated (inverse
		// square) and the fourth root of cross-section (the radar equation),
		// scaled to a hundred kilometres at a hundred kilowatts and at ten
		// square metres. A sensor model (M08) replaces the scales, not the laws.
		Out.ThermalRangeKm = 100.0 * FMath::Sqrt(Out.ThermalKw / 100.0);
		Out.EmissionRangeKm = 150.0 * FMath::Sqrt(Out.EmissionKw / 100.0);
		Out.RadarRangeKm = 50.0 * FMath::Pow(Out.CrossSectionM2 / 10.0, 0.25);
		return Out;
	}

	FLedgerShipState Cold(const FLedgerShipDefinition& Ship)
	{
		FLedgerShipState State = FLedgerShipState::For(Ship);
		for (FLedgerComponentState& Component : State.Components)
		{
			Component.bOn = false;
		}
		return State;
	}

	bool CanStart(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Component, FString* Reason)
	{
		if (!State.Components.IsValidIndex(Component) || State.Components[Component].Condition <= 0.0)
		{
			if (Reason != nullptr)
			{
				*Reason = TEXT("it is destroyed");
			}
			return false;
		}
		// Power and data are only known by solving with it switched on.
		FLedgerShipState Trial = State;
		Trial.Components[Component].bOn = true;
		SolvePower(Ship, Trial);
		const FLedgerComponent& Definition = Ship.Components[Component];
		for (int32 Port = 0; Port < Definition.Ports.Num(); ++Port)
		{
			const FLedgerPort& Input = Definition.Ports[Port];
			if (!Input.bRequired || Input.bOutput)
			{
				continue;
			}
			bool bLive = false;
			if (Input.Kind == ELedgerPortKind::Power)
			{
				bLive = Trial.Components[Component].bPowered;
			}
			else
			{
				for (const TPair<int32, int32>& Source : Ship.ConnectedTo(Component, Port))
				{
					const FLedgerComponentState& From = Trial.Components[Source.Key];
					const bool bTank = Ship.Components[Source.Key].Type == TEXT("FuelTank");
					bLive |= From.IsWorking() && (!bTank || From.ContentsKg > 0.0)
						&& (Input.Kind != ELedgerPortKind::Data || From.bPowered);
				}
			}
			if (!bLive)
			{
				if (Reason != nullptr)
				{
					*Reason = FString::Printf(TEXT("%s: its %s input '%s' is not live"),
						*Definition.Id, LexToString(Input.Kind), *Input.Name);
				}
				return false;
			}
		}
		return true;
	}

	bool TryStart(const FLedgerShipDefinition& Ship, FLedgerShipState& State, int32 Component, FString* Reason)
	{
		if (!CanStart(Ship, State, Component, Reason))
		{
			return false;
		}
		State.Components[Component].bOn = true;
		SolvePower(Ship, State);
		return true;
	}

	TArray<int32> StartupOrder(const FLedgerShipDefinition& Ship)
	{
		FLedgerShipState State = Cold(Ship);
		TArray<int32> Order;
		bool bProgress = true;
		while (bProgress)
		{
			bProgress = false;
			TArray<int32> Ready;
			for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
			{
				if (!State.Components[Index].bOn && CanStart(Ship, State, Index))
				{
					Ready.Add(Index);
				}
			}
			for (const int32 Index : Ready)
			{
				State.Components[Index].bOn = true;
				Order.Add(Index);
				bProgress = true;
			}
			SolvePower(Ship, State);
		}
		return Order;
	}

	double DrawKw(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Component)
	{
		const FLedgerComponent& Definition = Ship.Components[Component];
		if (Definition.Type == TEXT("Actuator"))
		{
			const FLedgerComponentState& Actuator = State.Components[Component];
			return FMath::IsNearlyEqual(Actuator.Deployment, Actuator.DeployTarget) ? 0.0 : Definition.Param(TEXT("drawKw"));
		}
		return Definition.Param(TEXT("drawKw"));
	}

	double HoldVolumeUsedM3(const FLedgerShipState& State, int32 Hold)
	{
		double Used = 0.0;
		if (State.Components.IsValidIndex(Hold))
		{
			for (const FLedgerCargoItem& Item : State.Components[Hold].Cargo)
			{
				Used += Item.VolumeM3;
			}
		}
		return Used;
	}

	bool Stow(const FLedgerShipDefinition& Ship, FLedgerShipState& State, int32 Hold,
		const FLedgerCargoItem& Item, FString* Why)
	{
		if (!Ship.Components.IsValidIndex(Hold) || Ship.Components[Hold].Type != TEXT("CargoHold"))
		{
			if (Why != nullptr)
			{
				*Why = TEXT("that is not a hold");
			}
			return false;
		}
		const double Volume = Ship.Components[Hold].Param(TEXT("volumeM3"));
		const double MaxKg = Ship.Components[Hold].Param(TEXT("maxKg"));
		FLedgerComponentState& Contents = State.Components[Hold];
		const bool bVolume = HoldVolumeUsedM3(State, Hold) + Item.VolumeM3 > Volume + 1.0e-9;
		const bool bMass = Contents.ContentsKg + Item.MassKg > MaxKg + 1.0e-9;
		if (bVolume || bMass)
		{
			if (Why != nullptr)
			{
				*Why = bVolume && bMass ? TEXT("no room and too heavy") : (bVolume ? TEXT("no room") : TEXT("too heavy"));
			}
			return false;
		}
		Contents.Cargo.Add(Item);
		Contents.ContentsKg += Item.MassKg;
		return true;
	}

	void StepActuators(const FLedgerShipDefinition& Ship, FLedgerShipState& State, double DeltaSeconds)
	{
		for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
		{
			if (Ship.Components[Index].Type != TEXT("Actuator"))
			{
				continue;
			}
			FLedgerComponentState& Actuator = State.Components[Index];
			if (!Actuator.IsWorking() || !Actuator.bPowered)
			{
				continue;
			}
			const double Rate = 1.0 / FMath::Max(Ship.Components[Index].Param(TEXT("travelSeconds"), 4.0), 0.01);
			const double Step = Rate * DeltaSeconds;
			const double Gap = Actuator.DeployTarget - Actuator.Deployment;
			Actuator.Deployment += FMath::Clamp(Gap, -Step, Step);
		}
	}

	FLedgerGaugeReading ReadGauge(const FLedgerShipDefinition& Ship, const FLedgerShipState& State, int32 Instrument)
	{
		FLedgerGaugeReading Out;
		if (!Ship.Instruments.IsValidIndex(Instrument))
		{
			Out.Why = TEXT("no such gauge");
			return Out;
		}
		const FLedgerInstrument& Gauge = Ship.Instruments[Instrument];
		const int32 Sensor = Ship.FindComponent(Gauge.Sensor);
		if (Sensor != INDEX_NONE)
		{
			const FLedgerComponentState& Measuring = State.Components[Sensor];
			if (!Measuring.IsWorking() || !Measuring.bPowered)
			{
				Out.Why = FString::Printf(TEXT("its sensor %s is %s"), *Gauge.Sensor,
					Measuring.Condition <= 0.0 ? TEXT("destroyed") : (!Measuring.bOn ? TEXT("off") : TEXT("unpowered")));
				return Out;
			}
		}
		const int32 Component = Ship.FindComponent(Gauge.Component);
		const int32 Compartment = Ship.Compartments.IndexOfByPredicate([&Gauge](const FLedgerCompartment& C) { return C.Id == Gauge.Compartment; });
		Out.bAvailable = true;
		if (Gauge.Reading == TEXT("hullIntegrity"))
		{
			Out.Value = State.HullIntegrity;
		}
		else if (Compartment != INDEX_NONE && Gauge.Reading == TEXT("pressurePa"))
		{
			Out.Value = State.Compartments[Compartment].PressurePa;
		}
		else if (Compartment != INDEX_NONE && Gauge.Reading == TEXT("oxygenPa"))
		{
			Out.Value = State.Compartments[Compartment].OxygenPa;
		}
		else if (Component != INDEX_NONE)
		{
			const FLedgerComponentState& Read = State.Components[Component];
			if (Gauge.Reading == TEXT("temperatureK")) { Out.Value = Read.TemperatureK; }
			else if (Gauge.Reading == TEXT("contentsKg")) { Out.Value = Read.ContentsKg; }
			else if (Gauge.Reading == TEXT("condition")) { Out.Value = Read.Condition; }
			else if (Gauge.Reading == TEXT("powerKw")) { Out.Value = Read.PowerKw; }
			else if (Gauge.Reading == TEXT("deployment")) { Out.Value = Read.Deployment; }
			else
			{
				Out.bAvailable = false;
				Out.Why = FString::Printf(TEXT("%s has no reading called %s"), *Gauge.Component, *Gauge.Reading);
			}
		}
		else
		{
			Out.bAvailable = false;
			Out.Why = TEXT("it reads nothing");
		}
		return Out;
	}

	FString SaveState(const FLedgerShipDefinition& Ship, const FLedgerShipState& State)
	{
		auto Num = [](double Value) { return FString::Printf(TEXT("\"%.17g\""), Value); };
		TArray<FString> Components;
		for (int32 Index = 0; Index < State.Components.Num(); ++Index)
		{
			const FLedgerComponentState& C = State.Components[Index];
			TArray<FString> Cargo;
			for (const FLedgerCargoItem& Item : C.Cargo)
			{
				Cargo.Add(FString::Printf(TEXT("{ \"name\": \"%s\", \"massKg\": %s, \"volumeM3\": %s }"),
					*Item.Name.ReplaceCharWithEscapedChar(), *Num(Item.MassKg), *Num(Item.VolumeM3)));
			}
			Components.Add(FString::Printf(
				TEXT("{ \"id\": \"%s\", \"on\": %s, \"deployed\": %s, \"condition\": %s, \"temperatureK\": %s, \"contentsKg\": %s, \"hours\": %s, \"deployment\": %s, \"target\": %s, \"sectors\": [%s, %s, %s, %s], \"cargo\": [ %s ] }"),
				Ship.Components.IsValidIndex(Index) ? *Ship.Components[Index].Id : TEXT(""),
				C.bOn ? TEXT("true") : TEXT("false"), C.bDeployed ? TEXT("true") : TEXT("false"),
				*Num(C.Condition), *Num(C.TemperatureK), *Num(C.ContentsKg), *Num(C.OperatingHours),
				*Num(C.Deployment), *Num(C.DeployTarget),
				*Num(C.SectorKj[0]), *Num(C.SectorKj[1]), *Num(C.SectorKj[2]), *Num(C.SectorKj[3]),
				*FString::Join(Cargo, TEXT(", "))));
		}
		TArray<FString> Compartments;
		for (const FLedgerCompartmentState& C : State.Compartments)
		{
			Compartments.Add(FString::Printf(TEXT("{ \"pressurePa\": %s, \"temperatureK\": %s, \"breachM2\": %s, \"oxygenPa\": %s, \"carbonDioxidePa\": %s }"),
				*Num(C.PressurePa), *Num(C.TemperatureK), *Num(C.BreachM2), *Num(C.OxygenPa), *Num(C.CarbonDioxidePa)));
		}
		return FString::Printf(TEXT("{\n\"ship\": \"%s\",\n\"hullIntegrity\": %s,\n\"sparesKg\": %s,\n\"oxygenReserveKg\": %s,\n\"components\": [ %s ],\n\"compartments\": [ %s ]\n}"),
			*Ship.Name, *Num(State.HullIntegrity), *Num(State.SparesKg), *Num(State.OxygenReserveKg),
			*FString::Join(Components, TEXT(",\n")), *FString::Join(Compartments, TEXT(",\n")));
	}

	bool LoadState(const FLedgerShipDefinition& Ship, const FString& Json, FLedgerShipState& Out, TArray<FString>& OutErrors)
	{
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutErrors.Add(TEXT("not a JSON object"));
			return false;
		}
		FLedgerShipState State = FLedgerShipState::For(Ship);
		SystemsExact(Root, TEXT("hullIntegrity"), State.HullIntegrity);
		SystemsExact(Root, TEXT("sparesKg"), State.SparesKg);
		SystemsExact(Root, TEXT("oxygenReserveKg"), State.OxygenReserveKg);
		const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
		if (!Root->TryGetArrayField(TEXT("components"), Components) || Components->Num() != Ship.Components.Num())
		{
			OutErrors.Add(TEXT("the saved components do not match this ship's"));
			return false;
		}
		for (int32 Index = 0; Index < Components->Num(); ++Index)
		{
			const TSharedPtr<FJsonObject> Object = (*Components)[Index]->AsObject();
			FString Id;
			if (!Object.IsValid() || !Object->TryGetStringField(TEXT("id"), Id) || Id != Ship.Components[Index].Id)
			{
				OutErrors.Add(FString::Printf(TEXT("component %d is not %s"), Index, *Ship.Components[Index].Id));
				return false;
			}
			FLedgerComponentState& C = State.Components[Index];
			Object->TryGetBoolField(TEXT("on"), C.bOn);
			Object->TryGetBoolField(TEXT("deployed"), C.bDeployed);
			SystemsExact(Object, TEXT("condition"), C.Condition);
			SystemsExact(Object, TEXT("temperatureK"), C.TemperatureK);
			SystemsExact(Object, TEXT("contentsKg"), C.ContentsKg);
			SystemsExact(Object, TEXT("hours"), C.OperatingHours);
			SystemsExact(Object, TEXT("deployment"), C.Deployment);
			SystemsExact(Object, TEXT("target"), C.DeployTarget);
			const TArray<TSharedPtr<FJsonValue>>* Sectors = nullptr;
			if (Object->TryGetArrayField(TEXT("sectors"), Sectors) && Sectors->Num() == 4)
			{
				for (int32 Sector = 0; Sector < 4; ++Sector)
				{
					C.SectorKj[Sector] = SystemsValue((*Sectors)[Sector]);
				}
			}
			const TArray<TSharedPtr<FJsonValue>>* Cargo = nullptr;
			C.Cargo.Reset();
			if (Object->TryGetArrayField(TEXT("cargo"), Cargo))
			{
				for (const TSharedPtr<FJsonValue>& ItemValue : *Cargo)
				{
					const TSharedPtr<FJsonObject> Item = ItemValue->AsObject();
					FLedgerCargoItem Stowed;
					Item->TryGetStringField(TEXT("name"), Stowed.Name);
					SystemsExact(Item, TEXT("massKg"), Stowed.MassKg);
					SystemsExact(Item, TEXT("volumeM3"), Stowed.VolumeM3);
					C.Cargo.Add(Stowed);
				}
			}
		}
		const TArray<TSharedPtr<FJsonValue>>* Compartments = nullptr;
		if (Root->TryGetArrayField(TEXT("compartments"), Compartments) && Compartments->Num() == State.Compartments.Num())
		{
			for (int32 Index = 0; Index < Compartments->Num(); ++Index)
			{
				const TSharedPtr<FJsonObject> Object = (*Compartments)[Index]->AsObject();
				FLedgerCompartmentState& C = State.Compartments[Index];
				SystemsExact(Object, TEXT("pressurePa"), C.PressurePa);
				SystemsExact(Object, TEXT("temperatureK"), C.TemperatureK);
				SystemsExact(Object, TEXT("breachM2"), C.BreachM2);
				SystemsExact(Object, TEXT("oxygenPa"), C.OxygenPa);
				SystemsExact(Object, TEXT("carbonDioxidePa"), C.CarbonDioxidePa);
			}
		}
		// What is derived, not stored, comes back from the models.
		SolvePower(Ship, State);
		Out = MoveTemp(State);
		return true;
	}

	void AlarmVoice(ELedgerAlarm Kind, double& OutToneHz, double& OutOnSeconds, double& OutOffSeconds, int32& OutUrgency)
	{
		// Pitch says which system, rhythm how urgent: the fast high ones are the
		// ones that kill you soonest.
		struct FVoice { double Hz; double On; double Off; int32 Urgency; };
		static const FVoice Voices[] =
		{
			{ 1200.0, 0.10, 0.10, 1 },  // ReactorOverheat: rapid and high
			{  600.0, 0.50, 0.50, 4 },  // PowerShed: slow even pulse
			{  440.0, 0.20, 1.00, 6 },  // FuelLow: a single chirp a second
			{  900.0, 0.80, 0.20, 2 },  // HullBreach: long blasts
			{  700.0, 0.15, 0.30, 0 },  // LowOxygen: fast mid
			{  520.0, 0.30, 0.30, 3 },  // HighCarbonDioxide: steady mid
			{  300.0, 1.00, 0.25, 0 },  // StructuralFailure: low and long
			{ 1000.0, 0.05, 0.45, 5 },  // ComponentDestroyed: a tick
			{  350.0, 0.10, 2.00, 7 },  // ServiceDue: a rare low blip
		};
		const FVoice& Voice = Voices[static_cast<int32>(Kind)];
		OutToneHz = Voice.Hz;
		OutOnSeconds = Voice.On;
		OutOffSeconds = Voice.Off;
		OutUrgency = Voice.Urgency;
	}

	TArray<FLedgerAlarm> Alarms(const FLedgerShipDefinition& Ship, const FLedgerShipState& State,
		const FLedgerPowerReport& Power, ELedgerAirWarning Air)
	{
		TArray<FLedgerAlarm> Out;
		auto Raise = [&Out](ELedgerAlarm Kind, int32 Component, const FString& Says)
		{
			FLedgerAlarm Alarm;
			Alarm.Kind = Kind;
			Alarm.Component = Component;
			Alarm.Says = Says;
			AlarmVoice(Kind, Alarm.ToneHz, Alarm.OnSeconds, Alarm.OffSeconds, Alarm.Urgency);
			Out.Add(Alarm);
		};
		for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
		{
			const FLedgerComponent& Definition = Ship.Components[Index];
			const FLedgerComponentState& Component = State.Components[Index];
			if (Component.Condition <= 0.0)
			{
				Raise(ELedgerAlarm::ComponentDestroyed, Index, FString::Printf(TEXT("%s destroyed"), *Definition.Id));
				continue;
			}
			if (SystemsIsPlant(Definition) && Component.TemperatureK > FLedgerThermal::ThrottleK)
			{
				Raise(ELedgerAlarm::ReactorOverheat, Index, FString::Printf(TEXT("%s at %.0f K"), *Definition.Id, Component.TemperatureK));
			}
			if (Definition.Type == TEXT("FuelTank") && Component.ContentsKg < 0.1 * Definition.Param(TEXT("capacityKg")))
			{
				Raise(ELedgerAlarm::FuelLow, Index, FString::Printf(TEXT("%s at %.0f kg"), *Definition.Id, Component.ContentsKg));
			}
			if (Component.NeedsService())
			{
				Raise(ELedgerAlarm::ServiceDue, Index, FString::Printf(TEXT("%s at %.0f%%"), *Definition.Id, Component.Condition * 100.0));
			}
		}
		if (Power.Shed.Num() > 0)
		{
			Raise(ELedgerAlarm::PowerShed, Power.Shed[0], FString::Printf(TEXT("%d loads shed"), Power.Shed.Num()));
		}
		for (int32 Index = 0; Index < State.Compartments.Num(); ++Index)
		{
			if (State.Compartments[Index].BreachM2 > 0.0 && Ship.Compartments.IsValidIndex(Index))
			{
				Raise(ELedgerAlarm::HullBreach, INDEX_NONE, FString::Printf(TEXT("%s breached"), *Ship.Compartments[Index].Id));
			}
		}
		if (EnumHasAnyFlags(Air, ELedgerAirWarning::LowOxygen))
		{
			Raise(ELedgerAlarm::LowOxygen, INDEX_NONE, TEXT("oxygen low"));
		}
		if (EnumHasAnyFlags(Air, ELedgerAirWarning::HighCarbonDioxide))
		{
			Raise(ELedgerAlarm::HighCarbonDioxide, INDEX_NONE, TEXT("carbon dioxide high"));
		}
		if (State.HullIntegrity < FLedgerShipState::StructuralFailure)
		{
			Raise(ELedgerAlarm::StructuralFailure, INDEX_NONE, FString::Printf(TEXT("hull at %.0f%%"), State.HullIntegrity * 100.0));
		}
		Out.StableSort([](const FLedgerAlarm& A, const FLedgerAlarm& B) { return A.Urgency < B.Urgency; });
		return Out;
	}
}
