#include "LedgerShipDefinition.h"

#include "Dom/JsonObject.h"
#include "Misc/DefaultValueHelper.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "LedgerShipSystems.h"

namespace
{
	/// A number, read exactly. The engine's JSON reader does not promise the
	/// last bit of a double (LedgerBody.cpp has the story), so a saved ship
	/// writes its numbers as "%.17g" strings and they come back through
	/// FDefaultValueHelper, which round-trips them; a number written by hand
	/// as a plain JSON number is read as one.
	double ShipValue(const TSharedPtr<FJsonValue>& Value)
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

	bool ShipExact(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& Out)
	{
		const TSharedPtr<FJsonValue> Value = Object.IsValid() ? Object->TryGetField(Field) : nullptr;
		if (!Value.IsValid() || (Value->Type != EJson::String && Value->Type != EJson::Number))
		{
			return false;
		}
		Out = ShipValue(Value);
		return true;
	}

	bool ShipKindFromString(const FString& Text, ELedgerPortKind& Out)
	{
		for (const ELedgerPortKind Kind : { ELedgerPortKind::Power, ELedgerPortKind::Coolant,
			ELedgerPortKind::Fuel, ELedgerPortKind::Data })
		{
			if (Text.Equals(LexToString(Kind), ESearchCase::IgnoreCase))
			{
				Out = Kind;
				return true;
			}
		}
		return false;
	}

	void ShipNumber(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field, double& Out)
	{
		ShipExact(Object, Field, Out);
	}

	FVector3d ShipVector(const TSharedPtr<FJsonObject>& Object, const TCHAR* Field)
	{
		const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
		if (Object.IsValid() && Object->TryGetArrayField(Field, Values) && Values->Num() >= 3)
		{
			return FVector3d(ShipValue((*Values)[0]), ShipValue((*Values)[1]), ShipValue((*Values)[2]));
		}
		return FVector3d::ZeroVector;
	}
}

const TCHAR* LexToString(ELedgerPortKind Kind)
{
	switch (Kind)
	{
	case ELedgerPortKind::Power:   return TEXT("power");
	case ELedgerPortKind::Coolant: return TEXT("coolant");
	case ELedgerPortKind::Fuel:    return TEXT("fuel");
	default:                       return TEXT("data");
	}
}

int32 FLedgerComponent::FindPort(const FString& Name) const
{
	return Ports.IndexOfByPredicate([&Name](const FLedgerPort& Port) { return Port.Name == Name; });
}

double FLedgerComponent::Param(const FString& Key, double Default) const
{
	const double* Value = Params.Find(Key);
	return Value != nullptr ? *Value : Default;
}

int32 FLedgerShipDefinition::FindComponent(const FString& Id) const
{
	return Components.IndexOfByPredicate([&Id](const FLedgerComponent& Component) { return Component.Id == Id; });
}

TArray<TPair<int32, int32>> FLedgerShipDefinition::ConnectedTo(int32 Component, int32 Port) const
{
	TArray<TPair<int32, int32>> Out;
	for (const FLedgerConnection& Connection : Connections)
	{
		if (Connection.FromComponent == Component && Connection.FromPort == Port)
		{
			Out.Add({ Connection.ToComponent, Connection.ToPort });
		}
		else if (Connection.ToComponent == Component && Connection.ToPort == Port)
		{
			Out.Add({ Connection.FromComponent, Connection.FromPort });
		}
	}
	return Out;
}

int32 FLedgerShipDefinition::CompartmentAt(const FVector3d& PointMetres) const
{
	return Compartments.IndexOfByPredicate([&PointMetres](const FLedgerCompartment& Compartment)
	{
		return Compartment.Contains(PointMetres);
	});
}

double FLedgerShipDefinition::MassKg() const
{
	double Total = Hull.MassKg;
	for (const FLedgerComponent& Component : Components)
	{
		Total += Component.MassKg;
	}
	return Total;
}

namespace LedgerShips
{
	FString DefaultDirectory()
	{
		return FPaths::ConvertRelativePathToFull(FPaths::ProjectConfigDir() / TEXT("Ships"));
	}

