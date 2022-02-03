#include "Script.hpp"
#include "Types.hpp"
#include "Movement.hpp"
#include "Inputs.hpp"

bool AdvanceM64FromStartToFrame::verification()
{
	//Verify initial state exists
	load_state(&game.startSave);
	if (game.getCurrentFrame() != 0)
		return false;

	return true;
}

bool AdvanceM64FromStartToFrame::execution()
{
	load_state(&game.startSave);

	for (uint64_t frame = 0; frame < _frame; frame++)
	{
		advance_frame();
	}

	return true;
}

bool AdvanceM64FromStartToFrame::validation()
{
	//Verify frame is correct
	if (game.getCurrentFrame() != _frame)
		return false;

	return true;
}