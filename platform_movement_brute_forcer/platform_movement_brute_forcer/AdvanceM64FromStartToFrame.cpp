#include "Script.hpp"
#include "Types.hpp"
#include "Movement.hpp"
#include "Inputs.hpp"

bool AdvanceM64FromStartToFrame::verification()
{
	//Verify initial state exists
	game.load_state(&game.startSave);
	if (game.getCurrentFrame() != 0)
		return false;

	return true;
}

bool AdvanceM64FromStartToFrame::execution()
{
	game.load_state(&game.startSave);

	for (uint64_t frame = 0; frame < _frame; frame++)
	{
		if (frame >= _m64->frames.size())
			_m64->frames.push_back(Inputs(0, 0, 0));

		set_inputs(game, _m64->frames[frame]);
		game.advance_frame();
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