	bool Parse(const FString& Json, FLedgerShipDefinition& Out, TArray<FString>& OutErrors)
	{
		const int32 ErrorsBefore = OutErrors.Num();
		TSharedPtr<FJsonObject> Root;
		const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(Json);
		if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
		{
			OutErrors.Add(TEXT("not a JSON object"));
			return false;
		}

		FLedgerShipDefinition Ship;
		if (!Root->TryGetStringField(TEXT("name"), Ship.Name) || Ship.Name.IsEmpty())
		{
			OutErrors.Add(TEXT("'name' is missing or empty"));
		}

		const TSharedPtr<FJsonObject>* Hull = nullptr;
		if (Root->TryGetObjectField(TEXT("hull"), Hull))
		{
			(*Hull)->TryGetStringField(TEXT("kit"), Ship.Hull.Kit);
			ShipNumber(*Hull, TEXT("lengthMetres"), Ship.Hull.LengthMetres);
			ShipNumber(*Hull, TEXT("widthMetres"), Ship.Hull.WidthMetres);
			ShipNumber(*Hull, TEXT("heightMetres"), Ship.Hull.HeightMetres);
			ShipNumber(*Hull, TEXT("massKg"), Ship.Hull.MassKg);
			ShipNumber(*Hull, TEXT("armourJoules"), Ship.Hull.ArmourJoules);
			ShipNumber(*Hull, TEXT("sparesKg"), Ship.Hull.SparesKg);
			double Crew = Ship.Hull.Crew;
			ShipNumber(*Hull, TEXT("crew"), Crew);
			Ship.Hull.Crew = FMath::Max(static_cast<int32>(Crew), 0);
		}
		const TSharedPtr<FJsonObject>* Flight = nullptr;
		if (Root->TryGetObjectField(TEXT("flight"), Flight))
		{
			ShipNumber(*Flight, TEXT("mainThrust"), Ship.Flight.MainThrust);
			ShipNumber(*Flight, TEXT("manoeuvringThrust"), Ship.Flight.ManoeuvringThrust);
			ShipNumber(*Flight, TEXT("pitchRate"), Ship.Flight.PitchRate);
			ShipNumber(*Flight, TEXT("yawRate"), Ship.Flight.YawRate);
			ShipNumber(*Flight, TEXT("rollRate"), Ship.Flight.RollRate);
			ShipNumber(*Flight, TEXT("drag"), Ship.Flight.AtmosphericDrag);
			ShipNumber(*Flight, TEXT("ballistic"), Ship.Flight.BallisticKgPerM2);
		}
		if (Ship.Hull.LengthMetres <= 0.0 || Ship.Hull.MassKg <= 0.0)
		{
			OutErrors.Add(TEXT("the hull needs a positive length and mass"));
		}

		// ---- components and their ports
		const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
		if (!Root->TryGetArrayField(TEXT("components"), Components) || Components->Num() == 0)
		{
			OutErrors.Add(TEXT("'components' is missing or empty"));
		}
		else
		{
			for (const TSharedPtr<FJsonValue>& Value : *Components)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				FLedgerComponent Component;
				if (!Object.IsValid() || !Object->TryGetStringField(TEXT("id"), Component.Id) || Component.Id.IsEmpty())
				{
					OutErrors.Add(TEXT("a component has no id"));
					continue;
				}
				if (Ship.FindComponent(Component.Id) != INDEX_NONE)
				{
					OutErrors.Add(FString::Printf(TEXT("two components are called '%s'"), *Component.Id));
					continue;
				}
				Object->TryGetStringField(TEXT("type"), Component.Type);
				ShipNumber(Object, TEXT("massKg"), Component.MassKg);
				Component.PositionMetres = ShipVector(Object, TEXT("position"));
				const TSharedPtr<FJsonObject>* Params = nullptr;
				if (Object->TryGetObjectField(TEXT("params"), Params))
				{
					for (const TPair<FString, TSharedPtr<FJsonValue>>& Param : (*Params)->Values)
					{
						Component.Params.Add(Param.Key, ShipValue(Param.Value));
					}
				}
				const TArray<TSharedPtr<FJsonValue>>* Ports = nullptr;
				if (Object->TryGetArrayField(TEXT("ports"), Ports))
				{
					for (const TSharedPtr<FJsonValue>& PortValue : *Ports)
					{
						const TSharedPtr<FJsonObject> PortObject = PortValue->AsObject();
						FLedgerPort Port;
						FString Kind;
						FString Direction;
						if (!PortObject.IsValid() || !PortObject->TryGetStringField(TEXT("name"), Port.Name)
							|| !PortObject->TryGetStringField(TEXT("kind"), Kind)
							|| !PortObject->TryGetStringField(TEXT("dir"), Direction))
						{
							OutErrors.Add(FString::Printf(TEXT("%s: a port needs a name, a kind and a dir"), *Component.Id));
							continue;
						}
						if (!ShipKindFromString(Kind, Port.Kind))
						{
							OutErrors.Add(FString::Printf(TEXT("%s.%s: '%s' is not a port kind (power, coolant, fuel, data)"),
								*Component.Id, *Port.Name, *Kind));
							continue;
						}
						if (Direction != TEXT("in") && Direction != TEXT("out"))
						{
							OutErrors.Add(FString::Printf(TEXT("%s.%s: dir must be 'in' or 'out', not '%s'"),
								*Component.Id, *Port.Name, *Direction));
							continue;
						}
						Port.bOutput = Direction == TEXT("out");
						PortObject->TryGetBoolField(TEXT("required"), Port.bRequired);
						Component.Ports.Add(Port);
					}
				}
				Ship.Components.Add(Component);
			}
		}

