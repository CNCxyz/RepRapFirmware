/*
 * Trigger.cpp
 *
 *  Created on: 6 Jun 2019
 *      Author: David
 */

#include "Trigger.h"
#include "RepRap.h"
#include "GCodes.h"
#include "PrintMonitor.h"
#include "GCodeBuffer/GCodeBuffer.h"

Trigger::Trigger() noexcept : condition(0)
{
}

// Initialise the trigger
void Trigger::Init() noexcept
{
	highLevelEndstops.Clear();
	lowLevelEndstops.Clear();
	highLevelInputs.Clear();
	lowLevelInputs.Clear();
	condition = 0;
}

// Return true if this trigger is unused, i.e. it doesn't watch any pins
bool Trigger::IsUnused() const noexcept
{
	return highLevelEndstops.IsEmpty() && lowLevelEndstops.IsEmpty() && highLevelInputs.IsEmpty() && lowLevelInputs.IsEmpty();
}

// Check whether this trigger is active and update the input states
bool Trigger::Check() noexcept
{
	bool triggered = false;

	// Check the endstops
	EndstopsManager& endstops = reprap.GetPlatform().GetEndstops();
	(highLevelEndstops | lowLevelEndstops)
		.Iterate([this, &endstops, &triggered](unsigned int axis, bool)
					{
						const bool stopped = (endstops.Stopped(axis) == EndStopHit::atStop);
						if (stopped != endstopStates.IsBitSet(axis))
						{
							endstopStates.SetOrClearBit(axis, stopped);
							if (stopped == highLevelEndstops.IsBitSet(axis))
							{
								triggered = true;
							}
						}
					}
				);

	Platform& platform = reprap.GetPlatform();
	(highLevelInputs | lowLevelInputs)
		.Iterate([this, &platform, &triggered](unsigned int inPort, bool)
					{
						const bool isActive = reprap.GetPlatform().GetGpInPort(inPort).GetState();
						if (isActive != inputStates.IsBitSet(inPort))
						{
							inputStates.SetOrClearBit(inPort, isActive);
							if (isActive == highLevelInputs.IsBitSet(inPort))
							{
								triggered = true;
							}
						}
					}
				);

	return triggered && (condition == 0 || (condition > 0 && reprap.GetPrintMonitor().IsPrinting()));
}

// Handle M581 for this trigger
GCodeResult Trigger::Configure(unsigned int number, GCodeBuffer &gb, const StringRef &reply)
{
	bool seen = false;
	if (gb.Seen('C'))
	{
		seen = true;
		condition = gb.GetIValue();
	}
	else if (IsUnused())
	{
		condition = 0;					// this is a new trigger, so set no condition
	}

	const int sParam = (gb.Seen('S')) ? gb.GetIValue() : 1;
	if (gb.Seen('P'))
	{
		if (gb.GetIValue() == -1)
		{
			Init();						// P-1 clears all inputs and sets condition to 0
			return GCodeResult::ok;
		}
		else
		{
			seen = true;
			gb.Seen('P');
			uint32_t inputNumbers[MaxGpInPorts];
			size_t numValues = MaxGpInPorts;
			gb.GetUnsignedArray(inputNumbers, numValues, false);
			const InputPortsBitmap portsToWaitFor = InputPortsBitmap::MakeFromArray(inputNumbers, numValues);
			((sParam >= 1) ? highLevelInputs : lowLevelInputs) |= portsToWaitFor;
		}
	}
	AxesBitmap endstopsToWaitFor;
	for (size_t axis = 0; axis < reprap.GetGCodes().GetTotalAxes(); ++axis)
	{
		if (gb.Seen(reprap.GetGCodes().GetAxisLetters()[axis]))
		{
			seen = true;
			endstopsToWaitFor.SetBit(axis);
		}
	}

	((sParam >= 1) ?highLevelEndstops : lowLevelEndstops) |= endstopsToWaitFor;


	inputStates.Clear();
	(void)Check();					// set up initial input states

	if (!seen)
	{
		reply.printf("Trigger %u ", number);
		if (IsUnused())
		{
			reply.cat("is not configured");
		}
		else
		{
			if (condition < 0)
			{
				reply.cat("if enabled would fire on a");
			}
			else if (condition > 0)
			{
				reply.cat("fires only when printing on a");
			}
			else
			{
				reply.cat("fires on a");
			}
			const bool hasHighLevel = !highLevelEndstops.IsEmpty() || !highLevelInputs.IsEmpty();
			if (hasHighLevel)
			{
				reply.cat(" rising edge of endstops/inputs");
				AppendInputNames(highLevelEndstops, highLevelInputs, reply);
			}
			const bool hasLowLevel = !lowLevelEndstops.IsEmpty() || !lowLevelInputs.IsEmpty();
			if (hasLowLevel)
			{
				if (hasHighLevel)
				{
					reply.cat(" or a");
				}
				reply.cat(" falling edge of endstops/inputs");
				AppendInputNames(lowLevelEndstops, lowLevelInputs, reply);
			}
		}
	}
	return GCodeResult::ok;
}

// Handle M582 for this trigger
bool Trigger::CheckLevel() noexcept
{
	endstopStates = lowLevelEndstops;
	inputStates = lowLevelInputs;
	return Check();
}

void Trigger::AppendInputNames(AxesBitmap endstops, InputPortsBitmap inputs, const StringRef &reply) noexcept
{
	if (endstops.IsEmpty() && inputs.IsEmpty())
	{
		reply.cat(" (none)");
	}
	else
	{
		const char* const axisLetters = reprap.GetGCodes().GetAxisLetters();
		endstops.Iterate([axisLetters, &reply](unsigned int axis, bool) noexcept { reply.catf(" %c", axisLetters[axis]); } );
		inputs.Iterate([&reply](unsigned int port, bool) noexcept { reply.catf(" %d", port); } );
	}
}

// End