		// ---- connections: "component.port" to "component.port", output to input
		const TArray<TSharedPtr<FJsonValue>>* Connections = nullptr;
		if (Root->TryGetArrayField(TEXT("connections"), Connections))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Connections)
			{
				const TArray<TSharedPtr<FJsonValue>>* Pair = nullptr;
				if (!Value->TryGetArray(Pair) || Pair->Num() != 2)
				{
					OutErrors.Add(TEXT("a connection is not a pair of \"component.port\" strings"));
					continue;
				}
				const FString From = (*Pair)[0]->AsString();
				const FString To = (*Pair)[1]->AsString();
				auto Resolve = [&Ship, &OutErrors](const FString& Text, int32& OutComponent, int32& OutPort)
				{
					FString ComponentId;
					FString PortName;
					if (!Text.Split(TEXT("."), &ComponentId, &PortName))
					{
						OutErrors.Add(FString::Printf(TEXT("'%s' is not \"component.port\""), *Text));
						return false;
					}
					OutComponent = Ship.FindComponent(ComponentId);
					if (OutComponent == INDEX_NONE)
					{
						OutErrors.Add(FString::Printf(TEXT("'%s': there is no component '%s'"), *Text, *ComponentId));
						return false;
					}
					OutPort = Ship.Components[OutComponent].FindPort(PortName);
					if (OutPort == INDEX_NONE)
					{
						OutErrors.Add(FString::Printf(TEXT("'%s': %s has no port '%s'"), *Text, *ComponentId, *PortName));
						return false;
					}
					return true;
				};
				FLedgerConnection Connection;
				if (!Resolve(From, Connection.FromComponent, Connection.FromPort)
					| !Resolve(To, Connection.ToComponent, Connection.ToPort))
				{
					continue;
				}
				const FLedgerPort& Out = Ship.Components[Connection.FromComponent].Ports[Connection.FromPort];
				const FLedgerPort& In = Ship.Components[Connection.ToComponent].Ports[Connection.ToPort];
				if (!Out.bOutput || In.bOutput)
				{
					OutErrors.Add(FString::Printf(TEXT("%s -> %s: a connection runs from an output to an input"), *From, *To));
					continue;
				}
				if (Out.Kind != In.Kind)
				{
					OutErrors.Add(FString::Printf(TEXT("%s -> %s: %s cannot feed %s"), *From, *To,
						LexToString(Out.Kind), LexToString(In.Kind)));
					continue;
				}
				if (Connection.FromComponent == Connection.ToComponent)
				{
					OutErrors.Add(FString::Printf(TEXT("%s -> %s: a component cannot feed itself"), *From, *To));
					continue;
				}
				Connection.Kind = Out.Kind;
				Ship.Connections.Add(Connection);
			}
		}

		// ---- compartments: optional, and each a box with some room in it
		const TArray<TSharedPtr<FJsonValue>>* Compartments = nullptr;
		if (Root->TryGetArrayField(TEXT("compartments"), Compartments))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Compartments)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				FLedgerCompartment Compartment;
				if (!Object.IsValid() || !Object->TryGetStringField(TEXT("id"), Compartment.Id))
				{
					OutErrors.Add(TEXT("a compartment has no id"));
					continue;
				}
				Compartment.MinMetres = ShipVector(Object, TEXT("min"));
				Compartment.MaxMetres = ShipVector(Object, TEXT("max"));
				if (Compartment.VolumeM3() <= 0.0)
				{
					OutErrors.Add(FString::Printf(TEXT("compartment %s: max must be beyond min on every axis"), *Compartment.Id));
					continue;
				}
				Ship.Compartments.Add(Compartment);
			}
		}

		// ---- hardpoints: optional
		const TArray<TSharedPtr<FJsonValue>>* Hardpoints = nullptr;
		if (Root->TryGetArrayField(TEXT("hardpoints"), Hardpoints))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Hardpoints)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				FLedgerHardpoint Hardpoint;
				double SizeClass = 1.0;
				if (!Object.IsValid() || !Object->TryGetStringField(TEXT("id"), Hardpoint.Id))
				{
					OutErrors.Add(TEXT("a hardpoint has no id"));
					continue;
				}
				ShipNumber(Object, TEXT("class"), SizeClass);
				Hardpoint.SizeClass = static_cast<int32>(SizeClass);
				Hardpoint.PositionMetres = ShipVector(Object, TEXT("position"));
			Object->TryGetStringField(TEXT("occupant"), Hardpoint.Occupant);
				Ship.Hardpoints.Add(Hardpoint);
			}
		}

		// ---- nozzles: optional; what the allocator steers. T133.
		const TArray<TSharedPtr<FJsonValue>>* Nozzles = nullptr;
		if (Root->TryGetArrayField(TEXT("nozzles"), Nozzles))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Nozzles)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				FString Of;
				if (!Object.IsValid() || !Object->TryGetStringField(TEXT("component"), Of))
				{
					OutErrors.Add(TEXT("a nozzle names no component"));
					continue;
				}
				FLedgerNozzle Nozzle;
				Nozzle.Component = Ship.FindComponent(Of);
				Nozzle.PositionMetres = ShipVector(Object, TEXT("position"));
				Nozzle.Push = ShipVector(Object, TEXT("push"));
				ShipNumber(Object, TEXT("thrustN"), Nozzle.ThrustNewtons);
				if (Nozzle.Component == INDEX_NONE || Ship.Components[Nozzle.Component].Type != TEXT("Thruster"))
				{
					OutErrors.Add(FString::Printf(TEXT("a nozzle is on %s, which is not a thruster"), *Of));
					continue;
				}
				if (Nozzle.Push.IsNearlyZero() || Nozzle.ThrustNewtons <= 0.0)
				{
					OutErrors.Add(FString::Printf(TEXT("a nozzle on %s pushes no way, or with nothing"), *Of));
					continue;
				}
				Nozzle.Push.Normalize();
				Ship.Nozzles.Add(Nozzle);
			}
		}

		// ---- instruments: optional
		const TArray<TSharedPtr<FJsonValue>>* Instruments = nullptr;
		if (Root->TryGetArrayField(TEXT("instruments"), Instruments))
		{
			for (const TSharedPtr<FJsonValue>& Value : *Instruments)
			{
				const TSharedPtr<FJsonObject> Object = Value->AsObject();
				FLedgerInstrument Instrument;
				if (!Object.IsValid() || !Object->TryGetStringField(TEXT("id"), Instrument.Id)
					|| !Object->TryGetStringField(TEXT("reading"), Instrument.Reading))
				{
					OutErrors.Add(TEXT("an instrument needs an id and a reading"));
					continue;
				}
				Object->TryGetStringField(TEXT("component"), Instrument.Component);
				Object->TryGetStringField(TEXT("compartment"), Instrument.Compartment);
				Object->TryGetStringField(TEXT("sensor"), Instrument.Sensor);
				if ((!Instrument.Component.IsEmpty() && Ship.FindComponent(Instrument.Component) == INDEX_NONE)
					|| (!Instrument.Sensor.IsEmpty() && Ship.FindComponent(Instrument.Sensor) == INDEX_NONE))
				{
					OutErrors.Add(FString::Printf(TEXT("instrument %s: names a component that is not there"), *Instrument.Id));
					continue;
				}
				Ship.Instruments.Add(Instrument);
			}
		}

		// ---- every required input fed
		for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
		{
			const FLedgerComponent& Component = Ship.Components[Index];
			for (int32 PortIndex = 0; PortIndex < Component.Ports.Num(); ++PortIndex)
			{
				const FLedgerPort& Port = Component.Ports[PortIndex];
				if (Port.bRequired && !Port.bOutput && Ship.ConnectedTo(Index, PortIndex).Num() == 0)
				{
					OutErrors.Add(FString::Printf(TEXT("%s.%s: a required %s input is not connected"),
						*Component.Id, *Port.Name, LexToString(Port.Kind)));
				}
			}
		}

		if (OutErrors.Num() > ErrorsBefore)
		{
			return false;
		}
		Out = MoveTemp(Ship);
		return true;
	}

	bool Load(const FString& Directory, const FString& Name, FLedgerShipDefinition& Out, TArray<FString>& OutErrors)
	{
		const FString Path = Directory / (Name + TEXT(".json"));
		FString Json;
		if (!FFileHelper::LoadFileToString(Json, *Path))
		{
			OutErrors.Add(FString::Printf(TEXT("%s: unreadable"), *Path));
			return false;
		}
		TArray<FString> Errors;
		const bool bParsed = Parse(Json, Out, Errors);
		for (const FString& Error : Errors)
		{
			OutErrors.Add(FString::Printf(TEXT("%s: %s"), *FPaths::GetCleanFilename(Path), *Error));
		}
		return bParsed;
	}

	bool Mount(FLedgerShipDefinition& Ship, FLedgerComponent Item, const FString& HardpointId, FString& OutError)
	{
		FLedgerHardpoint* Hardpoint = Ship.Hardpoints.FindByPredicate([&HardpointId](const FLedgerHardpoint& H) { return H.Id == HardpointId; });
		if (Hardpoint == nullptr)
		{
			OutError = FString::Printf(TEXT("there is no hardpoint '%s'"), *HardpointId);
			return false;
		}
		if (!Hardpoint->Occupant.IsEmpty())
		{
			OutError = FString::Printf(TEXT("%s already carries %s"), *HardpointId, *Hardpoint->Occupant);
			return false;
		}
		const int32 ItemClass = static_cast<int32>(Item.Param(TEXT("mountClass")));
		if (ItemClass != Hardpoint->SizeClass)
		{
			OutError = FString::Printf(TEXT("%s is class %d and %s takes class %d"), *Item.Id, ItemClass, *HardpointId, Hardpoint->SizeClass);
			return false;
		}
		if (Ship.FindComponent(Item.Id) != INDEX_NONE)
		{
			OutError = FString::Printf(TEXT("there is already a component called %s"), *Item.Id);
			return false;
		}

		// Find a source for every required input before touching anything.
		auto SourceFor = [&Ship](ELedgerPortKind Kind, int32& OutComponent, int32& OutPort)
		{
			const TCHAR* Type = Kind == ELedgerPortKind::Power ? TEXT("Bus")
				: Kind == ELedgerPortKind::Coolant ? TEXT("Radiator")
				: Kind == ELedgerPortKind::Data ? TEXT("Computer") : TEXT("FuelTank");
			for (int32 Index = 0; Index < Ship.Components.Num(); ++Index)
			{
				if (Ship.Components[Index].Type != Type)
				{
					continue;
				}
				for (int32 Port = 0; Port < Ship.Components[Index].Ports.Num(); ++Port)
				{
					if (Ship.Components[Index].Ports[Port].bOutput && Ship.Components[Index].Ports[Port].Kind == Kind)
					{
						OutComponent = Index;
						OutPort = Port;
						return true;
					}
				}
			}
			return false;
		};
		TArray<FLedgerConnection> Joins;
		const int32 NewIndex = Ship.Components.Num();
		for (int32 Port = 0; Port < Item.Ports.Num(); ++Port)
		{
			const FLedgerPort& Input = Item.Ports[Port];
			if (Input.bOutput || !Input.bRequired)
			{
				continue;
			}
			FLedgerConnection Join;
			if (!SourceFor(Input.Kind, Join.FromComponent, Join.FromPort))
			{
				OutError = FString::Printf(TEXT("%s needs %s and the ship has nothing to give it"), *Item.Id, LexToString(Input.Kind));
				return false;
			}
			Join.ToComponent = NewIndex;
			Join.ToPort = Port;
			Join.Kind = Input.Kind;
			Joins.Add(Join);
		}
		Item.PositionMetres = Hardpoint->PositionMetres;
		Hardpoint->Occupant = Item.Id;
		Ship.Components.Add(MoveTemp(Item));
		Ship.Connections.Append(Joins);
		return true;
	}

	FString ToJson(const FLedgerShipDefinition& Ship)
	{
		auto Num = [](double Value) { return FString::Printf(TEXT("\"%.17g\""), Value); };
		auto Vec = [&Num](const FVector3d& V) { return FString::Printf(TEXT("[%s, %s, %s]"), *Num(V.X), *Num(V.Y), *Num(V.Z)); };
		auto Str = [](const FString& Text) { return FString::Printf(TEXT("\"%s\""), *Text.ReplaceCharWithEscapedChar()); };
		TArray<FString> Out;
		Out.Add(TEXT("{"));
		Out.Add(FString::Printf(TEXT("\"name\": %s,"), *Str(Ship.Name)));
		Out.Add(FString::Printf(TEXT("\"hull\": { \"kit\": %s, \"lengthMetres\": %s, \"widthMetres\": %s, \"heightMetres\": %s, \"massKg\": %s, \"armourJoules\": %s, \"sparesKg\": %s, \"crew\": %d },"),
			*Str(Ship.Hull.Kit), *Num(Ship.Hull.LengthMetres), *Num(Ship.Hull.WidthMetres), *Num(Ship.Hull.HeightMetres),
			*Num(Ship.Hull.MassKg), *Num(Ship.Hull.ArmourJoules), *Num(Ship.Hull.SparesKg), Ship.Hull.Crew));
		const FLedgerShipFlight& F = Ship.Flight;
		Out.Add(FString::Printf(TEXT("\"flight\": { \"mainThrust\": %s, \"manoeuvringThrust\": %s, \"pitchRate\": %s, \"yawRate\": %s, \"rollRate\": %s, \"drag\": %s, \"ballistic\": %s },"),
			*Num(F.MainThrust), *Num(F.ManoeuvringThrust), *Num(F.PitchRate), *Num(F.YawRate), *Num(F.RollRate), *Num(F.AtmosphericDrag), *Num(F.BallisticKgPerM2)));
		TArray<FString> Components;
		for (const FLedgerComponent& C : Ship.Components)
		{
			TArray<FString> Params;
			for (const TPair<FString, double>& Param : C.Params)
			{
				Params.Add(FString::Printf(TEXT("%s: %s"), *Str(Param.Key), *Num(Param.Value)));
			}
			TArray<FString> Ports;
			for (const FLedgerPort& Port : C.Ports)
			{
				Ports.Add(FString::Printf(TEXT("{ \"name\": %s, \"kind\": \"%s\", \"dir\": \"%s\", \"required\": %s }"),
					*Str(Port.Name), LexToString(Port.Kind), Port.bOutput ? TEXT("out") : TEXT("in"), Port.bRequired ? TEXT("true") : TEXT("false")));
			}
			Components.Add(FString::Printf(TEXT("{ \"id\": %s, \"type\": %s, \"massKg\": %s, \"position\": %s, \"params\": { %s }, \"ports\": [ %s ] }"),
				*Str(C.Id), *Str(C.Type), *Num(C.MassKg), *Vec(C.PositionMetres), *FString::Join(Params, TEXT(", ")), *FString::Join(Ports, TEXT(", "))));
		}
		Out.Add(FString::Printf(TEXT("\"components\": [ %s ],"), *FString::Join(Components, TEXT(",\n"))));
		TArray<FString> Connections;
		for (const FLedgerConnection& Link : Ship.Connections)
		{
			Connections.Add(FString::Printf(TEXT("[\"%s.%s\", \"%s.%s\"]"),
				*Ship.Components[Link.FromComponent].Id, *Ship.Components[Link.FromComponent].Ports[Link.FromPort].Name,
				*Ship.Components[Link.ToComponent].Id, *Ship.Components[Link.ToComponent].Ports[Link.ToPort].Name));
		}
		Out.Add(FString::Printf(TEXT("\"connections\": [ %s ],"), *FString::Join(Connections, TEXT(", "))));
		TArray<FString> Compartments;
		for (const FLedgerCompartment& C : Ship.Compartments)
		{
			Compartments.Add(FString::Printf(TEXT("{ \"id\": %s, \"min\": %s, \"max\": %s }"), *Str(C.Id), *Vec(C.MinMetres), *Vec(C.MaxMetres)));
		}
		Out.Add(FString::Printf(TEXT("\"compartments\": [ %s ],"), *FString::Join(Compartments, TEXT(", "))));
		TArray<FString> Hardpoints;
		for (const FLedgerHardpoint& H : Ship.Hardpoints)
		{
			Hardpoints.Add(FString::Printf(TEXT("{ \"id\": %s, \"class\": %d, \"position\": %s, \"occupant\": %s }"),
				*Str(H.Id), H.SizeClass, *Vec(H.PositionMetres), *Str(H.Occupant)));
		}
		Out.Add(FString::Printf(TEXT("\"hardpoints\": [ %s ],"), *FString::Join(Hardpoints, TEXT(", "))));
		TArray<FString> Instruments;
		for (const FLedgerInstrument& I : Ship.Instruments)
		{
			Instruments.Add(FString::Printf(TEXT("{ \"id\": %s, \"component\": %s, \"reading\": %s, \"compartment\": %s, \"sensor\": %s }"),
				*Str(I.Id), *Str(I.Component), *Str(I.Reading), *Str(I.Compartment), *Str(I.Sensor)));
		}
		Out.Add(FString::Printf(TEXT("\"instruments\": [ %s ],"), *FString::Join(Instruments, TEXT(", "))));
		TArray<FString> Nozzles;
		for (const FLedgerNozzle& N : Ship.Nozzles)
		{
			Nozzles.Add(FString::Printf(TEXT("{ \"component\": %s, \"position\": %s, \"push\": %s, \"thrustN\": %s }"),
				*Str(Ship.Components[N.Component].Id), *Vec(N.PositionMetres), *Vec(N.Push), *Num(N.ThrustNewtons)));
		}
		Out.Add(FString::Printf(TEXT("\"nozzles\": [ %s ]"), *FString::Join(Nozzles, TEXT(", "))));
		Out.Add(TEXT("}"));
		return FString::Join(Out, TEXT("\n"));
	}

	FLedgerOutfitPreview PreviewSwap(const FLedgerShipDefinition& Ship, const FString& ComponentId,
		const FLedgerComponent& Replacement)
	{
		FLedgerOutfitPreview Preview;
		const int32 Slot = Ship.FindComponent(ComponentId);
		if (Slot == INDEX_NONE)
		{
			Preview.Why = FString::Printf(TEXT("there is no %s to replace"), *ComponentId);
			return Preview;
		}
		const FLedgerComponent& Old = Ship.Components[Slot];
		if (Replacement.Type != Old.Type)
		{
			Preview.Why = FString::Printf(TEXT("a %s cannot go in a %s slot"), *Replacement.Type, *Old.Type);
			return Preview;
		}
		const double SlotSize = Old.Param(TEXT("slotSize"), Old.Param(TEXT("slotClass"), 1.0));
		if (Replacement.Param(TEXT("slotClass"), 1.0) > SlotSize)
		{
			Preview.Why = FString::Printf(TEXT("class %.0f is too big for a size %.0f slot"), Replacement.Param(TEXT("slotClass"), 1.0), SlotSize);
			return Preview;
		}
		// Every port the old one was joined through must be on the new one, the
		// same kind and the same way round.
		for (const FLedgerConnection& Link : Ship.Connections)
		{
			if (Link.FromComponent != Slot && Link.ToComponent != Slot)
			{
				continue;
			}
			const FLedgerPort& Used = Old.Ports[Link.FromComponent == Slot ? Link.FromPort : Link.ToPort];
			const int32 Match = Replacement.FindPort(Used.Name);
			if (Match == INDEX_NONE || Replacement.Ports[Match].Kind != Used.Kind || Replacement.Ports[Match].bOutput != Used.bOutput)
			{
				Preview.Why = FString::Printf(TEXT("%s has no %s port to take the %s connection"), *Replacement.Id, *Used.Name, LexToString(Used.Kind));
				return Preview;
			}
		}

		Preview.After = Ship;
		FLedgerComponent Fitted = Replacement;
		Fitted.Id = Old.Id;
		Fitted.PositionMetres = Old.PositionMetres;
		Fitted.Params.Add(TEXT("slotSize"), SlotSize);
		// Reconnect by port name: the indices move with the new port list.
		for (FLedgerConnection& Link : Preview.After.Connections)
		{
			if (Link.FromComponent == Slot)
			{
				Link.FromPort = Fitted.FindPort(Old.Ports[Link.FromPort].Name);
			}
			if (Link.ToComponent == Slot)
			{
				Link.ToPort = Fitted.FindPort(Old.Ports[Link.ToPort].Name);
			}
		}
		Preview.After.Components[Slot] = Fitted;
		Preview.bFits = true;

		// The three things the screen shows, from the models, full power on both.
		FLedgerShipState Before = FLedgerShipState::For(Ship);
		FLedgerShipState After = FLedgerShipState::For(Preview.After);
		const FLedgerPowerReport PowerBefore = LedgerShipSystems::SolvePower(Ship, Before);
		const FLedgerPowerReport PowerAfter = LedgerShipSystems::SolvePower(Preview.After, After);
		LedgerShipSystems::StepThermal(Ship, Before, 1.0);
		LedgerShipSystems::StepThermal(Preview.After, After, 1.0);
		double HeatBefore = 0.0;
		double HeatAfter = 0.0;
		for (const FLedgerComponentState& C : Before.Components) { HeatBefore += C.HeatKw; }
		for (const FLedgerComponentState& C : After.Components) { HeatAfter += C.HeatKw; }
		// Heat at full rated output: what the plant would shed flat out.
		HeatBefore += Old.Param(TEXT("outputKw")) * FLedgerThermal::HeatShare(Old) - Before.Components[Slot].HeatKw;
		HeatAfter += Fitted.Param(TEXT("outputKw")) * FLedgerThermal::HeatShare(Fitted) - After.Components[Slot].HeatKw;
		const FLedgerMassProperties MassBefore = LedgerShipSystems::MassProperties(Ship, FLedgerShipState::For(Ship));
		const FLedgerMassProperties MassAfter = LedgerShipSystems::MassProperties(Preview.After, FLedgerShipState::For(Preview.After));
		Preview.MassDeltaKg = MassAfter.MassKg - MassBefore.MassKg;
		Preview.HeatDeltaKw = HeatAfter - HeatBefore;
		Preview.PowerMarginDeltaKw = (PowerAfter.SupplyKw - PowerAfter.DemandKw) - (PowerBefore.SupplyKw - PowerBefore.DemandKw);
		// The file's main thrust is an acceleration at the ship's own mass; the
		// same engine pushes a heavier ship less.
		Preview.AccelerationBefore = Ship.Flight.MainThrust;
		Preview.AccelerationAfter = Ship.Flight.MainThrust * MassBefore.MassKg / MassAfter.MassKg;
		Preview.RollInertiaBefore = MassBefore.Inertia[0][0];
		Preview.RollInertiaAfter = MassAfter.Inertia[0][0];
		return Preview;
	}
}
